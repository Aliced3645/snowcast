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

#define HELLO 0
#define SET_STATION 1


#define DEBUG

#define MAX_LENGTH 256
#define DEFAULT_STATION_NUM 2
static uint16_t total_station_num = DEFAULT_STATION_NUM;

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
	struct client_info* station_next_client;
	struct sockaddr client_connect_addr;
	char* client_readable_addr;
	
	uint16_t client_udp_port;
	uint16_t client_station;
	uint16_t client_sockfd;
};

//manage information of station
struct station_info;
struct station_info_manager{
	uint8_t station_total_number;
	struct station_info* first_station;
	struct station_info* last_station;
};

struct station_info{
	char* song_name;
	int station_num;
	uint8_t current_progress; // decide whether to send an annnouce..
	struct station_info* next_station_info;
	uint8_t client_total_number;
	struct client_info* first_client;
	struct client_info* last_client;
};

//initialize the client info manager and station info manager..
struct client_info_manager g_client_info_manager = {0, NULL, NULL};
struct station_info_manager g_station_info_manager = {0, NULL, NULL};

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

struct station_info* get_station_info_by_num(int num){
	struct station_info* info_traverser = g_station_info_manager.first_station;
	while(info_traverser != NULL){
		if(info_traverser->station_num == num)
			return info_traverser;
		info_traverser = info_traverser->next_station_info;
	}
	return NULL;
}

//add a client info to a station
int add_client_to_station(int client_sockfd, int station_num){
	if(station_num < 0 || station_num >= total_station_num){
		printf("Failure in adding the client to the station\n");
		return -1;
	}
	struct station_info* target_station_info = get_station_info_by_num(station_num);
	struct client_info* target_client_info = get_client_info_by_socket(client_sockfd);
	if(target_station_info->client_total_number == 0){
		target_station_info->first_client = target_station_info->last_client = target_client_info;
		target_client_info->station_next_client = 0;
	}
	else{
		target_station_info->last_client->station_next_client = target_client_info;
		target_station_info->last_client = target_client_info;
		target_client_info->station_next_client = 0;
	}
	target_station_info->client_total_number ++ ;
	return 0;
}

//caution: not remove the info forever
int delete_client_from_station(int client_sockfd, int station_num){
	struct client_info* target_client_info = get_client_info_by_socket(client_sockfd);
	if(target_client_info->client_station == 65535){
			return 0;
	}
	struct station_info* target_station_info = get_station_info_by_num(station_num);

	if(target_station_info == 0 || target_client_info == 0){
		printf("Failure in deleting the client from station \n");
		return -1;
	}
	if(target_station_info->client_total_number == 0)
		return -1;
	if(target_station_info->client_total_number == 1){
		if(target_station_info->first_client->client_sockfd == client_sockfd){
			target_station_info->client_total_number = 0;
			target_station_info->first_client = target_station_info->last_client = 0;
			return client_sockfd;
		}
		else
			return -1;
	}
	
	struct client_info* pre, *next;
	pre = target_station_info->first_client;
	next = pre->station_next_client;

	if(pre->client_sockfd == client_sockfd){
		target_station_info->client_total_number --;
		target_station_info->first_client = next;
		return client_sockfd;
	}
	while(next != 0){
		if(next->client_sockfd == client_sockfd){
			if(next == target_station_info->last_client)
				target_station_info->last_client = pre;
			pre->station_next_client = next->station_next_client;
			target_station_info->client_total_number --;
			return client_sockfd;
		}
		pre = next;
		next = next->station_next_client;
	}
	return -1;
}

//delete a client info from the list.
//return -1 if failed
int delete_client_info(int client_sockfd){
	if(g_client_info_manager.client_total_number == 0)
		return -1;	
	if(g_client_info_manager.client_total_number == 1){
		if(g_client_info_manager.first_client->client_sockfd == client_sockfd){
			if(delete_client_from_station(g_client_info_manager.first_client->client_sockfd, g_client_info_manager.first_client->client_station) == -1)
				return -1;
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
		delete_client_from_station(pre->client_sockfd, pre->client_station);
		g_client_info_manager.client_total_number -- ;
		g_client_info_manager.first_client = next;
		free(pre->client_readable_addr);
		free(pre);
		return client_sockfd;
	}
	while(next != 0){
		if(next->client_sockfd == client_sockfd){
			//if it is the last one to be deleted, then update the last_client
			delete_client_from_station(next->client_sockfd, next->client_station);
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
	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	int i;
	int rv;
	printf("Server starts listening to connection...\n");
	for(i = 0; i < 2; i ++){
		FD_SET(server_sockfds[i], &fd_list);
		FD_SET(server_sockfds[i], &fd_list_temp);
		if (server_sockfds[i] >  fd_max)
			fd_max = server_sockfds[i];
			if((rv = listen(server_sockfds[i], 0))  == -1){
			close(server_sockfds[i]);
			printf("Listen Error: %s\n", strerror(errno));
			exit(-1);
		}
	}

	while(1){
		//check..whether song has been ended
		fd_list_temp = fd_list;
		if((rv = select(fd_max +1 , &fd_list_temp, NULL, NULL, &tv)) == -1 ){
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
				if((server_sockfd = find_sockfd(server_sockfds,i))!= -1){
					if((client_sockfd = accept(server_sockfd, &client_connect_addr, &addr_lenth)) == -1){
						printf("Error on accepting a client's connection : %s\n", strerror(errno));
						continue;
					}
					else{
						FD_SET(client_sockfd, &fd_list);
						if (client_sockfd > fd_max) 	
								fd_max = client_sockfd;
						//record the info
						struct client_info* new_client_info = (struct client_info*)malloc(sizeof(struct client_info));
						memset(new_client_info, 0, sizeof(struct client_info));
						new_client_info->client_connect_addr = client_connect_addr;
						convert_binary_addr_to_readable_addr(new_client_info);
						new_client_info->client_sockfd = client_sockfd;
						new_client_info->client_station = -1;
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
						//default station: 0
						if(msg_type == (uint8_t)HELLO){
							//receive a Hello
							struct client_info* target_client_info = get_client_info_by_socket(client_sockfd);
							if(target_client_info == NULL){
								printf("Error in fetching client info!\n");
								exit(-1);	
							}
							//record udp port
							uint16_t clnt_udpport_n = (uint16_t)msg.content;
							uint16_t clnt_udpport_h = ntohs(clnt_udpport_n);
							target_client_info -> client_udp_port = clnt_udpport_h;
							target_client_info -> client_station = -1;
							char* readable_addr = target_client_info->client_readable_addr;
							printf("Received a hello! Connector from: [%s:%d]\n", readable_addr,clnt_udpport_h);
							
							//respond to the hello here	
							uint16_t station_num = htons(DEFAULT_STATION_NUM);
							struct Welcome wl_msg = {(uint8_t)0, station_num};
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
							//add the client into the channel management structure
							//add_client_to_station(client_sockfd, 0);
						}
						else if(msg_type == (uint8_t)SET_STATION){
							//receive a SetStation
							uint16_t station_num = ntohs((uint16_t)msg.content);
							//get the client info to know the address and port
							struct client_info* current_client =  get_client_info_by_socket(client_sockfd);
							uint16_t former_station = current_client->client_station;
							current_client->client_station = station_num;
							uint16_t port_num = current_client->client_udp_port;
							char* readable_addr = current_client->client_readable_addr;
							printf("Received a SetStation! The client (%s:%d) turns to the station [%d]. \n", readable_addr, port_num, station_num);
							//change the station
							if(former_station != 65535){
								delete_client_from_station(client_sockfd, former_station);
							}
							add_client_to_station(client_sockfd, station_num);						
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
		if(instruction == 'c'){
			//print all connected clients ( and station info to be implemented )
			struct client_info* info_traverser = g_client_info_manager.first_client;
			while(info_traverser != NULL){
				uint16_t port = info_traverser->client_udp_port;
				uint16_t station = info_traverser->client_station;
				char* readable_addr = info_traverser->client_readable_addr;
				int sockfd = info_traverser->client_sockfd;
				printf("Client Address: [%s:%d], station: [%d], socket: [%d] \n", readable_addr, port, station, sockfd);
				info_traverser = info_traverser -> next_client_info;
			}	
		}
		else if(instruction == 'p'){
			//print all channels with all clients
			struct station_info* station_traverser = g_station_info_manager.first_station;
			while(station_traverser != NULL){
				uint8_t station_num = station_traverser->station_num;
				char* song_name = station_traverser->song_name;
				uint8_t client_total_number = station_traverser->client_total_number;
				printf("Station [%d]: Playing:[%s] | [%d] clients listening:\n", station_num, song_name, client_total_number);
				struct client_info* client_traverser = station_traverser->first_client;
				while(client_traverser != NULL){
					uint16_t port = client_traverser->client_udp_port;
					char* readable_addr = client_traverser->client_readable_addr;
					printf("\t Client Address: [%s:%d]\n", readable_addr, port);
					client_traverser = client_traverser -> station_next_client;
				}
				station_traverser = station_traverser->next_station_info;
			}
		}
		else if(instruction == 'q'){
			//release all structure objects
			while(g_client_info_manager.client_total_number != 0){
				//delete all client info
				struct client_info* to_delete = g_client_info_manager.first_client;
				g_client_info_manager.first_client = to_delete->next_client_info;
				g_client_info_manager.client_total_number --;
				free(to_delete);
			}
			while(g_station_info_manager.station_total_number != 0){
				//delete all station info
				struct station_info* to_delete = g_station_info_manager.first_station;
				g_station_info_manager.first_station = to_delete->next_station_info;
				g_station_info_manager.station_total_number --;
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
#ifdef DEBUG
	char* songnames[2] = {"Beat It", "Tuesday"};
#endif
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
		if((rv = bind(sockfds[i], servinfos[i]->ai_addr, servinfos[i]->ai_addrlen)) < 0){
			close(sockfds[i]);
			printf("Error in binding address to socket: %s\n", strerror(errno));
			exit(-1);
		}
	}
	
	//initialize the stations
	for(i = 0; i < total_station_num; i ++){
		struct station_info* current_station = (struct station_info*)malloc(sizeof(struct station_info));
		memset(current_station,0,sizeof(struct station_info));
		current_station->song_name = songnames[i];
		current_station->station_num = i;
		current_station->next_station_info = 0;
		if(g_station_info_manager.station_total_number == 0){
			g_station_info_manager.first_station = current_station;
			g_station_info_manager.last_station = current_station;		
		}
		else{
			g_station_info_manager.last_station -> next_station_info = current_station;
			g_station_info_manager.last_station = current_station;
		}
		g_station_info_manager.station_total_number ++;
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
	close(sockfds[1]);
	close(sockfds[0]);
	return 0;
}
