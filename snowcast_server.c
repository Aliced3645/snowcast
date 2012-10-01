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
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/time.h>

#define HELLO 0
#define WELCOME 0
#define ANNOUNCE 1
#define SET_STATION 1
#define INVALID_COMMAND 2

#define REQUEST_ALL_STATIONS_PLAYING 3
#define REPLY_ALL_SONGS_PLAYING 3
#define REPLY_STATION_SONGS_PLAYING 6
#define REQUEST_PLAYLIST 4
#define REPLY_PLAYLIST_HEADER 4
#define REPLY_PLAYLIST_ITEM 5

#define DEBUG

#define MAX_LENGTH 256
#define DEFAULT_STATION_NUM 2
#define PACKAGE_SIZE 1500

static int largest_station_num = 0;
fd_set fd_list, fd_list_temp;
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

struct AllStationPlayingHeader{
	uint8_t replyType;
	uint8_t numString;
};

struct OneStationPlaying{
	uint8_t replyType;
	uint8_t index;
	uint8_t replyStringSize;
	char* replyString;
};

struct PlayListHeader{
	uint8_t replyType;
	uint8_t songsCount;
	uint8_t stationNum;
};

struct PlayListItem{
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

	//mutex
	pthread_mutex_t clients_mutex;
};

struct client_info{
	struct client_info* next_client_info;
	struct client_info* station_next_client;
	struct sockaddr client_connect_addr;
	char* client_readable_addr;
	
	uint16_t client_udp_port;
	uint16_t client_station;
	uint16_t client_sockfd;
	int client_helloed;
	int client_announced;
};

//manage songs for each station
struct song_info;

struct song_info_manager{
	uint8_t song_total_number;
	struct song_info* first_song;
	struct song_info* last_song;
};

struct song_info{
	char* song_name;
	struct song_info* next_song_info;
};

//manage information of station
struct station_info;
struct station_info_manager{
	uint8_t station_total_number;
	struct station_info* first_station;
	struct station_info* last_station;
	pthread_mutex_t stations_mutex;
};

struct station_info{
	char* songs_dir_name;
	DIR* songs_dir;
	struct song_info* current_song_info;
	struct song_info_manager station_song_manager;
	int station_num;
	uint8_t current_progress; // decide whether to send an annnouce..
	struct station_info* next_station_info;
	uint8_t client_total_number;
	struct client_info* first_client;
	struct client_info* last_client;
	//mutex
	pthread_mutex_t station_mutex;
	pthread_t sending_thread;
};



//initialize the client info manager and station info manager..
struct client_info_manager g_client_info_manager = {0, NULL, NULL, PTHREAD_MUTEX_INITIALIZER};
struct station_info_manager g_station_info_manager = {0, NULL, NULL, PTHREAD_MUTEX_INITIALIZER};
pthread_mutex_t station_num_lock = PTHREAD_MUTEX_INITIALIZER;

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

struct station_info* get_station_info_by_client(struct client_info* current_client){
	int station_num = current_client->client_station;
	return get_station_info_by_num(station_num);
}

//add a client info to a station
int add_client_to_station(int client_sockfd, int station_num){
	if(station_num < 0 || station_num > largest_station_num){
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
			FD_CLR(g_client_info_manager.first_client->client_sockfd, &fd_list);
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
		FD_CLR(pre->client_sockfd, &fd_list);
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
			FD_CLR(pre->client_sockfd, &fd_list);
			free(next);
			return client_sockfd;
		}
		next = next->next_client_info;
		pre = pre->next_client_info;	
	}

	return -1;
}

void garbage_collection(){
		while(g_station_info_manager.first_station != NULL){
		struct station_info* to_delete = g_station_info_manager.first_station;
		pthread_cancel(to_delete->sending_thread);
		while(to_delete->station_song_manager.first_song != NULL){
			struct song_info* to_delete_song = to_delete->station_song_manager.first_song;
			free(to_delete_song->song_name);
			to_delete->station_song_manager.first_song = to_delete_song->next_song_info;
			free(to_delete_song);
		}
		g_station_info_manager.first_station = to_delete->next_station_info;
		free(to_delete);
	}

	//free all clients 
	while(g_client_info_manager.first_client != NULL){
		struct client_info* to_delete = g_client_info_manager.first_client;
		g_client_info_manager.first_client = to_delete->next_client_info;
		free(to_delete);
	}
}
void print_all_station(){
	struct station_info* station_traverser = g_station_info_manager.first_station;
		while(station_traverser != NULL){
			printf("Station %d has %d songs in the playlist.\n",station_traverser->station_num, station_traverser->station_song_manager.song_total_number);
				struct song_info* song_traverser = station_traverser->station_song_manager.first_song;
				while(song_traverser!= NULL){
					printf("\tSong name: %s\n", song_traverser->song_name);
					song_traverser = song_traverser -> next_song_info;
				}
				station_traverser = station_traverser->next_station_info;
			}
}

int send_announce_command(int client_sockfd, const char* songname);
int delete_station(int station_num){
	//send announce to all clients..
	pthread_mutex_lock(&g_client_info_manager.clients_mutex);
	struct client_info* client_traverser = g_client_info_manager.first_client;
	struct station_info* current_station = get_station_info_by_num(station_num);
	pthread_mutex_unlock(&current_station -> station_mutex);
	pthread_cancel(current_station -> sending_thread);
	char msg[MAX_LENGTH];
	while(client_traverser != NULL){
		memset(msg,0,MAX_LENGTH);
		sprintf(msg, "Remove station %d!", station_num);
		send_announce_command(client_traverser->client_sockfd, msg);
		client_traverser = client_traverser -> next_client_info;
	}
	if(current_station == NULL){
		printf("The station does not exist!\n");
		return -1;
	}

	client_traverser = g_client_info_manager.first_client;
	while(client_traverser != NULL){
		if(client_traverser->client_station == station_num){
			memset(msg,0,MAX_LENGTH);
			usleep(100);
			sprintf(msg, "The station you are listening will be deleted, please choose another staion!");
			send_announce_command(client_traverser->client_sockfd, msg);
			client_traverser -> client_station = -1;
			client_traverser -> station_next_client = NULL;
		}
		client_traverser = client_traverser -> next_client_info;
	}
	pthread_mutex_unlock(&g_client_info_manager.clients_mutex);
	//to delete the station
	usleep(100);
	pthread_mutex_lock(&g_station_info_manager.stations_mutex);
	if(g_station_info_manager.station_total_number == 0){
			pthread_mutex_unlock(&g_station_info_manager.stations_mutex);
			printf("0 Stations!\n");
			return -1;	
	}
	if(g_station_info_manager.station_total_number == 1){
		if(g_station_info_manager.first_station->station_num == station_num){
			pthread_cancel(g_station_info_manager.first_station -> sending_thread);
			struct station_info* to_delete = g_station_info_manager.first_station;
			while(to_delete->station_song_manager.first_song != NULL){
				struct song_info* to_delete_song = to_delete->station_song_manager.first_song;
				free(to_delete_song->song_name);
				to_delete->station_song_manager.first_song = to_delete_song->next_song_info;
				free(to_delete_song);
			}
			closedir(g_station_info_manager.first_station->songs_dir);
			free(g_station_info_manager.first_station);
			g_station_info_manager.station_total_number = 0; 
			g_station_info_manager.first_station = g_station_info_manager.last_station = 0;
			pthread_mutex_unlock(&g_station_info_manager.stations_mutex);
			return station_num;
		}
		else{
			pthread_mutex_unlock(&g_station_info_manager.stations_mutex);
			printf("Didn't find the station!\n");
			return -1;
		}
	}
	struct station_info* pre,*next;
	pre = g_station_info_manager.first_station;
	next = pre->next_station_info;
	if(pre->station_num == station_num){
		pthread_cancel(pre->sending_thread);
		struct station_info* to_delete = pre;
		while(to_delete->station_song_manager.first_song != NULL){
			struct song_info* to_delete_song = to_delete->station_song_manager.first_song;
			free(to_delete_song->song_name);
			to_delete->station_song_manager.first_song = to_delete_song->next_song_info;
			free(to_delete_song);
		}
		g_station_info_manager.station_total_number -- ;
		g_station_info_manager.first_station = next;
		closedir(pre->songs_dir);
		free(pre);
		pthread_mutex_unlock(&g_station_info_manager.stations_mutex);
		return station_num;
	}
	while(next != 0){
		if(next->station_num == station_num){
			//if it is the last one to be deleted, then update the last_client
			pthread_cancel(next->sending_thread);
			struct station_info* to_delete = next;
			while(to_delete->station_song_manager.first_song != NULL){
				struct song_info* to_delete_song = to_delete->station_song_manager.first_song;
				free(to_delete_song->song_name);
				to_delete->station_song_manager.first_song = to_delete_song->next_song_info;
				free(to_delete_song);
			}
			if(next == g_station_info_manager.last_station)
				g_station_info_manager.last_station = pre;
			pre->next_station_info = next->next_station_info;
			g_station_info_manager.station_total_number --;
			next->first_client = next->last_client = 0;
			next->next_station_info = 0;
			closedir(next->songs_dir);
			free(next);
			pthread_mutex_unlock(&g_station_info_manager.stations_mutex);
			return station_num;
		}
		next = next->next_station_info;
		pre = pre->next_station_info;	
	}
	pthread_mutex_unlock(&g_station_info_manager.stations_mutex);
	printf("Didn't find the station!\n");
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


//a quick function to send invalid command
void send_invalid_command(int client_sockfd, const char* command_string){
	int string_length = strlen(command_string);
	int command_size = 2 * sizeof(uint8_t) + string_length;
	struct InvalidCommand *invalid_command = malloc(command_size);
	memset(invalid_command, 0, command_size);
	//format the message
	uint8_t* command_intpart_pointer = (uint8_t*)invalid_command;
	command_intpart_pointer[0] = (uint8_t)INVALID_COMMAND;
	command_intpart_pointer[1] = (uint8_t)string_length;
	char* command_charpart_pointer = (char*)(((uint8_t*)invalid_command) + 2);
	memcpy(command_charpart_pointer, command_string, string_length);
	int bytes_sent = send(client_sockfd,(void*)invalid_command, command_size, 0);
	if(bytes_sent == -1){
		printf("An Error occured when sending a INVALID_COMMAND message: %s\n", strerror(errno));
	}
	else if(bytes_sent == 0){
		printf("A connection ended..\n");
		close(client_sockfd);
		FD_CLR(client_sockfd, &fd_list);
		delete_client_info(client_sockfd);
	}
	else if(bytes_sent != command_size){
		printf("Not all parts of the invalid command mesage are sent!");
	}
	free(invalid_command);
}

int send_announce_command(int client_sockfd, const char* songname){
	int string_length = strlen(songname);
	int command_size = 2 * sizeof(uint8_t) + string_length + 1;
	struct Announce* announce_command = malloc(command_size);
	memset(announce_command,0,command_size);
	uint8_t* command_intpart_pointer = (uint8_t*)announce_command;
	command_intpart_pointer[0] = (uint8_t)ANNOUNCE;
	command_intpart_pointer[1] = (uint8_t)string_length + 1;
	char* command_charpart_pointer = (char*)(((uint8_t*)announce_command) + 2);
	memcpy(command_charpart_pointer, songname, string_length + 1);
	command_charpart_pointer[string_length] = '\0';
	int bytes_sent = send(client_sockfd,(void*)announce_command, command_size, 0);
	if(bytes_sent == -1){
		printf("An Error occured when sending a ANNOUNCE message: %s\n", strerror(errno));
	}
	else if(bytes_sent == 0){
		printf("A connection ended..\n");
		close(client_sockfd);
		FD_CLR(client_sockfd, &fd_list);
		delete_client_info(client_sockfd);
	}
	else if(bytes_sent != command_size){
		printf("Not all parts of the invalid command mesage are sent!");
	}
	free(announce_command);
	if(bytes_sent == command_size)
		bytes_sent = 1;
	return bytes_sent;
}

int send_one_playing_song(int client_sockfd, int station_num){
	struct station_info* current_station = get_station_info_by_num(station_num);
	if(current_station == NULL)
		return -1;
	char* songname = current_station->current_song_info->song_name;
	int string_length = strlen(songname);
	int command_size = 3 * sizeof(uint8_t) + string_length+1;
	struct OneStationPlaying* one_station_playing = malloc(command_size);
	uint8_t* command_intpart_pointer = (uint8_t*)one_station_playing;
	command_intpart_pointer[0] = (uint8_t)REPLY_STATION_SONGS_PLAYING;
	command_intpart_pointer[1] = (uint8_t)station_num;
	command_intpart_pointer[2] = (uint8_t)string_length + 1;
	char* command_charpart_pointer = (char*)(((uint8_t*)one_station_playing) + 3);
	memcpy(command_charpart_pointer, songname, string_length);
	command_charpart_pointer[string_length] = '\0';
	int bytes_sent = send(client_sockfd, (void*)one_station_playing, command_size,0);
	if(bytes_sent == -1){
		printf("An Error occured when sending a OneStationPlaying  message: %s\n", strerror(errno));
	}
	else if(bytes_sent == 0){
		printf("A connection ended..\n");
		close(client_sockfd);
		FD_CLR(client_sockfd, &fd_list);
		delete_client_info(client_sockfd);
	}
	else if(bytes_sent != command_size){
		printf("Not all parts of the invalid command mesage are sent!");
	}
	free(one_station_playing);
	if(bytes_sent == command_size)
		bytes_sent = 1;
	return bytes_sent;
}

int send_playing_songs(int client_sockfd){
	pthread_mutex_lock(&g_station_info_manager.stations_mutex);
	struct AllStationPlayingHeader* header = malloc(sizeof(struct AllStationPlayingHeader));
	memset(header, 0, sizeof(struct AllStationPlayingHeader));
	header->replyType = REPLY_ALL_SONGS_PLAYING;
	header->numString = g_station_info_manager.station_total_number;
	int bytes_sent = send(client_sockfd, (void*)header, sizeof(struct AllStationPlayingHeader), 0 );
	if(bytes_sent == -1){
		printf("An Error occured when sending a REPLY_ALL_SONGS_PLAYING message: %s\n", strerror(errno));
	}
	else if(bytes_sent == 0){
		printf("A connection ended..\n");
		close(client_sockfd);
		FD_CLR(client_sockfd, &fd_list);
		delete_client_info(client_sockfd);
	}
	else if(bytes_sent != sizeof(struct AllStationPlayingHeader)){
		printf("Not all parts of the invalid command mesage are sent!");
	}
	free(header);
	struct station_info* traverser = g_station_info_manager.first_station;
	while(traverser != NULL){
		usleep(100);
		send_one_playing_song(client_sockfd, traverser->station_num);
		traverser = traverser -> next_station_info;
	}
	bytes_sent = 1;
	pthread_mutex_unlock(&g_station_info_manager.stations_mutex);
	return bytes_sent;
}

int send_playlist_item(int client_sockfd, struct song_info* song){
	char* songname = song->song_name;
	int string_length = strlen(songname);
	int command_size = 2 * sizeof(uint8_t) + string_length + 1;
	struct PlayListItem* item = (struct PlayListItem*)malloc(command_size);
	uint8_t* command_intpart_pointer = (uint8_t*)item;
	command_intpart_pointer[0] = (uint8_t)REPLY_PLAYLIST_ITEM;
	command_intpart_pointer[1] = (uint8_t)string_length + 1;
	char* command_charpart_pointer = (char*)(((uint8_t*)item) + 2);
	memcpy(command_charpart_pointer, songname, string_length);
	command_charpart_pointer[string_length] = '\0';
	int bytes_sent = send(client_sockfd, (void*)item, command_size,0);
	if(bytes_sent == -1){
		printf("An Error occured when sending a PlayListItem  message: %s\n", strerror(errno));
	}
	else if(bytes_sent == 0){
		printf("A connection ended..\n");
		close(client_sockfd);
		FD_CLR(client_sockfd, &fd_list);
		delete_client_info(client_sockfd);
	}
	else if(bytes_sent != command_size){
		printf("Not all parts of the invalid command mesage are sent!");
	}
	free(item);
	if(bytes_sent == command_size)
		bytes_sent = 1;
	return bytes_sent;
}

int send_playlist(int client_sockfd){
	struct client_info* current_client = get_client_info_by_socket(client_sockfd);
	struct PlayListHeader *header = malloc(sizeof(struct PlayListHeader));
	memset(header, 0, sizeof(struct PlayListHeader));
	header->replyType = REPLY_PLAYLIST_HEADER;
	int station_num = current_client->client_station;
	if(station_num == 65535){
		send_announce_command(client_sockfd, "You are not in any station.");
		return -1;
	}
	struct station_info* station = get_station_info_by_num(station_num);
	header->songsCount = station->station_song_manager.song_total_number;
	header->stationNum = station_num;
	int bytes_sent = send(client_sockfd, (void*)header, sizeof(struct PlayListHeader), 0 );
	if(bytes_sent == -1){
		printf("An Error occured when sending a REPLY_PLAYLIST_HEADER message: %s\n", strerror(errno));
	}
	else if(bytes_sent == 0){
		printf("A connection ended..\n");
		close(client_sockfd);
		FD_CLR(client_sockfd, &fd_list);
		delete_client_info(client_sockfd);
	}
	else if(bytes_sent != sizeof(struct PlayListHeader)){
		printf("Not all parts of the invalid command mesage are sent!");
	}
	free(header);
	
	struct song_info* traverser = station->station_song_manager.first_song;
	while(traverser != NULL){
		usleep(100);
		send_playlist_item(client_sockfd, traverser);
		traverser = traverser->next_song_info;	
	}
	bytes_sent = 1;
	return bytes_sent;
}

//this thread may use select() to deal with connections.
void* listening_thread_func(void* args){
	//struct listen_param* lp = (struct listen_param*)args;
	int *server_sockfds = (int*)args;
	
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
						pthread_mutex_lock(&g_client_info_manager.clients_mutex);
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
						pthread_mutex_unlock(&g_client_info_manager.clients_mutex);
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

							if(target_client_info->client_helloed == 1){
								printf("Received more than one HELLO from [%s:%d]\n", target_client_info->client_readable_addr, target_client_info->client_udp_port);
								send_invalid_command(client_sockfd, "More than one HELLO was sent!");
								continue;
							}

							//record udp port
							uint16_t clnt_udpport_n = (uint16_t)msg.content;
							uint16_t clnt_udpport_h = ntohs(clnt_udpport_n);
							target_client_info -> client_udp_port = clnt_udpport_h;
							target_client_info -> client_station = -1;
							target_client_info -> client_helloed = 1;
							target_client_info -> client_announced = 1;
							char* readable_addr = target_client_info->client_readable_addr;
							printf("Received a hello! Connector from: [%s:%d]\n", readable_addr,clnt_udpport_h);
							
							//respond to the hello here	
							uint16_t station_num = htons(g_station_info_manager.station_total_number);
							struct Welcome wl_msg = {(uint8_t)0, station_num};
							int bytes_sent = send(client_sockfd,(void*)&wl_msg, sizeof(struct Welcome), 0);
							if(bytes_sent == -1){
								printf("An error occured when sending a WELCOME message: %s\n", strerror(errno));
							}
							else if(bytes_sent != 3){
								printf("NOT all contents of the Welcome message were sent..\n");
							}
							else if(bytes_sent == 0){
								pthread_mutex_lock(&g_client_info_manager.clients_mutex);
								printf("A connection ended..\n");
								close(client_sockfd);
								FD_CLR(client_sockfd, &fd_list);
								delete_client_info(client_sockfd);
								pthread_mutex_unlock(&g_client_info_manager.clients_mutex);
								continue;
							}
						}
						else if(msg_type == (uint8_t)SET_STATION){
							//receive a SetStation
							uint16_t station_num = ntohs((uint16_t)msg.content);
							struct client_info* current_client =  get_client_info_by_socket(client_sockfd);

							//see whether the setstation is set before the previous announce being received
							if(current_client->client_announced == 0){
								printf("A SETSTAION was sent before the previoud announce being received from: [%s:%d]\n",current_client->client_readable_addr, current_client->client_udp_port);
								send_invalid_command(client_sockfd, "The SETSTATION was sent before receiving the former ANNOUNCE!");
								continue;
							}
							//decide if the station num is valid.
							if(current_client->client_helloed == 0){
								printf("Receive a SETSTATION request prior to HELLO from [%s:%d]\n", current_client->client_readable_addr,current_client->client_udp_port);
								send_invalid_command(client_sockfd, "Hello command must be sent first!");
								continue;
							}
							if(station_num > largest_station_num){
								printf("Received a invalid SETSTATION request from [%s:%d]\n", current_client->client_readable_addr, current_client->client_udp_port);
								char invalid_command_string[MAX_LENGTH];
								memset(invalid_command_string, 0, MAX_LENGTH);
								sprintf(invalid_command_string,"Station %d does not exist", station_num);
								send_invalid_command(client_sockfd,invalid_command_string);
								continue;
							}
							
							struct station_info* to_lock = get_station_info_by_num(station_num);
							if(to_lock == NULL){
								printf("Received a invalid SETSTATION request from [%s:%d]\n", current_client->client_readable_addr, current_client->client_udp_port);
								char invalid_command_string[MAX_LENGTH];
								memset(invalid_command_string, 0, MAX_LENGTH);
								sprintf(invalid_command_string,"Station %d does not exist", station_num);
								send_invalid_command(client_sockfd,invalid_command_string);
								continue;
							}

							current_client->client_announced = 0;	
							uint16_t former_station = current_client->client_station;
							current_client->client_station = station_num;
							uint16_t port_num = current_client->client_udp_port;
							char* readable_addr = current_client->client_readable_addr;
							printf("Received a SetStation! The client [%s:%d] turns to the station [%d]. \n", readable_addr, port_num, station_num);
							//change the station
							if(former_station != 65535){
								struct station_info* to_lock = get_station_info_by_num(former_station);
								pthread_mutex_lock(&to_lock->station_mutex);
								delete_client_from_station(client_sockfd, former_station);
								pthread_mutex_unlock(&to_lock->station_mutex);
							}
														pthread_mutex_lock(&to_lock->station_mutex);
							add_client_to_station(client_sockfd, station_num);
							pthread_mutex_unlock(&to_lock->station_mutex);			
							//send an announce
							char announce_command_string[MAX_LENGTH];
							sprintf(announce_command_string,"Successfully changed to station [%d]", station_num);
							int result = send_announce_command(client_sockfd, announce_command_string);
							if(result == 1)
								current_client->client_announced = 1;
						}
						else if(msg_type == (uint8_t)REQUEST_ALL_STATIONS_PLAYING){
							printf("A Client is requesting all music being played.\n");
							send_playing_songs(client_sockfd);
						}
						else if(msg_type == (uint8_t)REQUEST_PLAYLIST){
							printf("A Client is requesting playlist under his channel.\n");
							if(send_playlist(client_sockfd) == -1){
								continue;
							}
						}
						else{//unknown message
							struct client_info* current_client = get_client_info_by_socket(client_sockfd);
							printf("Received an unknown command from [%s:%d]\n",current_client->client_readable_addr, current_client->client_udp_port);

						}
					}
					else if(msg_length == 0){
						pthread_mutex_lock(&g_client_info_manager.clients_mutex);					
						close(client_sockfd);
						FD_CLR(client_sockfd, &fd_list);
						//delete a client_info..
						if(delete_client_info(client_sockfd) == -1){
							printf("An error occured when updating the client information list..\n");
							exit(-1);
						}	
						printf("A client [socket num: %d] has disconnected to the server..\n", client_sockfd);
						pthread_mutex_unlock(&g_client_info_manager.clients_mutex);
					}
					else{
						printf("Unrecognizable Message!\n");
						exit(-1);
					}
				}
			}
		}
	}
}

//make full path..
char* make_full_path(const char* dir, const char* song_name){
	int length = strlen(dir) + strlen(song_name) + 2;
	char* full_path_str = (char*)malloc(length);
	memset(full_path_str, 0 , length);
	strcpy(full_path_str, dir);
	full_path_str[strlen(dir)] = '/';
	strcpy(&full_path_str[strlen(dir)+1], song_name);
	full_path_str[length] = '\0';
	return full_path_str;

}

void* sending_thread_func(void* args){
	int* station_num = ((int*)args);
	struct station_info* current_station = get_station_info_by_num(*station_num);
	if(current_station == NULL){
		printf("Unable to find station :.\n");
		exit(-1);
	}
	printf("Station %d starts to send songs!\n", *station_num);
	const char* dir = current_station->songs_dir_name;
	const char* song_name = current_station->current_song_info->song_name;
	int length = strlen(current_station->songs_dir_name) + strlen(current_station->current_song_info->song_name) + 2;
	char* full_path = (char*)malloc(MAX_LENGTH);
	memset(full_path, 0 , MAX_LENGTH);
	strcpy(full_path, dir);
	full_path[strlen(dir)] = '/';
	strcpy(&full_path[strlen(dir)+1], song_name);
	full_path[length] = '\0';
	int fd = open(full_path, O_RDONLY, NULL);
	if(fd == -1){
		printf("Error in opening song file in %d station: %s\n", *station_num, strerror(errno));
		exit(-1);
	}
	free(full_path);
	pthread_mutex_unlock(&station_num_lock);

	char data[PACKAGE_SIZE];
	//void* data = malloc(PACKAGE_SIZE);
	memset(data, 0, PACKAGE_SIZE);
	//just for testing udp
	int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	char port_str[10];
	memset(port_str,0,10);
	struct timeval tm;
	int package_loop_count = 0;
	int actual_bytes_sent, actual_bytes_read;
	int time1, time2;
	while(1){
		//to be improved here
		struct client_info* info_traverser = current_station->first_client;
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		struct addrinfo* client_addrinfo;
		int nbytes, rv;
		gettimeofday(&tm,NULL);
		time1 = tm.tv_sec*1000000 + tm.tv_usec;
		while( package_loop_count != 16){
			actual_bytes_read = read(fd, data, 1024); //even no connections, still read!
			if(actual_bytes_read == -1){
				printf("An error in reading mp3 file: %s\n", strerror(errno));
				exit(-1);
			}
			else if(actual_bytes_read == 0){
				//Song ended. Announce here...
				close(fd);
				pthread_mutex_lock(&current_station->station_mutex);
				//switch to next song
				if(current_station->current_song_info == current_station->station_song_manager.last_song)
					current_station->current_song_info = current_station->station_song_manager.first_song;
				else
					current_station->current_song_info = current_station->current_song_info->next_song_info;
				printf("song name: %s\n", current_station->current_song_info->song_name);
				
				const char* dir = current_station->songs_dir_name;
				const char* song_name = current_station->current_song_info->song_name;
				int length = strlen(current_station->songs_dir_name) + strlen(current_station->current_song_info->song_name) + 2;
				memset(full_path, 0 , MAX_LENGTH);
				strcpy(full_path, dir);
				full_path[strlen(dir)] = '/';
				strcpy(&full_path[strlen(dir)+1], song_name);
				full_path[length] = '\0';
				fd = open(full_path,O_RDONLY, NULL);
				info_traverser = current_station->first_client;
				while(info_traverser != NULL){
					send_announce_command(info_traverser->client_sockfd, "The current song ended!");
					info_traverser = info_traverser->station_next_client;		
				}
				pthread_mutex_unlock(&current_station->station_mutex);
				package_loop_count = 16;
	 			continue;
			}
			pthread_mutex_lock(&current_station->station_mutex);
			info_traverser = current_station->first_client;
			while(info_traverser != NULL){					
					if(info_traverser -> client_helloed == 1){
					nbytes = sprintf(port_str, "%d", info_traverser->client_udp_port);
					port_str[sizeof(port_str)] = '\0';
					if((rv = getaddrinfo(info_traverser->client_readable_addr, port_str, &hints, &client_addrinfo)) == -1){
						printf("Error in getting addressinfo: %s...", strerror(errno));
						exit(-1);
					}
					if((actual_bytes_sent = sendto(sockfd,data,actual_bytes_read, 0, client_addrinfo->ai_addr, client_addrinfo->ai_addrlen)) == -1){
							printf("An error occured when sending: %s.\n  %s:%s\n", strerror(errno), info_traverser->client_readable_addr, port_str);
					}
					free(client_addrinfo);
					info_traverser = info_traverser->station_next_client;
					memset(port_str,0,10);
				}
			}
			pthread_mutex_unlock(&current_station->station_mutex);
			package_loop_count ++ ;
			//usleep(50000);
		}
	
		package_loop_count = 0;
		gettimeofday(&tm, NULL);
		time2 = tm.tv_sec*1000000 + tm.tv_usec ;
		int time_elapsed = time2 - time1;
		if(time_elapsed <= 1000000){
			usleep(1000000 - time_elapsed);
		}
		else{
			printf("The transmission rate cannot be ensured!\n");
		}
	}
	
	return NULL;
}

//this thread for simple user-interation.
void* instruction_thread_func(void* param){
	char *input_msg = malloc(MAX_LENGTH);
	memset(input_msg, 0, MAX_LENGTH);
	while(1){
		memset(input_msg, 0, MAX_LENGTH);
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
				char* song_name = station_traverser->current_song_info->song_name;
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
			free(input_msg);
			return NULL;
		}
		//print station's playlist
		else if(instruction == 's'){
			struct station_info* station_traverser = g_station_info_manager.first_station;
			while(station_traverser != NULL){
				printf("Station %d has %d songs in the playlist.\n",station_traverser->station_num, station_traverser->station_song_manager.song_total_number);
				struct song_info* song_traverser = station_traverser->station_song_manager.first_song;
				while(song_traverser!= NULL){
					printf("\tSong name: %s\n", song_traverser->song_name);
					song_traverser = song_traverser -> next_song_info;
				}
				station_traverser = station_traverser->next_station_info;
			}
		}
		//add station
		else if(instruction == 'a'){
			//get the added dir name
			int station_num = largest_station_num + 1;
			pthread_mutex_lock(&g_station_info_manager.stations_mutex);
			//char* dir = malloc(MAX_LENGTH);
			char dir[MAX_LENGTH];
			strcpy(dir, input_msg + 2);
			printf("Wants to create a new station, using folder %s\n", dir);
			struct station_info* current_station = (struct station_info*)malloc(sizeof(struct station_info));
			memset(current_station,0,sizeof(struct station_info));
			current_station->station_song_manager.song_total_number = 0;
			//initialize songs..
			current_station->songs_dir = opendir(dir);
			if(current_station->songs_dir == NULL){
				printf("Station %d has failed to load songs:%s\n", station_num, strerror(errno));
				exit(0);
			}
			struct dirent* song_dirp = readdir(current_station->songs_dir);
			if(song_dirp == NULL){
				printf("The songs directory for station %d has no song!\n", station_num);
				exit(-1);
			}
			while(song_dirp != NULL){
				if(strcmp(song_dirp->d_name,".") != 0 && strcmp(song_dirp->d_name,"..") != 0){
					struct song_info* new_song_info = malloc(sizeof(struct song_info));
					memset(new_song_info,0,sizeof(struct song_info));
					new_song_info->song_name = (char*)malloc(MAX_LENGTH);
					memset(new_song_info->song_name,0, MAX_LENGTH);
					strcpy(new_song_info->song_name, song_dirp->d_name);
					if(current_station->station_song_manager.song_total_number == 0){
						current_station->station_song_manager.first_song = new_song_info;
						current_station->station_song_manager.last_song = new_song_info;
					}
					else{
						current_station->station_song_manager.last_song->next_song_info = new_song_info;
						current_station->station_song_manager.last_song = new_song_info;
					}
					current_station->station_song_manager.song_total_number ++;
				}
				song_dirp = readdir(current_station->songs_dir);
			}
			current_station->songs_dir_name = dir;
			current_station->current_song_info = current_station->station_song_manager.first_song;
			current_station->station_num = station_num;
			current_station->next_station_info = 0;
			pthread_mutex_init(&current_station->station_mutex, NULL);
			if(g_station_info_manager.station_total_number == 0){
				g_station_info_manager.first_station = current_station;
				g_station_info_manager.last_station = current_station;		
			}
			else{
				g_station_info_manager.last_station -> next_station_info = current_station;
				g_station_info_manager.last_station = current_station;
			}
			g_station_info_manager.station_total_number ++;
			largest_station_num ++;
			pthread_mutex_unlock(&g_station_info_manager.stations_mutex);
			
			//start station thread
			pthread_attr_t attr;
			pthread_attr_init(&attr);
			pthread_attr_setstacksize(&attr, 20 * 1024 * 1024);
			pthread_mutex_lock(&station_num_lock);
			pthread_create(&current_station->sending_thread, &attr, sending_thread_func, (void*)&station_num);
			//send to all
			pthread_mutex_lock(&g_client_info_manager.clients_mutex);
			struct client_info* traverser = g_client_info_manager.first_client;
			while(traverser != NULL){
				char command[MAX_LENGTH];
				memset(command, 0, MAX_LENGTH);
				sprintf(command, "A new station [%d] has been added .", station_num);
				send_announce_command(traverser->client_sockfd, command);
				traverser = traverser -> next_client_info;
			}
			pthread_mutex_unlock(&g_client_info_manager.clients_mutex);
			//free(dir);
		}
		//remove station
		else if(instruction == 'r'){
			//get the station number to be deleted
			char* str = malloc(MAX_LENGTH);
			strcpy(str, input_msg + 2);
			printf("Wants to remove a station %s.\n", str);
			char* last = 0;
			int station_num = strtol(str, &last, 10);
			if(delete_station(station_num) == -1){
				printf("Delete Failed!\n");
				exit(-1);
			}
			fflush(stdout);
			free(str);
		}
		else{
			printf("what!?\n");
		}
	}
	printf("what!?\n");
}

int main(int argc, char** argv){
	if(argc < 2){
		printf("Usage: snowcast_server tcpport [station_songs_folder1] [station_songs_folder2] [station_songs_folder3] [...] \n");
		exit(-1);
	}
	largest_station_num = argc - 2 - 1;
	
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
	pthread_mutex_lock(&g_station_info_manager.stations_mutex);
	for(i = 0; i <=largest_station_num; i ++){
		struct station_info* current_station = (struct station_info*)malloc(sizeof(struct station_info));
		memset(current_station,0,sizeof(struct station_info));
		current_station->station_song_manager.song_total_number = 0;
		//initialize songs..
		current_station->songs_dir = opendir(argv[i+2]);
		if(current_station->songs_dir == NULL){
			printf("Station %d has failed to load songs:%s\n", i, strerror(errno));
			exit(-1);
		}
		struct dirent* song_dirp = readdir(current_station->songs_dir);
		if(song_dirp == NULL){
			printf("The songs directory for station %d has no song!\n", i);
			exit(-1);
		}
		while(song_dirp != NULL){
			if(strcmp(song_dirp->d_name,".") != 0 && strcmp(song_dirp->d_name,"..") != 0){
				struct song_info* new_song_info = malloc(sizeof(struct song_info));
				memset(new_song_info,0,sizeof(struct song_info));
				new_song_info->song_name = (char*)malloc(MAX_LENGTH);
				memset(new_song_info->song_name,0, MAX_LENGTH);
				strcpy(new_song_info->song_name, song_dirp->d_name);
				if(current_station->station_song_manager.song_total_number == 0){
					current_station->station_song_manager.first_song = new_song_info;
					current_station->station_song_manager.last_song = new_song_info;
				}
				else{
					current_station->station_song_manager.last_song->next_song_info = new_song_info;
					current_station->station_song_manager.last_song = new_song_info;
				}
				current_station->station_song_manager.song_total_number ++;
			}
			song_dirp = readdir(current_station->songs_dir);
		}
		current_station->songs_dir_name = argv[i+2];
		current_station->current_song_info = current_station->station_song_manager.first_song;
		current_station->station_num = i;
		current_station->next_station_info = 0;
		pthread_mutex_init(&current_station->station_mutex, NULL);
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
	pthread_mutex_unlock(&g_station_info_manager.stations_mutex);
	
	//create a single thread for listening tcp mesasges...
	pthread_t listening_thread;
	pthread_t instruction_thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 20 * 1024 * 1024);
	//struct listen_param* lp = (struct listen_param*)malloc(sizeof(struct listen_param));
	//lp->sockfds = sockfds;
	pthread_create(&listening_thread, &attr, listening_thread_func, sockfds);
	pthread_create(&instruction_thread, &attr, instruction_thread_func, NULL);
	

	struct station_info	* info_traverser = g_station_info_manager.first_station;
	while(info_traverser != NULL){
		pthread_mutex_lock(&station_num_lock);
		volatile int station_num = info_traverser->station_num;
		pthread_create(&info_traverser->sending_thread, &attr, sending_thread_func, (void*)&station_num);
		info_traverser = info_traverser->next_station_info;
	}

	pthread_join(instruction_thread,0);
	pthread_cancel(listening_thread);
	close(sockfds[1]);
	close(sockfds[0]);


	//free all station
	while(g_station_info_manager.first_station != NULL){
		struct station_info* to_delete = g_station_info_manager.first_station;
		pthread_cancel(to_delete->sending_thread);
		while(to_delete->station_song_manager.first_song != NULL){
			struct song_info* to_delete_song = to_delete->station_song_manager.first_song;
			free(to_delete_song->song_name);
			to_delete->station_song_manager.first_song = to_delete_song->next_song_info;
			free(to_delete_song);
		}
		g_station_info_manager.first_station = to_delete->next_station_info;
		closedir(to_delete->songs_dir);
		free(to_delete);
	}

	//free all clients 
	while(g_client_info_manager.first_client != NULL){
		struct client_info* to_delete = g_client_info_manager.first_client;
		g_client_info_manager.first_client = to_delete->next_client_info;
		free(to_delete->client_readable_addr);
		free(to_delete);
	}
	free(servinfos[0]);
	free(servinfos[1]);
	return 0;
}
