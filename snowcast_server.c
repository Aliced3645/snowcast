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
static uint16_t station_nums = 20;

struct listen_param{
	int sockfd;	
};

#pragma pack(push,1)
struct control_message{
	uint8_t command_type;
	uint16_t content;
};

struct Welcome{
	uint8_t replyType;
	uint16_t numStations;
};

struct Announce{
	uint8_t replyType;
	uint8_t songnameSize;
	char* sonename;
};

struct InvalidCommand{
	uint8_t replyType;
	uint8_t replyStringSize;
	char* replyString;
};
#pragma pack(pop)

void* listening_thread_func(void* args){
	struct listen_param* lp = (struct listen_param*)args;
	int server_sockfd = lp->sockfd;
	printf("Server starts listening to connection...(local socket ID: %d)\n",server_sockfd);
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
			uint16_t clnt_udpport_n = msg.content;
			printf("Received a hello! Connector UDP Port: %d\n", clnt_udpport);
			
			//respond to the hello here		
			struct Welcome wl_msg = {(uint8_t)0, station_nums};
			int bytes_sent = send(client_sockfd,(void*)&wl_msg, sizeof(struct Welcome), 0);
			if(bytes_sent == -1){
				printf("An error occured when sending a WELCOME message: %s\n", strerror(errno));
			}
			else if(bytes_sent != 3){
				printf("NOT all contents of the Welcome message were sent..\n");
			}
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
#ifdef DEBUG
	printf("Socket ID: %d\n", sockfd);
#endif
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
	struct listen_param* lp = (struct listen_param*)malloc(sizeof(struct listen_param));
	lp->sockfd = sockfd;
	pthread_create(&listening_thread, &attr, listening_thread_func, lp);
	pthread_join(listening_thread, 0);
	
	free(lp);
	return 0;
}
