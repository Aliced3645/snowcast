#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <inttypes.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

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

//this thread may use select() to deal with connections.
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

	//setup vars for select()
	fd_set fd_list, fd_list_temp;
	int fd_max;
	FD_ZERO(&fd_list);
	FD_ZERO(&fd_list_temp);
	
	fd_max = server_sockfd;
	FD_SET(server_sockfd, &fd_list);
	FD_SET(server_sockfd, &fd_list_temp);
	while(1){
		
		fd_list_temp = fd_list;
		if((rv = select(fd_max +1 , &fd_list_temp, NULL, NULL, NULL)) == -1 ){
			printf("An error occured when calling select..: %s\n", strerror(errno));
			return -1;
		}
		
		int i;
		int client_sockfd;
		//to record new connected client's addr..
		struct sockaddr client_connect_addr;
		socklen_t addr_lenth = sizeof(client_connect_addr);

		for ( i = 0 ; i <= fd_max; i++){
			if(FD_ISSET(i, &fd_list_temp)){
				//the server receives a new connection
				if( i == server_sockfd){
					if((client_sockfd = accept(server_sockfd, &client_connect_addr, &addr_lenth)) == -1){
						printf("Error on accepting a client's connection : %s\n", strerror(errno));
						continue;
					}
					else{
						FD_SET(client_sockfd, &fd_list);
						if (client_sockfd > fd_max) 	fd_max = client_sockfd;
						printf("New Connection\n");
					}
				}
				//Receive control message from snowcast_control.
				else{
					//Parse the received message
					struct control_message msg;
					int msg_length = 0;
					if( (msg_length = recv(client_sockfd, &msg, sizeof(struct control_message), 0)) > 0){
						uint8_t msg_type = msg.command_type;
						if(msg_type == (uint8_t)0){
							uint16_t clnt_udpport_n = msg.content;
							uint16_t clnt_udpport_h = ntohs(clnt_udpport_n);
							printf("Received a hello! Connector UDP Port: %d\n", clnt_udpport_h);
				
							//respond to the hello here		
							struct Welcome wl_msg = {(uint8_t)0, station_nums};
							int bytes_sent = send(client_sockfd,(void*)&wl_msg, sizeof(struct Welcome), 0);
							if(bytes_sent == -1){
								printf("An error occured when sending a WELCOME message: %s\n", strerror(errno));
							}
							else if(bytes_sent != 3){
								printf("NOT all contents of the Welcome message were sent..\n");
							}
							else if(bytes_sent == 0){
								printf("A connection ended..\n");
								close(client_sockfd);
								FD_CLR(client_sockfd, &fd_list);
								continue;
							}
						}
						else if(msg_type == (uint8_t)1){
					
						}
						else{
			
						}
					}
					
				}
			}
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
