#include <sys/socket.h>
#include <netinet/in.h>
#include <memory.h>
#include <errno.h>
#include <cstdlib>
#include <unistd.h>
#include <cstdio>

#include "TcpNewConnectionAcceptor.h"
#include "TcpServerController.h"
#include "network_utils.h"
#include "TcpClient.h"

TcpNewConnectionAcceptor::TcpNewConnectionAcceptor(TcpServerController* tcp_ctrl)
{
    // 创建用于接受新连接的 socket 文件描述符
    this->accept_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(this->accept_fd < 0)
    {
        printf("Error: Could not create AcceptFD! Error: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    this->accept_new_conn_thread = (pthread_t*)calloc(1, sizeof(pthread_t));
    this->tcp_ctrl = tcp_ctrl;
}

TcpNewConnectionAcceptor::~TcpNewConnectionAcceptor()
{
    if (accept_fd >= 0) {
        close(accept_fd);
    }
    if (accept_new_conn_thread) {
        free(accept_new_conn_thread);
    }
}

void TcpNewConnectionAcceptor::StartTcpNewConnectionAcceptorThreadInterval()
{
    int opt = 1;  // 端口快速重用选项
    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = this->tcp_ctrl->port_no;  // 已经是网络字节序
    server_addr.sin_addr.s_addr = this->tcp_ctrl->ip_addr;  // 已经是网络字节序

    // 设置套接字选项，允许端口快速重用
    if(setsockopt(this->accept_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) < 0)
    {
        printf("SetSocketopt Failed! Error: %d\n", errno);
        close(accept_fd);
        exit(EXIT_FAILURE);
    }

    // 绑定套接字到指定地址和端口
    if(bind(this->accept_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("Error: Acceptor socket bind failed [%s:%d], error=%d\n",
               network_convert_ip_n_to_p(this->tcp_ctrl->ip_addr, 0),
               ntohs(this->tcp_ctrl->port_no),
               errno);
        close(accept_fd);
        exit(EXIT_FAILURE);
    }

    // 开始监听连接请求
    if(listen(this->accept_fd, 5) < 0)
    {
        printf("listen failed, error: %d\n", errno);
        close(accept_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on [%s:%d]\n",
           network_convert_ip_n_to_p(this->tcp_ctrl->ip_addr, 0),
           ntohs(this->tcp_ctrl->port_no));

    while(true)
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int comm_sock_fd = accept(this->accept_fd, (struct sockaddr*)&client_addr, &addr_len);

        if(comm_sock_fd < 0)
        {
            printf("Error in Accepting New Connection: %d\n", errno);
            continue;
        }

        TcpClient* tcp_client = new TcpClient(client_addr.sin_addr.s_addr, client_addr.sin_port);
        tcp_client->tcp_ctrlr = this->tcp_ctrl;
        tcp_client->comm_fd = comm_sock_fd;

        // 回调应用程序
        if(this->tcp_ctrl->client_connected)
        {
            this->tcp_ctrl->client_connected(this->tcp_ctrl, tcp_client);
        }

        // 告诉 TcpController 进一步处理客户端
        this->tcp_ctrl->ProcessNewClient(tcp_client);

        printf("Connection Accepted from Client [%s,%d]\n",
               network_convert_ip_n_to_p(client_addr.sin_addr.s_addr, 0),
               ntohs(client_addr.sin_port));
    }
}

static void* tcp_listen_for_new_connections(void* arg)
{
    TcpNewConnectionAcceptor* tcp_new_conn_acc = (TcpNewConnectionAcceptor*)arg;
    tcp_new_conn_acc->StartTcpNewConnectionAcceptorThreadInterval();
    return NULL;
}

void TcpNewConnectionAcceptor::StartTcpNewConnectionAcceptorThread()
{
    if(pthread_create(this->accept_new_conn_thread,
                      NULL,
                      tcp_listen_for_new_connections,
                      (void*)this) != 0)
    {
        printf("%s(): Thread Creation failed, error=%d\n",
               __FUNCTION__, errno);
        exit(EXIT_FAILURE);
    }

    printf("Service Started: TcpNewConnectionAcceptorThread\n");
}