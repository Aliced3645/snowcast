#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <inttypes.h>

#define DEBUG



#define MAX_LENGTH 256
//define control messages here
struct Hello{
	uint8_t commandType;
	uint16_t udpPort;
};
int helloed = 0;

struct SetStation{
		uint8_t commanType;
		uint16_t stationNUmber;
};


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
		printf("Error in get address info: %s\n", strerror(errno));
		exit(-1);
	}
	if((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1){
		printf("Error in initializing socket: %s\n", strerror(errno));
		exit(-1);
	}
/*
	if((rv = connect(sockfd,servinfo->ai_addr,servinfo->ai_addrlen)) == -1){
		printf("%s\n",strerror(errno));
		//If "connection refused, that is there is no server started yet
		exit(-1);
	}
*/	
	//convert host byte order to network order
	uint16_t clnt_udpport_h = (uint16_t)atoi(clnt_udpport);
	uint16_t clnt_udpport_n = htons(clnt_udpport_h);
#ifdef DEBUG
	printf("Port: %d\n", clnt_udpport_n);
#endif
	struct Hello hlMsg;

	//waiting for input
	printf("Connected to the server! Please Input your set station command.\n \
			Usage: set <station number>\n");

	char input_msg[MAX_LENGTH];
	memset(input_msg, 0, MAX_LENGTH);
	while(1){
		scanf("%s", input_msg);
		//decide what type of message
		if(strncmp(input_msg,"set", 3) == 0){
			printf("hi\n");
		}
		else if((strncmp(input_msg, "exit", 4) == 0) || (strncmp(input_msg, "quit", 4) == 0))
				exit(0);
		else
			printf("Cannot recognize your message! Please type again...\n");
		
		memset(input_msg, 0, MAX_LENGTH);
	}

	return 0;
}
