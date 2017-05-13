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
#include "proxy_parse.h"

#define BUFFER_SIZE 1024
#define SMALL 30

using namespace std;

void handler(int a){
    printf("Here");
}

int main(int argc, char *argv[]){
    signal(SIGPIPE,handler);
    if(argc!=2){
        cout<<"Please enter port-number\n";
        return 1;
    }
 
    // Create Socket 
    int socketFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    int flag = 1;
    if(socketFileDescriptor<0){
        cout<<"Couldn't create socket\n";
        return 1;
    }

    //server Address information
    struct sockaddr_in serverAddress, clientAddress;
    serverAddress.sin_family = AF_INET;
    // serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");         //binds only to localhost
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);              //binds to all available interfaces
    serverAddress.sin_port = htons(atoi(argv[1]));

    //Assign socket address to declared socket
    int fails=bind(socketFileDescriptor, (struct sockaddr*)&serverAddress, sizeof(serverAddress)); 
    if(fails){
        fprintf(stderr,"Couldn't bind to the port: %d\n",atoi(argv[1]));
        return 1;
    }

    //Start listening on the port, maximum allowed clients is 5
    fails=listen(socketFileDescriptor,5); 
    if(!fails){
        cout<<"Listening...\n";
    }
   
    while(true){
        //Wait for client
        int clientFileDescriptor = accept(socketFileDescriptor,NULL,NULL);
        char request[SMALL];
        while(true){   
            int recData = recv(clientFileDescriptor,request,SMALL,0);
            if(recData == 0){
                printf("Waiting for client ....\n");
                break;
            }
            while(recData < SMALL){
                recData += recv(clientFileDescriptor,request+recData,SMALL-recData,0);
            }     
            cout<<"Request : "<<request<<endl;
            /*
            bool matchFound = false;
             //Open current working directory
            DIR* pwd = opendir(".");
            struct dirent* file;
            // Search for the file
            while ((file = readdir(pwd)) != NULL){
                if(!strcmp(file->d_name,filename)){
                    matchFound = true;
                    break;
                }
            }
            
            // Match not found
            if(!matchFound){
                char response[]="no such file exists\n";
                int len = strlen(response)+1;
                send(clientFileDescriptor,&len,sizeof(len),0);
                fwrite(response,1,len,clientWrite);
                fflush(clientWrite);
                continue;
            }
            else{
                FILE* fp = fopen(filename,"r");
                char response[BUFFER_SIZE];
                int bytesRead = 0;
                // Compute file size and send it to client
                fseek(fp, 0L, SEEK_END);
                int fileSize = ftell(fp);
                fseek(fp, 0L, SEEK_SET);
                send(clientFileDescriptor,&fileSize,sizeof(fileSize),0);
                // Send small chunks of file data
                while((bytesRead = fread(response, 1, BUFFER_SIZE, fp)) > 0){               
                    fwrite(response,1,bytesRead,clientWrite);
                    fflush(clientWrite);
                }           
                fclose(fp);
            }
            closedir(pwd);
            */
        }
    }

    return 0;
}
