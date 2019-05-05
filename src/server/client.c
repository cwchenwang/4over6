#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define SERVER_PORT 5000

int client_fd = -1;
int main() {
	client_fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if(client_fd < -1) {
		printf("socket()\n");
		return -1;
	}
	struct sockaddr_in6 serv_addr;
	serv_addr.sin6_family = AF_INET6;
	serv_addr.sin6_addr = in6addr_any;
	serv_addr.sin6_port = htons(SERVER_PORT);
	inet_pton(AF_INET6, "::1", &serv_addr.sin6_addr);
	if(connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		perror("connect");
		return -1;
	}
	printf("connect success\n");

	char send_buf[200];
	char recv_buf[200];
	while(1) {
		printf("Send: ");
		scanf("%s", send_buf);
		printf("\n");
		send(client_fd, send_buf, strlen(send_buf), 0);
		if(strcmp(send_buf, "quit") == 0)
			break;
		printf("Read:");
		recv_buf[0] = '\0';
		int len = recv(client_fd, recv_buf, 200, 0);
		recv_buf[len] = '\0';
		printf("%s\n", recv_buf);	
	}

	return 0;
}
