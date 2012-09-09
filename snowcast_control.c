#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <inttypes.h>


int main(int argc, char** argv){
	
	if(argc != 4){
		printf("Usage: snowcast_control servername serverport udpport\n");
		exit(-1);
	}
	//get ip addr infos..
	char* server_name = argv[1];
	char* server_port = argv[2];
	char* clnt_udpport = argv[3];

	int sockfd;
	struct addrinfo hints, *servinfo;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	int rv = 0;
	if((rv = getaddrinfo(server_name,server_port,&hints, &servinfo) != -1)){
		printf("%s\n", strerror(errno));
		exit(-1);
	}
	if((sockfd = socket(servinfo->ai_family, servinfo->ai_addrlen, servinfo->ai_protocol)) == -1){
		printf("%s\n", strerror(errno));
		exit(-1);
	}
	if((rv = connect(sockfd,servinfo->ai_addr,servinfo->ai_addrlen)) == -1){
		printf("%s\n",strerror(errno));
		//If "connection refused, that is there is no server started yet
		exit(-1);
	}
	printf("Connected to the server! Please Input your command.\n");

	
	return 0;
}
