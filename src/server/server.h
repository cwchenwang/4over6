#include <stdio.h>
#include <stdlib.h> //exit
#include <unistd.h> //close
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <net/if.h> //if_nametoindex
#include <sys/socket.h>

#define NUM_USERS 100
//Message type
#define IP_REQUEST 100
#define IP_RESPONSE 101
#define NET_REQUEST 102
#define NET_RESPONSE 103
#define KEEPALIVE 104

//Server info
#define SERVER_PORT 5678
#define CLIENT_QUEUE_LEN 10

typedef struct {
	int length;
	char type;
	char data[4096];
} Msg;

typedef struct user_info_table {
	int fd; //socket descriptor
	int cnt; //set to 20 when a user connects, decrease by 1 for every second. Sent keepalive packet when is 0
	int secs; //the last time receives keepalive
	struct in_addr v4addr;
	struct in6_addr v6addr;
	struct user_info_table* next;
} user_info_table;
