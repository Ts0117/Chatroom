#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <map>
#include "defs.h"

#define SERVPORT 4358
#define BACKLOG 10

using namespace std;

struct arg
{
    int connfd, index;
};

struct client
{
    bool connected;
    int connfd;
    in_addr_t addr;
    in_port_t port;
};

// 以 socket_id 为关键字的线程池
map <int, pthread_t> threads;
// 以序号为关键字的客户端列表
map <int, client*> clients;
// 以 sock_id 为关键字的客户端序号表
map <int, int> map_;

bool quit = false;

void handler(int sig)
{
    quit = true;
}
struct sigaction int_handler;

void handle_message(message *mes_, int connect_fd)
{
    Message mes = mes_->message_;
    message send_mes;
    int sendbytes;

    if (mes.mode != REQUEST)
        perror("wrong message");

    send_mes.message_.mode = RESPONSE;
    send_mes.message_.func = mes.func;
    switch (mes.func)
    {
    case TIME:
        time_t time_;
        time_ = time(NULL);
        memcpy(send_mes.message_.data, &time_, 4);
        break;    
    case NAME:
        char name[64];
        gethostname(name, 64);
        memcpy(send_mes.message_.data, name, 64);
        break;
    case CLIENTS:
        struct node { uint16_t index, port; uint32_t ip; };
        struct list_ { uint16_t number; node clients[MAX_CONNECTED_NO]; };
        union { list_ data_; char char_[MAXINFOSIZE]; } client_data;
        client_data.data_.number = clients.size();
        for (int i = 1, j = 1; j <= client_data.data_.number; i++, j++)
        {
            while (clients.find(i) == clients.end())
            {
                printf("%d,", i);
                i++;
            }
            client_data.data_.clients[j].index = i;
            client_data.data_.clients[j].port = clients[i]->port;
            client_data.data_.clients[j].ip = clients[i]->addr;
        }
        memcpy(send_mes.message_.data, client_data.char_, MAXINFOSIZE);
        break;
    case TEXT:
        int dest, src;
        message inst_mes;

        memcpy(&dest, mes.data, 2);
        if (clients.find(dest) == clients.end())
        {
            send_mes.message_.data[0] = 1;
            memcpy(send_mes.message_.data + 1, "error: the destination doesn't exist.", 37);
        }
        else
        {
            inst_mes.message_.mode = INSTRUCTION;
            inst_mes.message_.func = TEXT;
            src = map_[connect_fd];
            memcpy(inst_mes.message_.data, &src, 2);
            memcpy(inst_mes.message_.data + 2, mes.data + 2, MAXINFOSIZE - 2);

            if((sendbytes = send(clients[dest]->connfd, inst_mes.data, MAXDATASIZE, 0)) == -1)
            {
                perror("send:");
                exit(1);
            }
            printf("Send bytes: %d; Type: TEXT INSTRUCTION\n", sendbytes);

            send_mes.message_.data[0] = 0;
            memcpy(send_mes.message_.data + 1, "send message success.", 37);
        }
        break;
    default:
        perror("wrong message type");
        exit(1);
    }

    if((sendbytes = send(connect_fd, send_mes.data, MAXDATASIZE, 0)) == -1)
    {
        perror("send:");
        exit(1);
    }
    printf("Send bytes: %d; Type: %s RESPOND\n", sendbytes, \
        mes.func == 1? "TIME": mes.func == 2? "NAME": mes.func == 3? "CLIENTS": "TEXT");
}

void handle_one_client(void *arg_)
{
    sockaddr_in peer_addr, connected_addr;
    socklen_t addr_len = sizeof(peer_addr);
    int sendbytes, recvbytes;
    char peer_ip_addr[INET_ADDRSTRLEN];
    message mes_buf;
    char buf[MAXDATASIZE];
    bool err = false;

    sigaction(SIGUSR1, &int_handler, 0);

    int connect_fd = ((arg *) arg_)->connfd;
    int index = ((arg *) arg_)->index;
    getsockname(connect_fd, (sockaddr *)&connected_addr, &addr_len);
    printf("connected server address = %s:%d\n", inet_ntoa(connected_addr.sin_addr), ntohs(connected_addr.sin_port));
    getpeername(connect_fd, (sockaddr *)&peer_addr, &addr_len);
    printf("connected peer address = %s:%d\n", inet_ntop(AF_INET, &peer_addr.sin_addr, peer_ip_addr, sizeof(peer_ip_addr)), ntohs(peer_addr.sin_port));

    int len = 0;
    while (!quit)
    {
        if ((recvbytes = recv(connect_fd, buf, MAXDATASIZE,0)) <= 0)
        {
            if (recvbytes != 0 && errno != EINTR)
            {
                err = 1;
                perror("recv:");
            }
            break;
        }
        printf("Recv bytes: %d\n", recvbytes);

        int rest = MAXDATASIZE - len;
        memcpy(mes_buf.data + len, buf, rest);
        len += min(rest, recvbytes);

        if (len == MAXDATASIZE)
        {
            handle_message(&mes_buf, connect_fd);
            len = recvbytes - rest;
            memcpy(mes_buf.data, buf + rest, recvbytes - rest);
        }
    }

    delete clients[index];
    threads.erase(connect_fd);
    clients.erase(index);
    map_.erase(connect_fd);
    close(connect_fd);
    printf("goodbye Client %d\n", index);
    if (err) exit(err);
}

int main()
{
    sockaddr_in server_sockaddr, listen_sockaddr, connected_addr;
    socklen_t sin_size;
    int listenfd, connect_fd;
    int client_index = 0;

    int_handler.sa_handler = handler;
    sigaction(SIGINT, &int_handler, 0);

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    { // 申请 socket 句柄
        perror("socket:");
        exit(1);
    }

    int on = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    // 开启端口复用

    server_sockaddr.sin_family=AF_INET;
	server_sockaddr.sin_port = htons(SERVPORT);
    server_sockaddr.sin_addr.s_addr=INADDR_ANY;
	memset(&(server_sockaddr.sin_zero),0,8);

	if((bind(listenfd,(sockaddr *)&server_sockaddr,sizeof(sockaddr))) == -1)	
	{ // 绑定监听端口，设置端口为 4358
		perror("bind:");
		exit(1);
	}

	if(listen(listenfd,BACKLOG) == -1)
	{ // 开始监听，设置连接等待队列长度为 10
		perror("listen:");
		exit(1);
	}
	printf("Start listening...\n");

    sin_size = sizeof(listen_sockaddr);
    getsockname(listenfd, (sockaddr *)&listen_sockaddr, &sin_size);
    printf("listen address = %s:%d\n", inet_ntoa(listen_sockaddr.sin_addr), ntohs(listen_sockaddr.sin_port));
    // 输出监听信息

    while (!quit)
    { // 持续监听直至关闭
        if((connect_fd = accept(listenfd, (sockaddr *)&connected_addr, &sin_size)) < 0)
        { // 客户端请求
            if (errno == EINTR)
            {
                printf("goodbye\n");
                continue;
            }
            perror("accept:");
            exit(1);
        }

        if (threads.size() >= MAX_CONNECTED_NO)
        {
            printf("too much clients!");
            continue;;
        }

        getpeername(connect_fd, (sockaddr *)&connected_addr, &sin_size);
        client* client_ = new client ;
        *client_ = {false, connect_fd, connected_addr.sin_addr.s_addr, ntohs(connected_addr.sin_port)};
        clients[++client_index] = client_;
        map_[connect_fd] = client_index;

        pthread_t thread_id;
        arg arg_ = {connect_fd, clients.size()};
        pthread_create(&thread_id, NULL, (void* (*)(void*))&handle_one_client, (void*)&arg_);
        threads[connect_fd] = thread_id;
        printf("There are %ld clients.\n", clients.size());
    }

    while (threads.size())
    {
        map <int, pthread_t>::iterator i = threads.begin();
        pthread_kill(i->second, SIGUSR1);
        pthread_join(i->second, NULL);
    }
	close(listenfd);
    return 0;
}