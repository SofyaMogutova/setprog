#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <malloc.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

struct user{
    char name[50];
    struct sockaddr_in endp;
};
struct message{
    char text[1000];
    struct user sender;
};
struct user_env{
    struct user user;
    int user_fd;
    char deleted;
    pthread_t tid;
};
int usercmp(struct user u1, struct user u2){
    if(u1.endp.sin_addr.s_addr==u2.endp.sin_addr.s_addr && u1.endp.sin_port==u2.endp.sin_port) return 1;
    else return 0;
}
char** split(char* str, int* n){
    int i=0;
    int spaces=0;
    while(str[i]!=0){
        if(str[i]==' ') spaces++;
        i++;
    }
    char** res=malloc((spaces+1)*sizeof(char*));
    i=0;
    int j;
    for(j=0;j<=spaces;j++){
        res[j]=malloc(50);
        int k=0;
        while(str[i]!='\0'){
            if(str[i]==' '){
                break;
            }
            res[j][k]=str[i];
            k++;
            i++;
        }
        res[j][k]=0;
        i++;
    }
    *n=spaces+1;
    return res;
}
struct user_env* users;
int users_count=0,active_users_count=0,size=0;
void* user_chat(void* arg){
    struct user_env client=*(struct user_env*)arg;
    int socket=socket(AF_INET,SOCK_DGRAM,0);
    if(socket==-1){
        perror("udp socket thread");
        pthread_exit(NULL);
    }
    client.user_fd=socket;
    for(int i=0;i<users_count;i++){
        if(usercmp(users[i].user,client.user)){
            users[i].user_fd=socket;
            break;
        }
    }
    struct message msg;
    strcpy(msg.text,"Ready"); 
    if(sendto(client.user_fd,&msg,sizeof(msg),0,(struct sockaddr*)&client.user.endp,sizeof(struct sockaddr_in))==-1){
            perror("sendto Ready");
            pthread_exit(NULL);
    }
    while(1){
        socklen_t cl_size=sizeof(struct sockaddr_in);
        if(recvfrom(client.user_fd,&msg,sizeof(msg),0,(struct sockaddr*)&client.user.endp,&cl_size)==-1){
            perror("recvfrom");
            continue;
        }
        int msg_len;
        char** msg_split=split(msg.text,&msg_len);
        if(strcmp(msg_split[0],"Name")==0){
            for(int i=0;i<users_count;i++){
                if(usercmp(users[i].user,client.user)){
                    strcpy(users[i].user.name,msg_split[1]);
                }
            }
            strcpy(client.user.name,msg_split[1]);
            printf("Name %s %s(%d)\n",msg_split[1],inet_ntoa(client.user.endp.sin_addr),ntohs(client.user.endp.sin_port));
        }
        else if(strcmp(msg_split[0],"Exit")==0){
            for(int i=0;i<users_count;i++){
                if(usercmp(users[i].user,client.user)){
                    users[i].deleted=1;
                    active_users_count--;
                    close(users[i].user_fd);
                    printf("Exit %s %s(%d)\n",client.user.name,inet_ntoa(client.user.endp.sin_addr),ntohs(client.user.endp.sin_port));
                    pthread_exit(NULL);
                }
            }
        }
        else{
            msg.sender=client.user;
            for(int i=0;i<users_count;i++){
                if(users[i].deleted==0){
                    if(sendto(users[i].user_fd,&msg,sizeof(msg),0,(struct sockaddr*)&users[i].user.endp,sizeof(struct sockaddr_in))==-1){
                        perror("sendto user");
                    }
                }
            }
        }
        for(int i=0;i<msg_len;i++){
            free(msg_split[i]);
        }
        free(msg_split);
    }
}
void* service(void* arg){
    int socket =*(int*)arg;
    users = malloc(sizeof(struct user_env)*10);
    size=10;
    while(1){
        struct sockaddr_in client;
        socklen_t cl_size=sizeof(client);
        struct message msg;
        if(recvfrom(socket,&msg,sizeof(msg),0,(struct sockaddr*)&client,&cl_size)==-1){
            perror("recvfrom service");
            continue;
        }
        printf("Udp recv %s(%d)\n",inet_ntoa(client.sin_addr),ntohs(client.sin_port));
        struct user_env new_user;
        new_user.user.endp=client;
        new_user.deleted=0;
        pthread_t user_handler;

        if(pthread_create(&user_handler,NULL,user_chat,&new_user)!=0){
            perror("pthread_create");
            struct message msg;
            strcpy(msg.text,"Nready");
            while(sendto(socket,&msg,sizeof(msg),0,(struct sockaddr*)&client,sizeof(struct sockaddr_in))==-1){
                perror("sendto Nready");
            }
            continue;
        }
        new_user.tid=user_handler;
        int i;
        for(i=0;i<users_count;i++){
            if(users[i].deleted==1){
                users[i]=new_user;
                active_users_count++;
                break;
            }
        }
        if(i==users_count){
            if(active_users_count>=size){
                users=realloc(users,size*2);
                size*=2;
            }
            users[users_count++]=new_user;
            active_users_count++;
        }
    }
}
int main(){
    int socket=socket(AF_INET,SOCK_DGRAM,0);
    if(socket==-1){
        perror("socket listen");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in serv;
    serv.sin_family=AF_INET;
    serv.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int binded=0;
    for(int i=5000;i<6000;i++){
        serv.sin_port=htons(i);
        if(bind(socket,(struct sockaddr*)&serv,sizeof(serv))!=-1){
            printf("Port: %d\n",i);
            binded=1;
            break;
        }
    }
    if(!binded){
        serv.sin_port=htons(5000);
        if(bind(socket,(struct sockaddr*)&serv,sizeof(serv))!=-1){
            perror("bind");
            exit(EXIT_FAILURE);
        }
    }
    pthread_t srv_handler;
    pthread_create(&srv_handler,NULL,service,&socket);
    while(1){
        char c=getchar();
        if(c=='e'){
            pthread_cancel(srv_handler);
            for(int i=0;i<users_count;i++){
                if(!users[i].deleted){
                    struct message msg;
                    strcpy(msg.text,"Dead");
                    msg.sender.endp=serv;
                    if(sendto(users[i].user_fd,&msg,sizeof(msg),0,(struct sockaddr*)&users[i].user.endp,sizeof(struct sockaddr_in))==-1){
                        perror("sendto");
                    }
                    pthread_cancel(users[i].tid);
                    close(users[i].user_fd);
                }
            }
            exit(EXIT_SUCCESS);
        }
        else if(c=='u'){
            for(int i=0;i<users_count;i++){
                if(!users[i].deleted)
                    printf("%s %s(%d)\t%d\tUDP\t%d\n",users[i].user.name,inet_ntoa(users[i].user.endp.sin_addr),ntohs(users[i].user.endp.sin_port),users[i].deleted,users[i].user_fd);
            }
            printf("Total users count: %d\nActive users count:%d\nThreads: %d\n",users_count,active_users_count,size);
        }
        while(getchar()!='\n');
    }
}
