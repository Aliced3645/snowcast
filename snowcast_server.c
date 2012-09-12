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
#define DEFAULT_CHANNEL_NUM 2

struct listen_param{
	int* sockfds;	
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

//manage information data structure
struct client_info;
struct client_info_manager{
	uint8_t client_total_number;
	struct client_info *first_client;
	struct client_info *last_client;
};

struct client_info{
	struct client_info* next_client_info;
	struct client_info* channel_next_client;
	struct sockaddr client_connect_addr;
	char* client_readable_addr;
	
	uint16_t client_udp_port;
	uint16_t client_channel;
	uint16_t client_sockfd;
};

//manage information of channel
struct channel_info;
struct channel_info_manager{
	uint8_t channel_total_number;
	struct channel_info* first_channel;
	struct channel_info* last_channel;
};

struct channel_info{
	char* song_name;
	struct channel_info* next_channel_info;
	uint8_t client_total_number;
	struct client_info* first_client;
	struct client_info* last_client;
};

//initialize the client info manager and channel info manager..
struct client_info_manager g_client_info_manager = {0, NULL, NULL};
struct channel_info_manager g_channel_info_manager = {DEFAULT_CHANNEL_NUM, NULL, NULL};

//channel structures
struct client_info* get_client_info_by_socket(int sockfd){
	struct client_info* info_traverser = g_client_info_manager.first_client;
	while(info_traverser != NULL){
		if(info_traverser-> client_sockfd == sockfd){
			return info_traverser;
		}
		info_traverser = info_traverser->next_client_info;	
	}
	return NULL;
}

//delete a client info from the list.
//return -1 if failed
int delete_client_info(int client_sockfd){
	if(g_client_info_manager.client_total_number == 0)
		return -1;	
	if(g_client_info_manager.client_total_number == 1){
		if(g_client_info_manager.first_client->client_sockfd == client_sockfd){
			free(g_client_info_manager.first_client->client_readable_addr);
			free(g_client_info_manager.first_client);
			g_client_info_manager.client_total_number = 0;
			g_client_info_manager.first_client = g_client_info_manager.last_client = 0;
			return client_sockfd;
		}
		else
			return -1;
	}
	struct client_info* pre,*next;
	pre = g_client_info_manager.first_client;
	next = pre->next_client_info;
	if(pre->client_sockfd == client_sockfd){
		g_client_info_manager.client_total_number -- ;
		g_client_info_manager.first_client = next;
		free(pre->client_readable_addr);
		free(pre);
		return client_sockfd;
	}
	while(next != 0){
		if(next->client_sockfd == client_sockfd){
			//if it is the last one to be deleted, then update the last_client
			if(next == g_client_info_manager.last_client)
				g_client_info_manager.last_client = pre;
			pre->next_client_info = next->next_client_info;
			g_client_info_manager.client_total_number --;
			free(next->client_readable_addr);
			free(next);
			return client_sockfd;
		}
		next = next->next_client_info;
		pre = pre->next_client_info;	
	}

	return -1;
}

int find_sockfd(int *fds, int target){
	int size = sizeof(fds);
	int i;
	for(i = 0; i < size; i ++){
		if( fds[i] == target)
			return target;	
	}
	return -1;
}

//convert binary address to readable address (just within the client_info struct)
void convert_binary_addr_to_readable_addr(struct client_info* target){
		struct sockaddr binary_addr = target->client_connect_addr;
		char* readable_addr = (char*)malloc(MAX_LENGTH);
		memset(readable_addr, 0, MAX_LENGTH);
		inet_ntop(AF_INET, &(((struct sockaddr_in*)&binary_addr)->sin_addr),readable_addr,INET_ADDRSTRLEN); 
		target->client_readable_addr = readable_addr;
		return;
}

//this thread may use select() to deal with connections.
void* listening_thread_func(void* args){
	struct listen_param* lp = (struct listen_param*)args;
	int *server_sockfds = lp->sockfds;
	//setup vars for select()
	fd_set fd_list, fd_list_temp;
	int fd_max = server_sockfds[0];
	FD_ZERO(&fd_list);
	FD_ZERO(&fd_list_temp);
	int i;
	int rv;
	for(i = 0; i < 2; i ++){
		FD_SET(server_sockfds[i], &fd_list);
		FD_SET(server_sockfds[i], &fd_list_temp);
		if (server_sockfds[i] >  fd_max)
			fd_max = server_sockfds[i];
		printf("Server starts listening to connection...(local socket ID: %d)\n",server_sockfds[i]);
		if((rv = listen(server_sockfds[i], 0))  == -1){
			close(server_sockfds[i]);
			printf("Listen Error: %s\n", strerror(errno));
			exit(-1);
		}
	}

	while(1){
		fd_list_temp = fd_list;
		if((rv = select(fd_max +1 , &fd_list_temp, NULL, NULL, NULL)) == -1 ){
			printf("An error occured when calling select..: %s\n", strerror(errno));
			exit(-1);
		}
		
		int i;
		int client_sockfd;
		//to record new connected client's addr..
		struct sockaddr client_connect_addr;
		socklen_t addr_lenth = sizeof(client_connect_addr);
		int server_sockfd;
		for ( i = 0 ; i <= fd_max; i++){
			if(FD_ISSET(i, &fd_list_temp)){
				//the server receives a new connection
				if( (server_sockfd = find_sockfd(server_sockfds,i)) != -1){
					if((client_sockfd = accept(server_sockfd, &client_connect_addr, &addr_lenth)) == -1){
						printf("Error on accepting a client's connection : %s\n", strerror(errno));
						continue;
					}
					else{
						FD_SET(client_sockfd, &fd_list);
						if (client_sockfd > fd_max) 	
								fd_max = client_sockfd;
						//record the info
						struct client_info* new_client_info = malloc(sizeof(struct client_info));
						memset(new_client_info, 0, sizeof(struct client_info));
						new_client_info->client_connect_addr = client_connect_addr;
						convert_binary_addr_to_readable_addr(new_client_info);
						new_client_info->client_sockfd = client_sockfd;
						//leave udp port be to filled in the later process...
						if(g_client_info_manager.client_total_number == 0){
							g_client_info_manager.first_client = new_client_info;
							g_client_info_manager.last_client = new_client_info;
						}
						else{
							g_client_info_manager.last_client -> next_client_info = new_client_info;
							g_client_info_manager.last_client = new_client_info;
						}
						g_client_info_manager.client_total_number ++;

					}
				}
				//Receive control message from snowcast_control.
				else{
					//Parse the received message
					client_sockfd = i;
					struct control_message msg;
					int msg_length = 0;
					if( (msg_length = recv(client_sockfd, &msg, sizeof(struct control_message), 0)) > 0){
						uint8_t msg_type = msg.command_type;
						if(msg_type == (uint8_t)0){
							//receive a Hello
							uint16_t clnt_udpport_n = (uint16_t)msg.content;
							uint16_t clnt_udpport_h = ntohs(clnt_udpport_n);
							printf("Received a hello! Connector UDP Port: %d\n", clnt_udpport_h);
							//record the udp port to the client_info struct
							struct client_info* target_client_info = get_client_info_by_socket(client_sockfd);
							if(target_client_info == NULL){
								printf("Error in fetching client info!\n");
								exit(-1);	
							}
							target_client_info -> client_udp_port = clnt_udpport_h;
							//respond to the hello here		
							struct Welcome wl_msg = {(uint8_t)0, DEFAULT_CHANNEL_NUM};
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
								delete_client_info(client_sockfd);
								continue;
							}
						}
						else if(msg_type == (uint8_t)1){
							//receive a SetStation
							uint16_t channel_num = (uint16_t)msg.content;
							//get the client info to know the address and port
							struct client_info* current_client =  get_client_info_by_socket(client_sockfd);
							current_client->client_channel = channel_num;
							uint16_t port_num = current_client->client_udp_port;
							char* readable_addr = current_client->client_readable_addr;
							printf("Received a SetStation! The client (%s:%d) turns to the channel [%d]. \n", readable_addr, port_num, channel_num);
							
						}
						else{
			
						}
					}
					else if(msg_length == 0){
						printf("A client [socket num: %d] has disconnected to the server..\n", client_sockfd);
						close(client_sockfd);
						FD_CLR(client_sockfd, &fd_list);
						//delete a client_info..
						if(delete_client_info(client_sockfd) == -1){
							printf("An error occured when updating the client information list..\n");
							exit(-1);
						}					
					}
				}
			}
		}
	}
}


//this thread for simple user-interation.
void* instruction_thread_func(void* param){
	char input_msg[MAX_LENGTH];
	memset(input_msg, 0, MAX_LENGTH);
	while(1){
		fgets(input_msg, MAX_LENGTH, stdin);
		if( (strlen(input_msg) > 0) && (input_msg[strlen(input_msg) - 1] == '\n'))
			input_msg[strlen(input_msg) - 1] = '\0';
		//parse the instruction
		char instruction = input_msg[0];
		if(instruction == 'p'){
			//print all connected clients ( and channel info to be implemented )
			struct client_info* info_traverser = g_client_info_manager.first_client;
			while(info_traverser != NULL){
				uint16_t port = info_traverser->client_udp_port;
				uint16_t channel = info_traverser->client_channel;
				char* readable_addr = info_traverser->client_readable_addr;
				printf("Client Address: [%s:%d], channel: [%d] \n", readable_addr, port, channel);
				info_traverser = info_traverser -> next_client_info;
			}	
		}
		else if(instruction == 'q'){
			//release all structure objects
			while(g_client_info_manager.client_total_number != 0){
				struct client_info* to_delete = g_client_info_manager.first_client;
				g_client_info_manager.first_client = to_delete->next_client_info;
				g_client_info_manager.client_total_number --;
				free(to_delete);
			}
			exit(0);
		}
	}
}

int main(int argc, char** argv){
	
	if(argc < 2){
		printf("Usage: snowcast_server tcpport [file1] [file2] [file3] [...] \n");
		exit(-1);
	}

	char* server_port = argv[1];
	//one for local loop, the other for actual network stocket.
	int sockfds[2];
	struct addrinfo hints;
	struct addrinfo* servinfos[2];
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	char hostname[MAX_LENGTH];
	size_t size = MAX_LENGTH;
	gethostname(hostname,size);
	printf("host name: %s\n", hostname);
	char* names[2];
	names[0] = hostname; //Network IP Addr
	names[1] = NULL; //localhost
	
	int rv, i;
	for(i = 0; i < 2; i ++){
		if((rv = getaddrinfo(names[i], server_port, &hints, &servinfos[i])) == -1){
			printf("Error in getting %s\n", strerror(errno));
			exit(-1);
		}

		if((sockfds[i] = socket(servinfos[i]->ai_family, servinfos[i]->ai_socktype, servinfos[i]->ai_protocol)) == -1){
			close(sockfds[i]);
			printf("Error in initializing socket: %s\n", strerror(errno));
			exit(-1);
		}	
#ifdef DEBUG
		printf("Socket ID: %d\n", sockfds[i]);
#endif

		if((rv = bind(sockfds[i], servinfos[i]->ai_addr, servinfos[i]->ai_addrlen)) < 0){
			close(sockfds[i]);
			printf("Error in binding address to socket: %s\n", strerror(errno));
			exit(-1);
		}
	}
	
	//create a single thread for listening tcp mesasges...
	pthread_t listening_thread;
	pthread_t instruction_thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 20 * 1024 * 1024);
	struct listen_param* lp = (struct listen_param*)malloc(sizeof(struct listen_param));
	lp->sockfds = sockfds;
	pthread_create(&listening_thread, &attr, listening_thread_func, lp);
	pthread_create(&instruction_thread, &attr, instruction_thread_func, NULL);
	
	pthread_join(instruction_thread,0);
	pthread_join(listening_thread, 0);
	
	free(lp);
	return 0;
}
