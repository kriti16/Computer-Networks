#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h> 
#include <bits/stdc++.h>

#define BUFFER_SIZE 1024
#define SMALL 30

using namespace std;

int socketFileDescriptor;
int main(int argc, char *argv[]){
    
    if(argc != 3){
      cout<<"Please enter <server-ip> <server-port>\n";
      return 1;
    } 

    // Create socket
    label:
    socketFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    FILE* socketRead = fdopen(socketFileDescriptor,"r+"); 
    if(socketFileDescriptor<0){
        cout<<"Couldn't create socket\n";
        return 1;
    }

    //Server address information
    struct sockaddr_in serverAddress; 
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(argv[1]);
    serverAddress.sin_port = htons(atoi(argv[2])); 

    // Connect to server
    int status= connect(socketFileDescriptor, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    if(status != 0){
      fprintf(stderr,"Trying to connect\n");
      sleep(4);
      goto label;
    }

    // Receive welcome message
    char filename[SMALL];
    char response[BUFFER_SIZE+1];
    int recData = recv(socketFileDescriptor,response,SMALL,0);

    if(recData == 0){
      fprintf(stderr,"Trying to connect\n");
      sleep(4);
      goto label;
    }

    while(recData < SMALL){
      recData += recv(socketFileDescriptor,response+recData,SMALL-recData,0);
    }
    cout<<response;
    bool first = true;

    while(true){
      // First prompt by serevr and future prompts by client
      if(!first){
        cout<<"Enter filename:";
      }else{
        first = false;
      }
      // Ask user for filename
      cin>>filename;

      int dataSent = send(socketFileDescriptor,filename,SMALL,0);
      while(dataSent<SMALL){
        cout<<"\n\nOnly "<<dataSent<<" bytes were sent instead of "<<SMALL<<" bytes\n\n";
        dataSent += send(socketFileDescriptor,filename+dataSent,SMALL-dataSent,0);
      }
      int fileSize;

      int rec = recv(socketFileDescriptor,&fileSize,sizeof(fileSize),0);

      // Check whether connected to server or not
      if(rec == 0){
        fprintf(stderr,"Server disconnected. Trying to connect\n");
        sleep(4);
        goto label;
      }

      // Receive data in chunks and print on stdout
      do{        
        if(fileSize<BUFFER_SIZE){
          int received = fread(response,1,fileSize,socketRead);
          if(received<fileSize){
            cout<<"\n\nOnly "<<received<<" bytes were received instead of "<<fileSize<<" bytes\n\n";
          }
          response[fileSize]='\n';
          response[fileSize+1]='\0';
          fileSize = 0;
        }
        else{
          int received = fread(response,1,BUFFER_SIZE,socketRead);
          if(received<BUFFER_SIZE){
            cout<<"\n\nOnly "<<received<<" bytes were received instead of "<<BUFFER_SIZE<<" bytes\n\n";
          }
          response[BUFFER_SIZE]='\0';
          fileSize -= BUFFER_SIZE;
        }
        cout<<response;
      }
      while(fileSize>0);
    }
    return 0;
}