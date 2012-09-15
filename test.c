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
#include <signal.h>

int main(int argc, char** argv)
{
	void* data = malloc(512);
	memset(data,8,512);

	int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int rv;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	struct addrinfo* targetinfo;

	if((rv = getaddrinfo("127.0.0.1", "8082", &hints, &targetinfo)) == -1){
			printf("Error in getting %s\n", strerror(errno));
			exit(-1);
	}

	int actual_bytes;
	while(1){
		if( (actual_bytes = sendto(sockfd,data, sizeof(data), 0, targetinfo->ai_addr, targetinfo->ai_addrlen)) == -1)
					printf("An error occured when sending: %s\n", strerror(errno));	
		printf("send");
	}
	return 0;
}
