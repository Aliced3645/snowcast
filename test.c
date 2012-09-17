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
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>

//test the speed..
const char* song_name = "mp3/Beethoven-SymphonyNo5.mp3";

#define PACKAGE_SIZE 1500

int main(int argc, char** argv)
{
	int fd = open(song_name, O_RDONLY,NULL);
	if(fd == -1){
		printf("Error in opening song file : %s\n", strerror(errno));
		exit(-1);
	}
	void* data = malloc(PACKAGE_SIZE);
	memset(data,0, PACKAGE_SIZE);

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
	struct timeval tm;
	int package_loop_count = 0;
	int actual_bytes_sent;
	int actual_bytes_read;
	int time1, time2;
	while(1){
		//record time here
		gettimeofday(&tm,NULL);
		time1 = tm.tv_sec*1000000 + tm.tv_usec;
		while(package_loop_count != 16){
			actual_bytes_read = read(fd, data, 1024);
			printf("Actual Data length: %d\n", actual_bytes_read);
			if(actual_bytes_read == -1){
				printf("An error in reading mp3 file: %s\n", strerror(errno));
				exit(0);
			}
			if(actual_bytes_read == 0){
				exit(0);
			}
			if( (actual_bytes_sent = sendto(sockfd,data, actual_bytes_read, 0, targetinfo->ai_addr, targetinfo->ai_addrlen)) == -1)
						printf("An error occured when sending: %s\n", strerror(errno));	
			package_loop_count ++;
			printf("actual data sent: %d\n", actual_bytes_sent);

		}
		//record time here and sleep
		package_loop_count = 0;
		gettimeofday(&tm, NULL);
		time2 = tm.tv_sec*1000000 + tm.tv_usec ;
		int time_elapsed = time2 - time1;
		if(time_elapsed <= 1000000){
			printf("Sleep\n");
			usleep(1000000 - time_elapsed);
		}	
	}
	return 0;
}
