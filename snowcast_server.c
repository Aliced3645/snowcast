#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <inttypes.h>
#include <pthread.h>
#define DEBUG

#define MAX_LENGTH 256

struct listen_param{
	int sockfd;	
};

#pragma pack(push,1)
struct control_message{
	uint8_t command_type;
	uint16_t content;
};
#pragma pack(pop)

void* listening_thread_func(void* args){
	struct listen_param* lp = (struct isten_param*)args;
	int server_sockfd = lp->sockfd;
	printf("Server starts listening to connection...\n");
	int rv;
	if((rv = listen(server_sockfd, 0))  == -1){
		close(server_sockfd);
		printf("Listen Error: %s\n", strerror(errno));
		exit(-1);
	}

	//accept a client socket
	int client_sockfd = accept(server_sockfd, 0, 0);
	if(client_sockfd == -1){
		close(client_sockfd);
		close(server_sockfd);
		printf("Accept func error: %s\n", strerror(errno));
		exit(-1);
	}
	
	//Parse the received message
	struct control_message msg;
	int msg_length = 0;
	while( (msg_length = recv(client_sockfd, &msg, sizeof(struct control_message), 0)) > 0){
		uint8_t msg_type = msg.command_type;
		if(msg_type == (uint8_t)0){
			//respond to the hello here		
			printf("Received a hello!\n");
		}
		else if(msg_type == (uint8_t)1){
		
		}
		else{

		}
	}
}

int main(int argc, char** argv){
	
	if(argc < 2){
		printf("Usage: snowcast_server tcpport [file1] [file2] [file3] [...] \n");
		exit(-1);
	}

	char* server_port = argv[1];
	int sockfd;
	struct addrinfo hints, *servinfo;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	
	int rv;
	if((rv = getaddrinfo(NULL, server_port, &hints, &servinfo)) == -1){
		printf("Error in getting %s\n", strerror(errno));
		exit(-1);
	}
	
	if((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1){
		close(sockfd);
		printf("Error in initializing socket: %s\n", strerror(errno));
		exit(-1);
	}	
	
	if((rv = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen)) < 0){
		close(sockfd);
		printf("Error in binding address to socket: %s\n", strerror(errno));
		exit(-1);
	}
	
	//create a single thread for listening tcp mesasges...
	pthread_t listening_thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 20 * 1024 * 1024);
	pthread_create(&listening_thread, &attr, listening_thread_func, 0);
	pthread_join(listening_thread, 0);
	

	return 0;
}
