#include "server.h"

int epfd;
int tun_fd = -1;
int client_fd = -1;
int listenfd = -1; //for server to listen
in_addr tun_addr;
pthread_mutex_t mutex;

void setnonblocking(int sock) {
    int opts;
    opts = fcntl(sock, F_GETFL);
    if(opts < 0) {
        perror("fcntl(sock,GETFL)");
        exit(1);
    }
    opts = opts | O_NONBLOCK;
    if( fcntl(sock, F_SETFL, opts) < 0 ){
        perror("fcntl(sock,SETFL,opts)");
        exit(1);
    }
}

int find_user_by_fd(int fd) {
    if(fd < 0) return -1;
    pthread_mutex_lock(&mutex);
    for(int i = 0; i < MAX_USER; i++) {
        if(user_info_table[i].fd == fd) {
            pthread_mutex_unlock(&mutex);
            return i;
        }
    }
    pthread_mutex_unlock(&mutex);
    return -1;
}

//response to IP_REQUEST packet
int ip_response(int fd) {
    printf("Send IP_RESPONSE packet\n");
    Msg msg;
    msg.type = IP_RESPONSE;
    sprintf(msg.data ,"%s 0.0.0.0 202.38.120.242 8.8.8.8 202.106.0.20 ", inet_ntoa(tun_addr));
    msg.length = strlen(msg.data) + MSG_HEADER_SIZE;
    return send(fd, &msg, msg.length, 0);
}

int send_keepalive(int fd) {
    printf("Send KEEPALIVE packet\n");
    Msg msg;
    msg.type = KEEPALIVE;
    msg.length = MSG_HEADER_SIZE;
    return send(fd, &msg, msg.length, 0);
}

int sock_receive(int fd, char* buff, int n) {
    int left = n;
    while(left > 0) {
        ssize_t recvn = read(fd, buff + n - left, left);
        if ( recvn == -1 ) {
            usleep(100);
            continue;
        } else if( recvn == 0 ) {
            return 0;
        } else if( recvn > 0 ) {
            left-=recvn;
        } else {
            perror("Recv error");
            return -1;
        }
    }
    return n;
}

void process_packet_to_tun(int fd) {
    struct Msg msg;
    printf("Processing packet to tun: ");
    int ret = read(tun_fd, msg.data, MAX_DATA_LEN);
    if( ret <= 0 ){
        printf("Error processing tun packet\n");
        return;
    }

    ret += MSG_HEADER_SIZE;
    struct iphdr *hdr = (struct iphdr *)msg.data;
    char saddr[16], daddr[16];
    inet_ntop(AF_INET, &hdr->saddr, saddr, sizeof(saddr));
    inet_ntop(AF_INET, &hdr->daddr, daddr, sizeof(daddr));
    printf("A packet from %s to %s\n", saddr, daddr);
    //set the packet to client
    if(hdr->version == 4 && hdr->daddr == tun_addr.s_addr) {
        msg.type = NET_RESPONSE;
        msg.length = ret;
        send(fd, (void*)&msg, msg.length, 0);
        printf("Send back NET_REPONSE packet\n");
    }
}

int process_packet_from_client(int fd, int user) {
    if(fd < 0) return -1;
    // int user = find_user_by_fd(fd);
    // if(user < 0 || user >= MAX_USER) {
    //     printf("Error receving a packet from unknown user");
    //     return -1;
    // }
    // printf("Processing packet from user %d, fd %d\n", user, fd);
    struct Msg msg;
    int n = sock_receive(fd, (char*)&msg, MSG_HEADER_SIZE);
    if( n <= 0 ){
        printf("Receive from client failed\n");
        close(fd);
        for(int i = 0; i < MAX_USER; i++) {
            if( user_info_table[i].fd == fd ) {
                user_info_table[i].fd = -1;
            }
        }
        return -1;
    }

    if(msg.type == KEEPALIVE) {
        printf("Receive a keepalive packet from user %d\n", user);
        user_info_table[user].secs = time(NULL);
    } else if(msg.type == IP_REQUEST) {
        printf("Receive a IP REQUEST packet\n");
        int ret;
        if( (ret=ip_response(fd) < 0) ) {
            printf("Send ip response failed\n");
            exit(0);
        }
        return ret;
    } else if(msg.type == NET_REQUEST) {
        printf("Receive a NET REQUEST packet\n");
        n = sock_receive(fd,msg.data,msg.length - MSG_HEADER_SIZE);
        if(n == msg.length - MSG_HEADER_SIZE) {
            iphdr *hdr = (struct iphdr *)msg.data;
            if( hdr->version == 4 ){
                hdr->saddr = tun_addr.s_addr;
            }
            if( hdr->saddr == tun_addr.s_addr ){
                write(tun_fd, msg.data, MSG_DATA_SIZE(msg));
            }
        }
    } else {
        printf("Receive unknown type packet\n");
    }
    return 0;
}

struct epoll_event ev, events[20];;
void event_add(int fd) {
    ev.data.fd = fd;
    ev.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd,&ev);
}

void event_del(int fd) {
    ev.data.fd= fd;
    ev.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev);
}

int init_server() {
    listenfd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
    if (listenfd == -1) {
        perror("Error on server listening");
        exit(-1);
    }
    struct sockaddr_in6 server_addr;
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_addr = in6addr_any;
    server_addr.sin6_port = htons(SERVER_PORT);
    int ret = bind(listenfd, (struct sockaddr *) &server_addr, sizeof(server_addr));
    if (ret == -1) {
        perror("server bind error");
        close(listenfd);
        exit(-1);
    }
    if( (ret=listen(listenfd, MAX_USER)) < 0 ) {
        perror("Server listen failed");
    }
    setnonblocking(listenfd);
    event_add(listenfd);
    return listenfd;
}

void init_iptable() {
    system("iptables -F");
    system("iptables -t nat -F");
    system("echo \"1\" > /proc/sys/net/ipv4/ip_forward"); //enable ip forwarding
    //accept packets
    system("iptables -A FORWARD -j ACCEPT");
    //set SNAT, POSTROUTING applies when a packet leaves the interface
    system("iptables -t nat -A POSTROUTING -s 10.0.0.0/8 -j MASQUERADE");
}

//refer to linux kernel
int tun_alloc(char* dev) {
    int fd, err;
    if((fd = open("/dev/net/tun", O_RDWR)) < 0){
        printf("Create tun failed\n");
        return fd;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(struct ifreq));
    //IFF_TUN - TUN device
    //IFF_NO_PI - No package information
    ifr.ifr_flags |= IFF_TUN | IFF_NO_PI;

    if( *dev )
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    if ( (err=ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
        printf("Set tun device name failed\n");
        close(fd);
        return err;
    }

    char buffer[256];
    sprintf(buffer,"ip link set dev %s up", ifr.ifr_name);
    system(buffer);
    sprintf(buffer,"ip a add 10.0.0.1/24 dev %s", ifr.ifr_name);
    system(buffer);
    sprintf(buffer,"ip link set dev %s mtu %u", ifr.ifr_name, 1500 - MSG_HEADER_SIZE);
    system(buffer);
    return fd;
}

void init_tun() {
    tun_addr={0};
    inet_aton("10.0.0.3", &tun_addr);
    printf("TUN ADDRESS: 10.0.0.3\n");

    char dev[IFNAMSIZ];
    strcpy(dev, "4over6");
    tun_fd = tun_alloc(dev);
    setnonblocking(tun_fd);
    event_add(tun_fd);
}

void* keepalive_func(void*) {
    pthread_mutex_lock(&mutex);
    while(true) {
        pthread_mutex_unlock(&mutex);
        sleep(1);
        pthread_mutex_lock(&mutex);
        for ( int i = 0; i < MAX_USER; i++ ) {
            int fd = user_info_table[i].fd;
            if ( fd == -1 ) {
                continue;
            }
            if ( time(NULL) - user_info_table[i].secs > 60 ) {
                printf("Timeout, remove user %d\n", fd);
                user_info_table[i].fd = -1;
                close(fd);
                event_del(fd);
            } else {
                user_info_table[i].count -= 1;
                if ( user_info_table[i].count == 0 ) {
                    send_keepalive(fd);
                    user_info_table[i].count = 5;
                }
            }
        }
    }
}
 
void close_all() {
    close(tun_fd);
    close(listenfd);
    for(int i = 0; i < MAX_USER; i++) {
        if(user_info_table[i].fd >= 0) {
            close(user_info_table[i].fd);
        }
    }
}

static void exit_handler(int sig) {
    close_all();
    printf("Exit the process\n");
    exit(0);
}

int main(){
    signal(SIGINT, exit_handler);

    epfd = epoll_create(MAX_EPOLL_EVENT);

    init_server();
    init_iptable();
    init_tun();

    printf("listenfd: %d\n", listenfd);
    printf("tunfd: %d\n", tun_fd);

    //https://blog.csdn.net/ljx0305/article/details/4065058
    int nfds, connfd, ret;

    pthread_t keepalive_thread;
    ret = pthread_create(&keepalive_thread, NULL, keepalive_func, NULL);
    printf("Keep alive thread start\n");

    //init user_info_table
    for (int i = 0; i < MAX_USER; i++) {
        in_addr_t client_start = inet_addr(CLIENT_START_ADDR);
        user_info_table[i].v4addr.s_addr = htonl(client_start + i);
        user_info_table[i].fd = -1;
    }

    struct sockaddr_in6 clientaddr;
    socklen_t client_len = sizeof(clientaddr);
    while(true) {
        nfds = epoll_wait(epfd, events, 20, 500);
        for(int i = 0; i < nfds; i++) {
            if(events[i].data.fd == listenfd) { //listen event
                printf("listening event\n");
                connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &client_len);
                client_fd = connfd;
                if (connfd == -1) {
                    perror("accept failed");
                    close(connfd);
                    exit(-1);
                }
                printf("client fd: %d\n", client_fd);

                int i = 0;
                pthread_mutex_lock(&mutex);
                for (; i < MAX_USER; i++) {
                    if (user_info_table[i].fd == -1) {
                        user_info_table[i].fd = connfd;
                        memcpy(&(user_info_table[i].v6addr), &clientaddr, sizeof(struct sockaddr));
                        user_info_table[i].secs = time(NULL);
                        user_info_table[i].count = 5;
                        break;
                    }
                }
                pthread_mutex_unlock(&mutex);

                if ( i == MAX_USER ) {
                    printf("Cannot accept more users\n");
                    continue;
                }
                i = 0;

                event_add(connfd);
                char str_addr[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &(clientaddr.sin6_addr), str_addr, sizeof(str_addr));
                printf("A new user %s: %d\n", str_addr, ntohs(clientaddr.sin6_port));
            } else { 
                if(events[i].data.fd == tun_fd) {
                    process_packet_to_tun(client_fd);
                } else if(events[i].events & EPOLLIN) {
                    //only for one client
                    int fd = events[i].data.fd;
                    int user = 0;
                    user = find_user_by_fd(fd);
                    if(user < 0 || user >= MAX_USER) {
                        printf("Error receving a packet from unknown user");
                        continue;
                    }
                    printf("Processing packet from user %d, fd %d\n", user, fd);
                    process_packet_from_client(fd, user);
                }
            }
        }
    }
    close_all();
    return 0;
}
