#include <jni.h>
#include <string>
#include <android/log.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TAG "native_backend"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define MSG_DATA_LENGTH 4096
#define MSG_LENGTH (4096 + sizeof(int) + sizeof(char))
#define HEADER_SIZE sizeof(int) + sizeof(char)

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_my_14over6_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

struct Msg {
    int length;
    char type;
    char data[MSG_DATA_LENGTH];
};

int flow_fd;
int inst_recv=0, inst_send=0;
int tot_recv=0, tot_send=0;

int sockfd;
int tunfd;
bool running;
bool set_tun0;
pthread_mutex_t info_lock;
pthread_mutex_t lock_run;
pthread_mutex_t lock_tun;
pthread_mutex_t lock_time;
pthread_mutex_t lock_sockfd;

time_t cur_time, last_beat, init_time;

struct Msg tun_packet, recv_packet, heart_beat;
struct sockaddr* remote_sock_addr;
socklen_t sock_len;

int send_msg(int sockfd, char* msg, int length)
{
    pthread_mutex_lock(&lock_sockfd);
    long real_len = send(sockfd, msg, (size_t)length, 0);
    pthread_mutex_unlock(&lock_sockfd);
    if (real_len < length) {
        LOGE("send_msg() error: length incompatible.\n");
        return -1;
    }
    return 0;
}

int send_ip_request_(int sockfd)
{
    struct Msg request_packet;
    int total_length = sizeof(int) + sizeof(char);
    request_packet.length = total_length;
    request_packet.type = 100;
    return send_msg(sockfd, (char *)&request_packet, total_length);
}

int send_heartbeat(int sockfd)
{
    heart_beat.type = 104;
    heart_beat.length = sizeof(int) + sizeof(char);
    return send_msg(sockfd, (char *)&heart_beat, heart_beat.length);
}

int open_pipe_for_write(const char* pipe_name)
{
    return open(pipe_name, O_RDWR | O_CREAT | O_TRUNC);
}

int open_pipe_for_read(const char* pipe_name)
{
    return open(pipe_name, O_RDWR | O_CREAT);
}

int process_ip_response(struct Msg *recv_pkt, const char* ip_pipe)
{
    int ip_fd = open_pipe_for_write(ip_pipe);
    // int data_offset = sizeof(int) + sizeof(char);
    // int data_len = length - sizeof(int) - sizeof(char);
    LOGD("data: %s\n", recv_pkt->data);

    char buffer[100];
    bzero(buffer, 100);
    sprintf(buffer, "%d %s", sockfd, recv_pkt->data);

    long write_len = write(ip_fd, buffer, (size_t)recv_pkt->length);
    if (write_len < 0) {
        LOGE("write to ip pipe failed.\n");
        return -1;
    }
    LOGD("write to ip pipe succeeded. write_length : %ld\n", write_len);
    return 0;
}

void init_argus()
{
    running = true;
    set_tun0 = false;
    pthread_mutex_init(&lock_run, nullptr);
    pthread_mutex_init(&lock_tun, nullptr);
    pthread_mutex_init(&lock_time, nullptr);
    pthread_mutex_init(&lock_sockfd, nullptr);

    cur_time = time(nullptr);
    last_beat = cur_time;
    init_time = cur_time;
}

bool isRun()
{
    bool ret;
    pthread_mutex_lock(&lock_run);
    ret = running;
    pthread_mutex_unlock(&lock_run);
    return ret;
}

void turnOff()
{
    pthread_mutex_lock(&lock_run);
    running = false;
    pthread_mutex_unlock(&lock_run);
}

bool isTunSet()
{
    bool ret;
    pthread_mutex_lock(&lock_tun);
    ret = set_tun0;
    pthread_mutex_unlock(&lock_tun);
    return ret;
}

void tunSet()
{
    pthread_mutex_lock(&lock_tun);
    set_tun0 = true;
    pthread_mutex_unlock(&lock_tun);
}

void tunUnset()
{
    pthread_mutex_lock(&lock_tun);
    set_tun0 = false;
    pthread_mutex_unlock(&lock_tun);
}

int sock_recv(int sockfd, char* buf, int n)
{
    int cur = 0, len;
    pthread_mutex_lock(&lock_sockfd);
    while (cur < n) {
        len = recv(sockfd, buf + cur, n - cur, 0);
        if (len <  0) {
            usleep(100);
            LOGD("sock_recv() <= 0, reconnect.\n");
            connect(sockfd, remote_sock_addr, sock_len);
            continue;
        } else if (len == 0){
            usleep(100);
            continue;
        } else {
            cur += len;
        }
    }
    pthread_mutex_unlock(&lock_sockfd);
    if (cur > n) {
        LOGE("sock_recv error: len: %d > n: %d\n", cur, n);
    }
    return cur;
}

void* read_tun(void* arg)
{
    while (!isTunSet()); // waiting for tun0 ready.
    while (isRun()) {
        memset(&tun_packet, 0, sizeof(struct Msg));
        long length = read(tunfd, tun_packet.data, MSG_DATA_LENGTH);
        if (length > 0) {
            tun_packet.length = length + sizeof(int) + sizeof(char);
            tun_packet.type = 102;

            send_msg(sockfd, (char *)&tun_packet, tun_packet.length);

            //TODO
            // add flow calculating
            pthread_mutex_lock(&info_lock);
            tot_send += tun_packet.length;
            inst_send += tun_packet.length;
            pthread_mutex_unlock(&info_lock);
        }
    }
    LOGD("read_tun() exit.\n");
    return nullptr;
}

void* read_ip_pipe(void* arg)
{
    const char* ip_f2b = (const char*)arg;
    LOGD("read_ip_pipe() get pipe name: %s\n", ip_f2b);
    int ip_fd = open_pipe_for_read(ip_f2b);
    if (ip_fd < 0) {
        LOGE("read_ip_pipe() failed to get ip_fd. thread exit.\n");
        return nullptr;
    }
    char buffer[100];
    while (isRun()) {
        long length = read(ip_fd, buffer, 100);
        // LOGD("length : %ld\n", length);
        if (length <= 0) {
            // LOGE("read_ip_pipe() error: length == %ld\n", length);
            continue;
        }
        // length > 0
        if (!isTunSet()) {
            sscanf(buffer, "%d", &tunfd);
            LOGD("get tun0 : %d\n", tunfd);
            tunSet();
        } else {
            int sign;
            sscanf(buffer, "%d", &sign);
            LOGD("sign is : %d\n", sign);
            if (sign == -100) {
                /* app quit */
                turnOff();
            }
        }
    }
    close(ip_fd);
    return nullptr;
}

void send_flow_pipe() {
    printf("write flow infomation");
    char buf[50];
    memset(buf, 0, 50);

    int elapse = cur_time - init_time;
    int bs = sprintf(buf, "%d %d %d %d %d ", inst_recv, tot_recv, inst_send, tot_send, elapse);
    int size;

    lseek(flow_fd, 0, SEEK_SET);
    size = write(flow_fd, buf, bs);
    if (size < 0) {
        printf("write to pipe error\n");
    }
    printf("send flow info: %s", buf);
}

void* tik_tok(void* arg)
{
    int send_beat_interval = 0;
    while (isRun()) {
        sleep(1);
        cur_time = time(nullptr);
        pthread_mutex_lock(&lock_time);
        time_t diff = cur_time - last_beat;
        pthread_mutex_unlock(&lock_time);
        //TODO
        // send flow info
        send_flow_pipe();
        pthread_mutex_lock(&info_lock);
        inst_recv = 0;
        inst_send = 0;
        pthread_mutex_unlock(&info_lock);


        if (diff < 60) {
            send_beat_interval ++;
            if (send_beat_interval == 20) {
                LOGD("send_heartbeat() called.\n");
                send_heartbeat(sockfd);
                send_beat_interval = 0;
            }
        } else {
            turnOff();
        }
    }
    LOGD("tik tok thread exit.\n");
    return nullptr;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_my_14over6_MainActivity_unlink_1backend(JNIEnv *env, jobject instance) {
    pthread_mutex_lock(&lock_run);
    running = false;
    pthread_mutex_unlock(&lock_run);
    close(sockfd);
}

// backend thread entrance
extern "C"
JNIEXPORT void JNICALL
Java_com_example_my_14over6_MainActivity_backend(JNIEnv *env, jobject instance, jstring addr_,
                                                 jstring port_, jstring ip_pipe_, jstring flow_pipe_,
                                                 jstring ip_f2b_)
 {
    const char *addr = env->GetStringUTFChars(addr_, 0);
    const char *port = env->GetStringUTFChars(port_, 0);
    const char *ip_pipe = env->GetStringUTFChars(ip_pipe_, 0);
    const char *flow_pipe = env->GetStringUTFChars(flow_pipe_, 0);
    const char *ip_f2b = env->GetStringUTFChars(ip_f2b_, 0);
    LOGD("backend thread start running...\n");

    init_argus();

    int ret;
    LOGD("building pipe...\n");
    mknod(ip_pipe, S_IFIFO | 0666, 0);
    mknod(flow_pipe, S_IFIFO | 0666, 0);
    mknod(ip_f2b, S_IFIFO | 0666, 0);
    LOGD("building pipe complete.\n");
    flow_fd = open_pipe_for_write(flow_pipe);
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *cur;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    // hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    // hints.ai_protocol = IPPROTO_IPV6; // it turns to be IPPROTO_TCP.

    LOGD("%s", addr);
    LOGD("%s", port);

    ret = getaddrinfo(addr, port, &hints, &res);
    if (ret != 0) {
        LOGE("getaddrinfo() failed.\n");
        goto out;
    }
    ret = -1;
    for (cur = res; cur != nullptr; cur = cur->ai_next) {
        sockfd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        LOGD("cur->family : %d, cur->socktype: %d, cur->proto : %d\n", cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        // LOGE("cur->addr : %s\n", cur->ai_addr->sa_data);
        if (sockfd < 0) {
            LOGD("socket() failed. continue to next one.\n");
            continue;
        }
        int enable = 1;
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int));
        ret = connect(sockfd, cur->ai_addr, cur->ai_addrlen);
        if (ret == 0) {
            // server connected
            remote_sock_addr = cur->ai_addr;
            sock_len = cur->ai_addrlen;
            break;
        } else {
            LOGD("connect() failed. continue to next one.\n");
            close(sockfd);
        }
    }

    if (ret != 0) {
        LOGE("connect to server failed. backend thread exit.\n");
        goto out;
    }

    LOGD("connect complete!\n");

    ret = send_ip_request_(sockfd);
    if (ret != 0) {
        LOGE("send_ip_request_() failed.\n");
    } else {
        LOGD("send_ip_request() succeeded.\n");
    }

    pthread_t read_ip_pipe_t, read_tun_t, tik_tok_t;
    pthread_create(&read_ip_pipe_t, nullptr, read_ip_pipe, (void *)ip_f2b);
    pthread_create(&read_tun_t, nullptr, read_tun, nullptr);
    pthread_create(&tik_tok_t, nullptr, tik_tok, nullptr);
    // start while loop
    // char buf[MSG_LENGTH];
    while (isRun()) {
        memset(&recv_packet, 0, sizeof(struct Msg));
//        long len = 0;
//        long recv_length = 0;
        int size = sock_recv(sockfd, (char *)&recv_packet, sizeof(int));
        int data_size = sock_recv(sockfd, (char *)&recv_packet + size, recv_packet.length - size);

        //TODO
        // bug fix
        if (size + data_size != recv_packet.length) {
            LOGD("head_len: %d, data_len: %d\n", size, data_size);
            LOGD("receive total length: %d\n", size + data_size);
            LOGD("receive packet length: %d\n", recv_packet.length);
            LOGD("recv packet type: %d\n", recv_packet.type);
        }
        if (recv_packet.type == 101) {
            process_ip_response(&recv_packet, ip_pipe);
        }
        else if (recv_packet.type == 103) {
            int length = recv_packet.length - sizeof(int) - sizeof(char);
            long write_len = write(tunfd, recv_packet.data, (size_t)length);
            if (write_len != length) {
                LOGE("103 type write error. length incompatible.");
            }
            //TODO
            // add flow calculating in
            pthread_mutex_lock(&info_lock);
            tot_recv += recv_packet.length;
            inst_recv += recv_packet.length;
            pthread_mutex_unlock(&info_lock);
        }
        else if (recv_packet.type == 104) {
            LOGD("recv heartbeat.\n");
            pthread_mutex_lock(&lock_time);
            last_beat = time(nullptr);
            pthread_mutex_unlock(&lock_time);
        }
    }

    pthread_join(read_ip_pipe_t, nullptr);
    pthread_join(read_tun_t, nullptr);
    pthread_join(tik_tok_t, nullptr);

    close(sockfd);
    close(flow_fd);
    LOGD("backend thread successfully returned.\n");
out:
    env->ReleaseStringUTFChars(addr_, addr);
    env->ReleaseStringUTFChars(port_, port);
    env->ReleaseStringUTFChars(ip_pipe_, ip_pipe);
    env->ReleaseStringUTFChars(flow_pipe_, flow_pipe);
    env->ReleaseStringUTFChars(ip_f2b_, ip_f2b);
    freeaddrinfo(res);
}