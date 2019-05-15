#include "server.h"
int listen_fd = -1;

//listen at certain port
int init_server() {
	//create ipv6 socket
	if((listen_fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("server socket");
		return -1;
	}
	//populate the socket structure
	struct sockaddr_in6 serv_addr;
	serv_addr.sin6_family = AF_INET6;
	serv_addr.sin6_addr = in6addr_any;
	serv_addr.sin6_port = htons(SERVER_PORT);
	int ret;
	if((ret=bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) == -1) {
		perror("server bind");
		close(listen_fd);
		return -1;
	}

	//create listening queue
	if((ret=listen(listen_fd, CLIENT_QUEUE_LEN)) == -1) {
		perror("server listen");
		close(listen_fd);
		return -1;
	}
	printf("server init success!\n");
	return 0;
}

void handle_connection() {
	printf("listening at port: %d\n", SERVER_PORT);
	char str_addr[INET6_ADDRSTRLEN];
	while(1) {
		struct sockaddr_in6 cli_addr;
		socklen_t client_len = sizeof(cli_addr);
		int client_fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &client_len);
		if(client_fd < 0) {
			perror("accept");
			continue;
		}
		inet_ntop(AF_INET6, &(cli_addr.sin6_addr),
				str_addr, sizeof(str_addr)); //convert from numeric to pointer
		printf("New connection from: %s:%d ...\n",
				str_addr,
				ntohs(cli_addr.sin6_port));
		char buffer[200];
		while(1) {
			printf("Read:");
			buffer[0] = '\0';
			int data_num = recv(client_fd, buffer, 1024, 0);
			if(data_num < 0) {
				perror("recv null");
				continue;
			}
			buffer[data_num] = '\0';
			if(strcmp(buffer, "quit") == 0)
				break;
			
			printf("%s\n", buffer);
			printf("Send:");
			scanf("%s", buffer);
			printf("\n");
			send(client_fd, buffer, strlen(buffer), 0);
			if(strcmp(buffer, "quit") == 0)
				break;
		}
	}
}

void create_tun() {


}

int main() {
	if(init_server() < 0) {
		printf("server init failed\n");
		exit(EXIT_FAILURE);
	};
	handle_connection();
	return 0;
}
