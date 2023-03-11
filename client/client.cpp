#include <arpa/inet.h>
#include <error.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <fstream>
#include "defs.h"

using namespace std;

bool connected = false;
bool quit = false;

void handler(int sig)
{
    quit = true;
}
struct sigaction int_handler;

pthread_t recv_thread, listen_thread;
struct arg
{
    int sockfd_fd;
};
message_queue MQ, MQ_inst;

void handle_instruction(void *)
{
    message *mes;

    while (!quit)
    {
        mes = MQ_inst.front();
        MQ_inst.pop();

        int index;
        memcpy(&index, mes->message_.data, 2);
        printf("message from [Client %d]: %s\n", index, mes->message_.data + 2);
    }
}

void receive(void *arg_)
{
    char buf[MAXDATASIZE];
    message mes_buf;
    int sockfd = ((arg *) arg_)->sockfd_fd;
    int recvbytes, len = 0;
    bool err = false;

    sigaction(SIGUSR1, &int_handler, 0);

    while (!quit)
    {
        if ((recvbytes = recv(sockfd, buf, MAXDATASIZE, 0)) <= 0)
        {
            if (errno == EINTR)
            {
                printf("disconnecting...\n");
                continue;
            }
            else
            {
                err = 1;
                if (recvbytes == 0) perror("connection shut down!");
                else perror("recv:");
                break;
            }
        }
        printf("Client receive bytes: %d\n", recvbytes);

        int rest = MAXDATASIZE - len;
        memcpy(mes_buf.data + len, buf, rest);
        len += min(rest, recvbytes);

        if (len == MAXDATASIZE)
        {
            if (mes_buf.message_.mode == INSTRUCTION)
                MQ_inst.push(&mes_buf);
            else MQ.push(&mes_buf);
            len = recvbytes - rest;
            memcpy(mes_buf.data, buf + rest, recvbytes - rest);
        }
    }
    delete (arg*) arg_;
    if (err) exit(err);
}

void connect_(int sockfd)
{
    struct in_addr addr;
    struct hostent *host;
    struct sockaddr_in serv_addr;

    if (connected)
    {
        printf("You are already connected to server\n");
        return;
    }
    printf("Please type in the IP and port of server.\nIP: ");
    char ip[INET_ADDRSTRLEN];
    int port;
    scanf("%s", ip);
    printf("port: ");
    scanf("%d%*c", &port);
    // strcpy(ip, "127.0.0.1");
    // port = 4358;

    inet_aton(ip, &addr);
    if ((host = gethostbyaddr(&addr, sizeof(addr), AF_INET)) == NULL)
    {
        perror("gethostbyaddr:");
        exit(1);
    }

    printf("hostent h_name: %s , h_aliases: %s, h_addrtype: %d, h_length: %d, h_addr_list: %s\n",
            host->h_name, *(host->h_aliases), host->h_addrtype, host->h_length,
            inet_ntop(host->h_addrtype, host->h_addr, ip, sizeof(ip)));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr = *((struct in_addr *)host->h_addr);
    bzero(&(serv_addr.sin_zero), 8);
    
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1)
    {
        perror("connect:");
        exit(1);
    }
    printf("connect server success.\n");
    connected = true;

    arg *arg_ = new arg;
    arg_->sockfd_fd = sockfd;
    pthread_create(&recv_thread, NULL, (void* (*)(void*))&receive, (void*)arg_);
    pthread_create(&listen_thread, NULL, (void* (*)(void*))&handle_instruction, NULL);
}

void disconnect(int sockfd)
{
    pthread_kill(recv_thread, SIGUSR1);
    pthread_join(recv_thread, NULL);
    pthread_detach(listen_thread);
    pthread_cancel(listen_thread);
    connected = false;
    close(sockfd);
}

void request(int func, int sockfd)
{
    int sendbytes;
    message send_mes, *mes;
    send_mes.message_.mode = REQUEST;
    send_mes.message_.func = func;

    if (send_mes.message_.func == TEXT)
    {
        uint16_t index;
        char info[MAXINFOSIZE - 2];
        printf("Please select a client by its index: ");
        scanf("%d", &index);
        printf("Please enter the information: ");//?
        scanf("%s%*c", info);
        memcpy(send_mes.message_.data, &index, 2);
        memcpy(send_mes.message_.data + 2, info, MAXINFOSIZE - 2);
    }

    if ((sendbytes = send(sockfd, send_mes.data, MAXDATASIZE, 0)) == -1)
    {
        perror("send:");
        exit(1);
    }
    printf("Send bytes: %d; Type: %s REQUEST\n", sendbytes, \
        func == 1? "TIME": func == 2? "NAME": func == 3? "CLIENTS": "TEXT");

    do
    {
        mes = MQ.front();
    } while (mes->message_.mode != RESPONSE || mes->message_.func != func);
    MQ.pop();

    switch (func)
    {
    case TIME:
        int time_;
        char* time;
        memcpy(&time_, mes->message_.data, 4);
        time = ctime((long*) &time_);
        printf("Current time is: %s", time);
        break;
    case NAME:
        printf("The name of server is: %s\n", mes->message_.data);
        break;
    case CLIENTS:
        char addr[INET_ADDRSTRLEN];
        struct node { uint16_t index, port; uint32_t ip; };
        struct list_ { uint16_t number; node clients[MAX_CONNECTED_NO]; };
        union { list_ data_; char char_[MAXINFOSIZE]; } client_data;
        memcpy(client_data.char_, mes->message_.data, MAXINFOSIZE);
        printf("Here are %d clients:\n", client_data.data_.number);
        for (int i = 1; i <= client_data.data_.number; i++)
            printf("[Client %d] IP: %s, port: %d\n", \
                client_data.data_.clients[i].index, \
                inet_ntop(AF_INET, &(client_data.data_.clients[i].ip), addr, INET_ADDRSTRLEN), \
                client_data.data_.clients[i].port);
        break;
    case TEXT:
        printf("%s\n", mes->message_.data + 1);
        break;
    default:
        perror("wrong message type");
        exit(1);
    }
}

void exit_(int sockfd)
{
    if (connected) disconnect(sockfd);
    printf("goodbye\n");
    exit(0);
}

int main() {
    int sockfd, sendbytes;
    struct timeval timestamp;
    struct timeval timestamp_end;

    int_handler.sa_handler = handler;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket:");
        exit(1);
    }

    printf("Welcome! Please choose an option:\n");
    while (1)
    {
        char op;
        printf("-------------------------------\n");
        printf("a. connect to a server\n");
        if (connected)
        {
            printf("b. disconnect\n");
            printf("c. get time\n");
            printf("d. get the name of the server\n");
            printf("e. get client list\n");
            printf("f. sent massage to a client\n");
        }
        printf("g. exit\n");

        scanf("%c%*c", &op);
        if (!connected && op != 'a' && op != 'g')
            continue;
        switch (op)
        {
        case 'a':
            connect_(sockfd);
            break;
        case 'b':
            disconnect(sockfd);
            break;
        case 'c':
            request(TIME, sockfd);
            break;
        case 'd':
            request(NAME, sockfd);
            break;
        case 'e':
            request(CLIENTS, sockfd);
            break;
        case 'f':
            request(TEXT, sockfd);
            break;
        case 'g':
            exit_(sockfd);
            break;
        default:
            break;
        }
    }

    return 0;
}
