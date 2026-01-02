#ifndef _TCPSERVERCONTROLLER_
#define _TCPSERVERCONTROLLER_

#include<stdio.h>
#include<string>
#include<cstdint>

class TcpNewConnectionAcceptor;
class TcpClientServiceManager;
class TcpClientDBManager;
class TcpClient;

class TcpServerController
{
private:
    TcpNewConnectionAcceptor *tcp_new_conn_acc;
    TcpClientDBManager *tcp_client_db_mgr;
    TcpClientServiceManager *tcp_client_svc_mgr;

public:
    uint32_t ip_addr;
    uint16_t port_no;  // 存储为网络字节序
    std::string name;

    /*Set callbacks*/
    void(*client_connected)(const TcpServerController*,const TcpClient*);
    void(*client_disconnected)(const TcpServerController*,const TcpClient*);
    void(*client_msg_recvd)(const TcpServerController*,const TcpClient*,unsigned char*,uint16_t);

    void SetServerNotiCallbacks(
        void(*client_connected)(const TcpServerController*,const TcpClient*),
        void(*client_disconnected)(const TcpServerController*,const TcpClient*),
        void(*client_msg_recvd)(const TcpServerController*,const TcpClient*,unsigned char*,uint16_t)
    );

    TcpServerController(std::string ip_addr, uint16_t port_no, std::string name);
    ~TcpServerController();
    void Start();
    void Stop();

    void ProcessNewClient(TcpClient* tcp_client);
};

#endif
