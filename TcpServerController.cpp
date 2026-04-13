#include<stdio.h>
#include<assert.h>
#include"TcpServerController.h"
#include"network_utils.h"
#include"TcpNewConnectionAcceptor.h"
#include"TcpClientServiceManager.h"
#include"TcpClientDBManager.h"
#include <arpa/inet.h> 

// TCP服务器控制器构造函数：初始化服务器配置并创建各组件实例
TcpServerController::TcpServerController(std::string ip_addr, uint16_t port_no, std::string name)
{
    this->ip_addr = inet_addr(ip_addr.c_str());
    this->port_no = htons(port_no);  // 转换为网络字节序存储
    this->name = name;

    this->tcp_new_conn_acc = new TcpNewConnectionAcceptor(this);
    this->tcp_client_db_mgr = new TcpClientDBManager(this);
    this->tcp_client_svc_mgr = new TcpClientServiceManager(this);
}

// 启动服务器：启动所有组件线程并打印运行状态
void TcpServerController::Start()
{
    /*Start the CRS thread
    Start the DRS thread
    initialize the DBMS*/

    this->tcp_new_conn_acc->StartTcpNewConnectionAcceptorThread();
    this->tcp_client_svc_mgr->StartTcpClientServiceManagerThread();
    this->tcp_client_db_mgr->StartTcpClientDbMgrInit();

    printf("Redflag Tcp Server is Up and Running[%s,%d]\nOK.\n",
        network_convert_ip_n_to_p(this->ip_addr, 0),
        ntohs(this->port_no));  // 转换回主机字节序显示
}

// 处理新客户端连接：将其注册到数据库并开始监听数据
void TcpServerController::ProcessNewClient(TcpClient* tcp_client)
{
    this->tcp_client_db_mgr->AddClienttoDB(tcp_client);
    this->tcp_client_svc_mgr->ClientFDStartListen(tcp_client);
}

//The application would make use of this function to register its local functions
void TcpServerController::SetServerNotiCallbacks(
    void(*client_connected)(const TcpServerController*,const TcpClient*),
    void(*client_disconnected)(const TcpServerController*,const TcpClient*),
    void(*client_msg_recvd)(const TcpServerController*,const TcpClient*,unsigned char*,uint16_t,uint16_t))
    {
        this->client_connected = client_connected;
        this->client_disconnected = client_disconnected;
        this->client_msg_recvd = client_msg_recvd;
        /* 回调注册原理：
         * 这里把 testapp.cpp 中定义的函数地址存入成员变量。
         * 之后底层事件发生时，通过 tcp_ctrl->client_msg_recvd(...) 间接调用它。
         * 这就是 C 语言风格的"回调"——函数指针 + 事件驱动。 */
    }

// 析构函数（如果需要）
TcpServerController::~TcpServerController()
{
    // 清理资源
    if (tcp_new_conn_acc) delete tcp_new_conn_acc;
    if (tcp_client_db_mgr) delete tcp_client_db_mgr;
    if (tcp_client_svc_mgr) delete tcp_client_svc_mgr;
}

// 停止服务器（如果需要）
void TcpServerController::Stop()
{
    // 实现停止服务器的逻辑
}

void TcpServerController::Broadcast(const uint8_t* body, uint32_t len, uint16_t msg_type, const TcpClient* exclude) const
{
    if (tcp_client_svc_mgr)
        tcp_client_svc_mgr->Broadcast(body, len, msg_type, exclude);
}

void TcpServerController::SendPlayerListTo(const TcpClient* new_client) const
{
    if (tcp_client_svc_mgr)
        tcp_client_svc_mgr->SendPlayerListTo(new_client);
}

void TcpServerController::Display()
{
    printf("Server Name:%s\n", this->name.c_str());
    printf("Listening on:[%s,%d]\n",
           network_convert_ip_n_to_p(this->ip_addr, 0),  // ✓ IP转换
           ntohs(this->port_no));                        // ✓ 端口转换
    
    this->tcp_client_db_mgr->DisplayClientDB();
}