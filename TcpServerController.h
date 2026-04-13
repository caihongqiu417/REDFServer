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
    /* 回调函数指针：
     * 底层收到完整消息后，通过这个指针调用应用层（testapp.cpp）的处理函数。
     * 参数: server指针, 发送方客户端, 消息体, 消息体长度, 消息类型(MsgType枚举) */
    void(*client_msg_recvd)(const TcpServerController*,const TcpClient*,unsigned char*,uint16_t,uint16_t);

    void SetServerNotiCallbacks(
        void(*client_connected)(const TcpServerController*,const TcpClient*),
        void(*client_disconnected)(const TcpServerController*,const TcpClient*),
        void(*client_msg_recvd)(const TcpServerController*,const TcpClient*,unsigned char*,uint16_t,uint16_t)
    );

    TcpServerController(std::string ip_addr, uint16_t port_no, std::string name);
    ~TcpServerController();
    void Start();
    void Stop();

    void ProcessNewClient(TcpClient* tcp_client);

    void Display();

    // 广播给所有客户端，exclude 为发送者（传 nullptr 则广播给所有人）
    // msg_type: 转发时保持与原消息相同的类型
    // const：不修改服务器自身状态，可在 const TcpServerController* 回调中直接调用
    void Broadcast(const uint8_t* body, uint32_t len, uint16_t msg_type, const TcpClient* exclude) const;

    // 向新连入的客户端发送当前在线玩家列表（MSG_PLAYER_LIST）
    // 在 client_connected 回调中调用；此时 new_client 尚未进入服务管理器 DB
    void SendPlayerListTo(const TcpClient* new_client) const;
};

#endif
