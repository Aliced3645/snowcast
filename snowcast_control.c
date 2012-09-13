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

#define MAX_LENGTH 256
//define control messages here
//cancel the allignment
#pragma pack(push,1)
struct Hello{
	uint8_t commandType;
	uint16_t udpPort;
};

struct SetStation{
	uint8_t commandtype;
	uint16_t stationNUmber;
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
			if(station_num < 0 || station_num >= total_station_num){
				printf("The station you request doesn't exist! \n");
				continue;
			}
			printf("Sending setstation request...You want to listen to the station [%d] ~\n", station_num);
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
	while(1){
		uint8_t* msg = (uint8_t*)malloc(MAX_LENGTH);
		memset(msg,0,MAX_LENGTH);
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
			if(msg_type == (uint8_t)0){
				uint16_t* num_station_p = (uint16_t*)(msg + 1);
				printf("\n> Welcome! There are %d music stations! Using set <num> to set station!\n> ", *num_station_p);
			}
			else if(msg_type == (uint8_t)1){
			}
			else if(msg_type == (uint8_t)2){
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
	pthread_join(recving_thread, 0);
	return 0;
}
