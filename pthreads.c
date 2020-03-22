#include <stdio.h>
#include <stdlib.h>

#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
#include <pthread.h>
#include<fcntl.h>
// execute the user command



  
#define MAXCOM 1000 // max number of letters to be supported 
#define MAXLIST 100 // max number of commands to be supported 
  
// Clearing the shell using escape sequences 
#define clear() printf("\033[H\033[J") 

#define SHELL_SERVER_PORT 8093 
#define FILE_SERVER_PORT 8091

void *create_shell_server( int ptr );
void *create_file_server( void *ptr );
void execommand(char*,char*);
// void do_client(int sd);




void clientThread(int cp ){
     char buffer[1024] = {0}; 
     char res[2000] = {0}; 
     char *hello = "sample message";
     int valread = read( cp , buffer, 1024); 
		
    printf("Client Command:%s\n diff %d",buffer ,strcmp(buffer,"CPRINT")); 
    if(strcmp(buffer,"CPRINT") != 10){
          execommand(buffer,res);
          send(cp , res , strlen(hello) , 0 ); 
          printf("Need to execute command\n");
    }else{
    char buff[2000];
    char finalres[2500];
    int fp = open("log", O_RDONLY, S_IRUSR | S_IWUSR);
    strcat(finalres,"OK SUCCESS\n");
    read(fp, buff, 2000);
    strcat(finalres,buff);
     send(cp , finalres , strlen(buff) , 0 );           
          printf("CPRINT Requested\n");
    }
}

void fileServerclientThread(int cp ){
     char buffer[1024] = {0}; 
     char res[2000] = {0}; 
     char *hello = "sample message";
     int valread = read( cp , buffer, 1024); 
		
    printf("Client Command:%s ",buffer); 

    
    
    
    int i = 0;
    char *p = strtok (buffer, " ");
    char *array[3];

    while (p != NULL)
    {
        array[i++] = p;
        p = strtok (NULL, " ");
    }

//     for (i = 0; i < 3; ++i) 
        // printf("%s: %d, i=%d", array[0],strcmp(array[0],"FOPEN"),i);

    if(strcmp(array[0],"FOPEN")==10 ||strcmp(array[0],"FOPEN")==0 ){
    if(i>1){
    int openfileDesp = open("log", O_RDWR);
    // printf("Filename: %s, %d",array[1],openfileDesp);
    if(openfileDesp==-1){
            char *re = "FAIL TO OPEN";
            send(cp , re , strlen(re) , 0 ); 
    }else{
            char *re;
            char num[5];
            strcat(re,"OK ");
            snprintf(num,5,"%d",openfileDesp);
            printf("dasd : %s",num);
            strcat(re," 10");
            send(cp , num , strlen(re) , 0 ); 
    }
    
   
    }else{
    	char *re = "ARGUMENT MISSING";
          send(cp , re , strlen(re) , 0 ); 
    }
          
    }else if(strcmp(buffer,"FSEEK") ==10 ||strcmp(array[0],"FSEEK")==0 ){
     if(i>2){
     	// 
     
    	  char *re = "OK";
     	lseek(atoi(array[1]), atoi(array[2]),0);
          send(cp , re , strlen(re) , 0 ); 
    }else{
    	char *re = "FSEEK ARGUMENT MISSING";
          send(cp , re , strlen(re) , 0 ); 
    } 
    }else if(strcmp(buffer,"FREAD") == 10 ||strcmp(array[0],"FREAD")==0 ){
     if(i>2){
    char *re ;
     read(atoi(array[1]), re, atoi(array[2]));
          send(cp , re , strlen(re) , 0 ); 
    }else{
    	char *re = "FREAD ARGUMENT MISSING";
          send(cp , re , strlen(re) , 0 ); 
    }
    }else if(strcmp(buffer,"FWRITE") == 10 ||strcmp(array[0],"FWRITE")==0 ){
     if(i>2){
          write(atoi(array[1]), array[2], strlen(array[2])); 
    	  char *re = "OK";
          send(cp , re , strlen(re) , 0 ); 
    }else{
    	char *re = "FWRITE ARGUMENT MISSING";
          send(cp , re , strlen(re) , 0 ); 
    } 
    }else if(strcmp(buffer,"FCLOSE") == 10 ||strcmp(array[0],"FCLOSE")==0 ){
    if(i>1){
    char *re = "FCLOSE REQUESTED";
          send(cp , re , strlen(re) , 0 ); 
    }else{
    	char *re = "FCLOSE ARGUMENT MISSING";
          send(cp , re , strlen(re) , 0 ); 
    }
    }else{
    char *re = "INVALID REQUEST";
          send(cp , re , strlen(re) , 0 );
    }
}


int main()
{
     pthread_t shellServerThread, fileServerThread;
     char *message1 = "Thread 1";
     char *message2 = "Thread 2";
     int  shellServer, fileServer;

    /* Create independent threads each of which will execute function */
       shellServer = pthread_create( &shellServerThread, NULL, create_shell_server, SHELL_SERVER_PORT);
//      fileServer = pthread_create( &fileServerThread, NULL, create_file_server, FILE_SERVER_PORT);

       pthread_join( shellServerThread, NULL);
//      pthread_join( fileServerThread, NULL); 

     exit(0);       
   
    return 0; 
}



// shell server Program
void *create_shell_server( int ptr )
{
     
    printf("Shell Server is running on port %d \n",ptr);

    int server_fd, new_socket, valread; 
    struct sockaddr_in address; 
    int opt = 1; 
    int addrlen = sizeof(address); 
    char buffer[1024] = {0}; 
    char *hello = "Hello from File server"; 
    char *res;

    pthread_t tt;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);
       
    // Creating socket file descriptor 
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    { 
        perror("socket failed"); 
        exit(EXIT_FAILURE); 
    } 
       
    // Forcefully attaching socket to the port 8080 
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, 
                                                  &opt, sizeof(opt))) 
    { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    } 
    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons( ptr ); 
       
    // Forcefully attaching socket to the port 8080 
    if (bind(server_fd, (struct sockaddr *)&address,  
                                 sizeof(address))<0) 
    { 
        perror("bind failed"); 
        exit(EXIT_FAILURE); 
    } 
    if (listen(server_fd, 3) < 0) 
    { 
        perror("listen"); 
        exit(EXIT_FAILURE); 
    } 

while(1){


    if ((new_socket = accept(server_fd, (struct sockaddr *)&address,  
                       (socklen_t*)&addrlen))<0) 
    { 
        perror("accept"); 
        exit(EXIT_FAILURE); 
    } 

    if ( pthread_create(&tt, &ta, (void* (*) (void*))clientThread, (void*)new_socket) != 0 ) {
            perror("pthread_create");
            return 1;
        }    

}

}


// file server Program
void *create_file_server( void  *ptr )
{
     
     printf("File Server is running on port %d \n",ptr);


    int server_fd, new_socket, valread; 
    struct sockaddr_in address; 
    int opt = 1; 
    int addrlen = sizeof(address); 
    char buffer[1024] = {0}; 
    char *hello = "Hello from File server"; 
    char *res;

    pthread_t tt;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);
       
    // Creating socket file descriptor 
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    { 
        perror("socket failed"); 
        exit(EXIT_FAILURE); 
    } 
       
    // Forcefully attaching socket to the port 8080 
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, 
                                                  &opt, sizeof(opt))) 
    { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    } 
    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons( ptr ); 
       
    // Forcefully attaching socket to the port 8080 
    if (bind(server_fd, (struct sockaddr *)&address,  
                                 sizeof(address))<0) 
    { 
        perror("bind failed"); 
        exit(EXIT_FAILURE); 
    } 
    if (listen(server_fd, 3) < 0) 
    { 
        perror("listen"); 
        exit(EXIT_FAILURE); 
    } 

while(1){


    if ((new_socket = accept(server_fd, (struct sockaddr *)&address,  
                       (socklen_t*)&addrlen))<0) 
    { 
        perror("accept"); 
        exit(EXIT_FAILURE); 
    } 

    if ( pthread_create(&tt, &ta, (void* (*) (void*))fileServerclientThread, (void*)new_socket) != 0 ) {
            perror("pthread_create");
            return 1;
        }    

}

}





void execommand(char* command, char* result){
     size_t buff_size = 200;
     char *buffer = (char*) malloc(buff_size * sizeof(char));
     char *err = (char*)malloc(buff_size* sizeof(char));
     FILE *fp = popen(command,"r");

	int creatLogFile = open("log", O_WRONLY| O_CREAT| O_TRUNC, 644);
  	int standredoutput = dup(1);
  	int standerdError = dup(2);
	dup2(creatLogFile, 1);
  	dup2(creatLogFile,2);
  	system(command);
  	dup2(standredoutput,1);
  	dup2(standerdError, 2);
  	close(creatLogFile);

     if(!fp){
     	  strcat(result,"OK");
          strcat(result," ERR");
          strcat(result,"Unable to process");
          return;
     }
     getline(&buffer,&buff_size,fp);
     printf("Output: %s",buffer);
     int status = pclose(fp);
     char temp_status[5];
     snprintf(temp_status,5,"%d",status);
     if(status ==0){
          strcat(result,"OK ");
          strcat(result,temp_status);
          strcat(result," SUCESS\n");

     }else{
          strcat(result,"ERR ");
          strcat(result,temp_status);
          strcat(result," FAILED\n");
     }


}




/*
void do_client(int sd){

const int ALEN = 256;
char req[ALEN];
const char* ack = "ACL:\n";
char resp[2000];
char *prev_serv_op = (char*) malloc;
int n;

strcpy(resp,ack);
prinf("Incomming client...");
while((n=fgets(sd,req,ALEN-1))!=0){
     if(strcmp(req,"CPRINT")==0){
     if(strlen(prev_serv_op)!=0){
          strcat(resp,prev_serv_op);
     }else{
          strcat(resp,"Sorry there are no previous command");
     }
     }else{
          execommand(req,resp);
     }
     send(sd,resp,strlen(resp),0);
     sleep(5);
}

printf("connection closed by client, \n");
shutdown(sd,1);
close(sd);
printf("outgoinf client....\n");

}
*/
