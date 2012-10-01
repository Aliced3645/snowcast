#include <unistd.h>
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

//#define DEBUG

#define REPLY_WELCOME 0
#define REPLY_ANNOUNCE 1
#define REPLY_INVALID_COMMAND 2
#define REQUEST_ALL_STATIONS_PLAYING 3
#define REPLY_ALL_SONGS_PLAYING 3
#define REQUEST_PLAYLIST 4
#define REPLY_PLAYLIST_HEADER 4
#define REPLY_PLAYLIST_ITEM 5
#define REPLY_STATION_SONGS_PLAYING 6
#define MAX_LENGTH 256
//define control messages here
//cancel the allignment
#pragma pack(push,1)
struct Hello{
	uint8_t commandType;
	uint16_t udpPort;
};

struct SetStation{
	uint8_t commandType;
	uint16_t stationNumber;
};

struct ReqStationPlaying{
	uint8_t commandType;
	uint16_t pack;
};

struct ReqStationPlaylist{
	uint8_t commandType;
	uint16_t pack;
};

#pragma pack(pop)

int helloed = 0;

#define DEFAULT_STATION_NUM 2
static uint8_t total_station_num = DEFAULT_STATION_NUM;

int send_hello(int sockfd, const char* clnt_udpport){
	//convert host byte order to network order
	uint16_t clnt_udpport_h = (uint16_t)atoi(clnt_udpport);
	uint16_t clnt_udpport_n = htons(clnt_udpport_h);
#ifdef DEBUG
	printf("Port: %d\n", clnt_udpport_n);	
	printf("uint8 size: %d\n",sizeof(uint8_t));
	printf("uint16 size: %d\n", sizeof(uint16_t));
	//The machine might be 32-bit alligned...
	printf("Hello struct size: %d\n", sizeof(struct Hello));
#endif
	struct Hello hl_msg = {(uint8_t)0, (uint16_t)clnt_udpport_n};
#ifdef DEBUG
	printf("%d\n", sizeof(hl_msg));
#endif
	int bytes_sent = send(sockfd,(void*)&hl_msg, sizeof(struct Hello),0);
	if(bytes_sent == -1){
		printf("An error occured when sending a HELLO message: %s\n", strerror(errno));
		return -1;
	}
	else if(bytes_sent != 3){
		printf("NOT all contents of the Hello mesasge is sent...\n");
		return 0;
	}

	return 1;
}

void* send_message_loop(void* socket){
	char input_msg[MAX_LENGTH];
	int sockfd = (int)socket;
	memset(input_msg, 0, MAX_LENGTH);
	while(1){
		memset(input_msg, 0, MAX_LENGTH);
		printf("> ");
		fgets(input_msg,MAX_LENGTH,stdin);	
		if((strlen(input_msg) > 0 ) && (input_msg[strlen(input_msg) - 1] == '\n'))
				input_msg[strlen(input_msg) - 1] = '\0';
		//decide what type of message
		if(strncmp(input_msg,"set", 3) == 0){
			//take the station number
			char station_str[MAX_LENGTH];
			memset(station_str, 0, MAX_LENGTH);
			strcpy(station_str, input_msg + 4);
			char* last = 0;
			int station_num = strtol(station_str, &last, 10);
			if((strlen(station_str) == 0) || (last[0] != '\0')){
				printf("> Ambiguous input! Do you mean 'set %d'? type [y] to confim or other keys to input again!\n> ", station_num);
				memset(input_msg, 0, MAX_LENGTH);
				fgets(input_msg, MAX_LENGTH, stdin);
				if((strlen(input_msg) > 0 ) && (input_msg[strlen(input_msg) - 1] == '\n'))
					input_msg[strlen(input_msg) - 1] = '\0';
				if( (strlen(input_msg) == 1) && input_msg[0] == 'y'){
					goto send;
				}
				else{
					printf("Please input again your instruction here!\n");
					continue;
				}
			}
send:
			//to send setstation package here..
			//Check validity
			printf("Sending setstation request...You want to listen to the station [%d] ~\n", station_num);
			station_num = htons(station_num);
			struct SetStation ss_msg = {(uint8_t)1, (uint16_t)station_num};
			int bytes_sent = send(sockfd,(void*)&ss_msg, sizeof(struct Hello),0);
			if(bytes_sent == -1){
				printf("An error occured when sending a HELLO message: %s\n", strerror(errno));
				exit(-1);
			}
			else if(bytes_sent != 3){
				printf("NOT all contents of the Hello mesasge is sent...\n");
				return 0;
			}
#ifdef DEBUG
			printf("Request sent...\n");
#endif
			
		}
		else if((strncmp(input_msg, "exit", 4) == 0) || (strncmp(input_msg, "quit", 4) == 0)){
			printf("Thanks for using Snowcast!\n");
			exit(0);
		}

		//request for each station's playing songs..
		else if(strncmp(input_msg, "s", 1) == 0){
			struct ReqStationPlaying rsp_msg = {(uint8_t)REQUEST_ALL_STATIONS_PLAYING, (uint16_t)0};	
			int bytes_sent = send(sockfd,(void*)&rsp_msg, sizeof(struct ReqStationPlaying),0);
			if(bytes_sent == -1){
				printf("An error occured when sending a REQUEST_ALL_STATIONS_PLAYING message: %s\n", strerror(errno));
				exit(-1);
			}
			else if(bytes_sent != 3){
				printf("NOT all contents of the mesasge is sent...\n");
				return 0;
			}
		}
		else if(strncmp(input_msg, "p", 1) == 0){
			struct ReqStationPlaying rsp_msg = {(uint8_t)REQUEST_PLAYLIST, (uint16_t)0};	
			int bytes_sent = send(sockfd,(void*)&rsp_msg, sizeof(struct ReqStationPlaying),0);
			if(bytes_sent == -1){
				printf("An error occured when sending a REQUEST_PLAYLIST message: %s\n", strerror(errno));
				exit(-1);
			}
			else if(bytes_sent != 3){
				printf("NOT all contents of the mesasge is sent...\n");
				return 0;
			}
		}

		else{
			printf("Cannot recognize your message! Please type again...\n");
		}
		

		memset(input_msg, 0, MAX_LENGTH);
		fflush(stdout);
	}

}

void* recv_message_loop(void* socket){
	int sockfd = (int)socket;
	int rv;
	uint8_t* msg = (uint8_t*)malloc(MAX_LENGTH);
	memset(msg,0,MAX_LENGTH);
	while(1){
		//decide which type of msg is received
		if( (rv = recv(sockfd, msg, MAX_LENGTH,0)) == -1){
			printf("Error on receiving server messages : %s\n", strerror(errno));
			exit(-1);
		}
		else if(rv == 0){
			printf("Server closed the connection!\n");
			exit(-1);
		}
		else{
			//parse the message received..
			uint8_t msg_type = msg[0];
			if(msg_type == (uint8_t)REPLY_WELCOME){
				uint16_t* num_station_p = (uint16_t*)(msg+1);
				uint16_t num_station = ntohs(*num_station_p);
				printf("\n> Welcome! There are %d music stations! Using [set <num>] instruction to set station!\n> ", num_station);
				total_station_num = num_station;
			}

			else if(msg_type == (uint8_t)REPLY_ANNOUNCE){//Announce
				uint16_t string_length = (uint16_t)msg[1];
                                //string_length = ntohs(string_length);
                                printf("String length: %d\n", string_length);
				char* command_charpart_pointer =  (char*)(((uint8_t*)msg)+2);
				char* command_string = malloc(string_length);
				memset(command_string,0,string_length);
				memcpy(command_string, command_charpart_pointer, string_length);
				printf("\n> Server Announcement: %s\n> ", command_string);
				free(command_string);
			}

			else if(msg_type == (uint8_t)REPLY_INVALID_COMMAND){
				//to analysis the command
				uint8_t string_length = msg[1];
                                //string_length = ntohs(string_length);
				char* command_charpart_pointer = (char*)(((uint8_t*)msg)+2);
				char* command_string = malloc(string_length);
				memset(command_string,0,string_length);
				memcpy(command_string, command_charpart_pointer, string_length);
				printf("\n> Invalid Command: %s\n> ", command_string);
				free(command_string);
			}
			else if(msg_type == (uint8_t)REPLY_ALL_SONGS_PLAYING){
				//receive, display...
				uint8_t index_or_total = msg[1];
				if(index_or_total == total_station_num){
					printf("Here is the playing information of all %d channels.\n> ", total_station_num);	
				}
			}
			else if(msg_type == (uint8_t)REPLY_STATION_SONGS_PLAYING){
					int index = msg[1];
					int string_length = msg[2];
					char* command_charpart_pointer = (char*)(((uint8_t*)msg)+3);
					char* command_string = malloc(string_length);
					memset(command_string, 0, string_length);
					memcpy(command_string, command_charpart_pointer, string_length);
					printf("Station %d is playing: \n\t %s\n> ", index, command_string);
					free(command_string);
			}

			else if(msg_type == (uint8_t)REPLY_PLAYLIST_HEADER){
				int total = msg[1];
				int station_num = msg[2];
				printf("There are %d songs in the station [%d]'s playlist!\n> ", total, station_num);
			}
			else if(msg_type == (uint8_t)REPLY_PLAYLIST_ITEM){
				uint8_t string_length = msg[1];
				char* command_charpart_pointer =  (char*)(((uint8_t*)msg)+2);
				char* command_string = malloc(string_length);
				memset(command_string,0,string_length);
				memcpy(command_string, command_charpart_pointer, string_length);
				printf("\t Song name: %s\n> ", command_string);
				free(command_string);
			}
			else{
				printf("Unrecoginizable message from the server\n");
			}
		}
		memset(msg,0, MAX_LENGTH);
		fflush(stdout);
	}
}
int main(int argc, char** argv){
	
	if(argc != 4){
		printf("Usage: snowcast_control servername serverport udpport\n");
		exit(-1);
	}
	//get ip addr infos..
	char* server_name = argv[1];
	char* server_port = argv[2];
	char* clnt_udpport = argv[3];
	
#ifdef DEBUG
	printf("%s\n", server_name);
#endif
	int sockfd;
	struct addrinfo hints, *servinfo;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	int rv = 0;
	if((rv = getaddrinfo(server_name,server_port,&hints, &servinfo) == -1)){
		printf("Error in getting address info: %s\n", strerror(errno));
		exit(-1);
	}
	
	if((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1){
		close(sockfd);
		printf("Error in initializing socket: %s\n", strerror(errno));
		exit(-1);
	}

	if((rv = connect(sockfd,servinfo->ai_addr,servinfo->ai_addrlen)) == -1){
		printf("%s\n",strerror(errno));
		//If "connection refused, that is there is no server started yet
		exit(-1);
	}	

	send_hello(sockfd, clnt_udpport);
	//waiting for input
	pthread_t sending_thread;
	pthread_t recving_thread;

	pthread_create(&sending_thread, NULL, send_message_loop, (void*)sockfd);
	pthread_create(&recving_thread, NULL, recv_message_loop, (void*)sockfd);
	pthread_join(sending_thread, 0);

	freeaddrinfo(servinfo);
	return 0;
}
