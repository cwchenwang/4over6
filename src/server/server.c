#include <stdio.h>
#include "server.h"

int listen_fd;

//listen at certain port
int init_server() {
	//create ipv6 socket
	if((listen_fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("server socket");
		return -1;
	}

	struct sockaddr_in6 serv_addr;

	return 0;
}

int main() {
	if(init_server < 0) {
		printf("server init failed\n");
		exit(0);
	};
	perror("hello world");
	return 0;
}
