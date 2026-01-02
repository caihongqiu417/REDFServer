#include <iostream>
#include <unistd.h>
#include "TcpServerController.h"
#include "TcpClient.h"
#include"network_utils.h"
#include<arpa/inet.h>

/* 打印TCP客户端信息 */
// testapp.cpp 中正确显示
static void print_client(const TcpClient* tcp_client)
{
    printf("[%s,%d]\n", 
           network_convert_ip_n_to_p(tcp_client->ip_addr, 0),
           ntohs(tcp_client->port_no));  // 正确：网络转主机字节序
}


/* 打印TCP服务器信息 */  
static void print_server(const TcpServerController* tcp_server)
{
    printf("[%s,%d]\n", 
           // IP：网络字节序转点分十进制字符串
           network_convert_ip_n_to_p(tcp_server->ip_addr, 0),
           // 端口：网络字节序转主机字节序（ntohs）
           ntohs(tcp_server->port_no));
}

/*Callback Fn*/
static void appln_client_connected(const TcpServerController* tcp_server,const TcpClient* tcp_client)
{
    print_server(tcp_server);
    printf("Appln:Client connected:");
    print_client(tcp_client);
}

static void appln_client_disconnected(const TcpServerController* tcp_server,const TcpClient* tcp_client)
{
    
}

static void appln_client_msg_recvd(const TcpServerController* tcp_server,const TcpClient* tcp_client,unsigned char* msg,uint16_t msg_size)
{
    
}

/*Application exe*/
int main()
{
    TcpServerController* server = new TcpServerController("127.0.0.1", 4000, "Redflag TCP Server");
    
    
    //using the tcp library to register callbacks
    server->SetServerNotiCallbacks(appln_client_connected,appln_client_disconnected,appln_client_msg_recvd);

    server->Start();  
    

    while(1) 
    {
        sleep(1);
    }

    return 0;
}
