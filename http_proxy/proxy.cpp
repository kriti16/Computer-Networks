#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h> 
#include <bits/stdc++.h>
#include <dirent.h>
#include <netinet/tcp.h>
// To check whether file or directory
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
// Fork child
#include <unistd.h>
// Parsing 
#include "proxy_parse.h"
#include <netdb.h>
#include <sys/wait.h> 
#include <sys/mman.h>

#define BUFFER_SIZE 8192
#define SMALL 30
#define VERY_SMALL 10
#define MAX 30

using namespace std;

/*
    Function: Handles client's request
*/
void serveClient(int clientFileDescriptor){
    char input[BUFFER_SIZE];
    int socketFileDescriptor ;

    // Receive request from client
    bzero(input,BUFFER_SIZE);
    int bytesReadNow; 
    int tillNow = 0;


    char* endIndex;
    do{
        bytesReadNow = recv(clientFileDescriptor,input+tillNow,BUFFER_SIZE,0);
        endIndex = strstr(input, "\r\n\r\n");
        tillNow+=bytesReadNow;
        if (bytesReadNow == 0){
            break;
        }
        if (bytesReadNow < 0){
            printf("Error in connection.\n");
            return;
        }
    }while(endIndex==NULL);

    // Client doesn't send request
    if(bytesReadNow<=0){
        return;
    }

    // Parse request
    ParsedRequest *req = ParsedRequest_create();
    if(ParsedRequest_parse(req,input,strlen(input))<0){
        printf("parse failed\n");
        char reply[BUFFER_SIZE];
        // goto label;
        sprintf(reply, "HTTP/1.0 500 Internal Server Error\r\nContent-Type: text/html\r\n\r\n");
        //send reply
        int len = strlen(reply);
        send(clientFileDescriptor,reply,len,0);
        return;
    }  
    if (ParsedHeader_set(req, "Host", req->host) < 0){
     printf("set header key not work\n");
     return;

    }

    if (ParsedHeader_set(req, "Connection", "close") < 0){
     printf("set header key not work\n");
     return;

    }
    // Default port 80
    if(req->port==NULL){
        req->port = new char[4];
        sprintf(req->port,"80");
    }

    // set http version to 1.0
    if(strcmp(req->version,"HTTP/1.0")){
        // cout<<"Did version change  from :"<<req->version<<endl;
        sprintf(req->version,"HTTP/1.0");
    }

    socketFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);

    if(socketFileDescriptor<0){
        cout<<"Couldn't create socket\n";
        return;
    }

    // Server address information
    struct sockaddr_in serverAddress; 
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(atoi(req->port));

    // Get IP
    struct in_addr **addr_list = (struct in_addr **) gethostbyname(req->host)->h_addr_list; 
    for(int i = 0; addr_list[i] != NULL; i++){
        serverAddress.sin_addr = *addr_list[i];         
        // cout<<req->host<<" resolved to "<<inet_ntoa(*addr_list[i])<<endl;         
        break;
    }

    // Connect to server
    int status= connect(socketFileDescriptor, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    if(status != 0){
      fprintf(stderr,"Trying to connect\n");
    }
    
    // Build request for server
    int rlen = ParsedRequest_totalLen(req);
    char *b = (char *)malloc(rlen+1);
    bzero(b,rlen);
    if (ParsedRequest_unparse(req, b, rlen) < 0) {
        printf("unparse failed\n");        
    }
    b[rlen]='\0';

    // Send request to server
    int bytesSent = send(socketFileDescriptor,b,strlen(b),0);
    // cout<<"Request sent to server: "<<bytesSent<<endl;
    // cout<<b<<"\n-------------------------------------\n";
    free(b);

    // Receive response from server and send it to client
    char serverResponse[BUFFER_SIZE];
    bytesSent=0;
    bzero(serverResponse,BUFFER_SIZE);
    bytesReadNow = recv(socketFileDescriptor,&serverResponse,BUFFER_SIZE,0);
    // cout<<"Read: "<<bytesReadNow<<endl;
    while(bytesReadNow>0){               
        bytesSent += send(clientFileDescriptor,serverResponse,bytesReadNow,0);
        // cout<<"Sent: "<<bytesSent<<endl;
        bzero(serverResponse,bytesReadNow);
        bytesReadNow = recv(socketFileDescriptor,&serverResponse,BUFFER_SIZE,0);        
    }

    close(socketFileDescriptor);
}
static int* numChild;
int main(int argc, char *argv[]){

    if(argc!=2){
        cout<<"Please enter port-number\n";
        return 1;
    }
 
    // Create Socket 
    int socketFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    int enable = 1;
    if (setsockopt(socketFileDescriptor, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        cout<<"setsockopt(SO_REUSEADDR) failed\n";

    if(socketFileDescriptor<0){
        cout<<"Couldn't create socket\n";
        return 1;
    }

    //server Address information
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);              //binds to all available interfaces
    serverAddress.sin_port = htons(atoi(argv[1]));

    //Assign socket address to declare socket
    int fails=bind(socketFileDescriptor, (struct sockaddr*)&serverAddress, sizeof(serverAddress)); 
    if(fails){
        fprintf(stderr,"Couldn't bind to the port: %d\n",atoi(argv[1]));
        return 1;
    }

    //Start listening on the port, maximum allowed clients is 20
    fails=listen(socketFileDescriptor,5); 
    if(!fails){
        cout<<"Listening...\n";
    }

    numChild = (int*)mmap(NULL, sizeof *numChild, PROT_READ | PROT_WRITE, 
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    bool done = false;
    int status;

    // Fork children on connection
    while(!done){
        int clientFileDescriptor = accept(socketFileDescriptor,NULL,NULL);
        while(*numChild>=MAX){
            wait(&status);
        }
        int pid = fork();
        if(pid==0){
            *numChild = *numChild + 1;
            serveClient(clientFileDescriptor);
            close(clientFileDescriptor);
            done = true;
            *numChild = *numChild - 1;
        }
        else if(pid<0){
            cout<<"unable to fork\n";
            return 1;
        }else{
            close(clientFileDescriptor);
        }
    }

    return 0;
}