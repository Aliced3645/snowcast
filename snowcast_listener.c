#include <unistd.h>
#include <netdb.h>
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
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_LENGTH 256
#define MAX_BUFFER_SIZE 2000
#define DEBUG

//convert binary address to readable address (just within the client_info struct)
char* convert_binary_addr_to_readable_addr(struct sockaddr binary_addr){
		char* readable_addr = (char*)malloc(MAX_LENGTH);
		memset(readable_addr, 0, MAX_LENGTH);
		inet_ntop(AF_INET, &(((struct sockaddr_in*)&binary_addr)->sin_addr),readable_addr,INET_ADDRSTRLEN); 	
		return readable_addr;
}


void* received_buffer = 0;

void* receive_thread_func(void* args){
	int sockfd = *(int*)args;
	int bytes_count;
	struct sockaddr from;
	socklen_t fromlen = sizeof(from);
#ifdef DEBUG
	//printf("socketfd : %d\n",sockfd);
#endif
	received_buffer = malloc(MAX_BUFFER_SIZE);
	memset(received_buffer,0,MAX_BUFFER_SIZE);
	while(1){
		if((bytes_count = recvfrom(sockfd,received_buffer, MAX_BUFFER_SIZE, 0, &from, &fromlen)) == -1)
			printf("Error in receiving the data : %s\n", strerror(errno));
		char* readable_addr = convert_binary_addr_to_readable_addr(from);
		//printf("Received %d bytes of data from %s\n", bytes_count, readable_addr);
		//stdout received_buffer
		write(STDOUT_FILENO, received_buffer, bytes_count);
		free(readable_addr);
		memset(received_buffer, 0, sizeof(received_buffer));
	}
}

int main(int argc, char** argv)
{
	if(argc != 2){
		printf("Usage: snowcast_listener [udpport]\n");
		exit(-1);
	}
	char* udp_port = argv[1];
	struct addrinfo hints, *addr_infos[2];
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM; // UDP datagram
	//loopup host name
	char hostname[MAX_LENGTH];
	size_t size = MAX_LENGTH;
	gethostname(hostname, size);
	//printf("Listener host name: %s\n", hostname);
	char* names[2]; // maybe from localhost, maybe from the internet...
	names[0] = hostname;//network IP address
	names[1] = NULL; //localhost loop
	int sockfds[2];

	int rv, i;
	for(i = 0; i < 2; i ++){
		if((rv = getaddrinfo(names[i], udp_port, &hints, &addr_infos[i])) == -1){
			printf("Error in getting address information : %s\n", strerror(errno));
			exit(-1);
		}
		if((sockfds[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1){
			close(sockfds[i]);
			printf("Error in initializing socket: %s\n", strerror(errno));
			exit(-1);
		}

		//bind..
		if( bind(sockfds[i], addr_infos[i]->ai_addr, addr_infos[i]->ai_addrlen) == -1){
			printf("Error in binding: %s\n", strerror(errno));
		}
	}
	
	received_buffer = malloc(MAX_BUFFER_SIZE);
	memset(received_buffer, 0, MAX_BUFFER_SIZE);
	
	//two threads to receive
	pthread_t receive_threads[2];
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 20*1024*1024);

	for(i = 0; i < 2; i ++){
		int* fd_p = &sockfds[i];
		pthread_create(&receive_threads[i],&attr, receive_thread_func, fd_p);
	}
	for(i = 0; i < 2; i ++){
		pthread_join(receive_threads[i],0);
	}

	for(i = 0; i < 2; i ++){
		freeaddrinfo(addr_infos[i]);
		close(sockfds[i]);
	}

	return 0;
}

