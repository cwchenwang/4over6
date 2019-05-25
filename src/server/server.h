#include <thread>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <assert.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <linux/ip.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#define IP_REQUEST    100
#define IP_RESPONSE   101
#define NET_REQUEST   102
#define NET_RESPONSE  103
#define KEEPALIVE     104
#define SERVER_PORT   5678

#define MAX_EPOLL_EVENT 100
#define MAX_USER        10

#define MAX_DATA_LEN  4096

struct Msg {
    int length;
    char type;
    char data[MAX_DATA_LEN];
};

#define MSG_HEADER_SIZE   5
#define MSG_DATA_SIZE(msg)  (msg.length-MSG_HEADER_SIZE)
#define CLIENT_START_ADDR "10.0.0.100"

struct user_info {
    int fd;
    int count;
    int secs;
    struct in_addr v4addr;
    struct in6_addr v6addr;
} user_info_table[MAX_USER];
