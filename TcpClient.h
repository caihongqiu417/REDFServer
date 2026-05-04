#ifndef _TCP_CLIENT_
#define _TCP_CLIENT_

#include<stdint.h>
#include<string>
#include<time.h>
#include "msg_protocol.h"

// TCP服务器中表示一个客户端连接的数据类
class TcpServerController;

class TcpClient{
    private:
    public:
        uint32_t ip_addr;
        uint16_t port_no;

        int comm_fd;
        TcpServerController* tcp_ctrlr;

        /* 客户端通过 MSG_JOIN 设置的昵称，服务器收到后存此处
         * mutable 允许在 const TcpClient* 回调中写入 */
        mutable std::string nickname;

        /* 登陆成功后记录的账号名（区别于昵称 nickname） */
        mutable std::string username;


        /* 心跳检活：上次收到心跳的时间（连接时初始化，每收到 MSG_HEARTBEAT 刷新）
         * 服务器每隔约 5s 检查一次；超过 30s 未收到心跳则视为超时，主动移除客户端 */
        time_t last_heartbeat_time;

        /* 登陆认证状态：连接建立后默认 PENDING，登陆成功后变 AUTHED */
        enum class AuthState{PENDING,AUTHED};
        mutable AuthState auth_state = AuthState::PENDING;


        /* 粘包/半包处理：每个客户端独立的接收缓冲区 */
        uint8_t  recv_buf[MSG_HDR_SIZE + MSG_MAX_BODY];
        uint32_t recv_buf_used;   /* 缓冲区中已积累的字节数 */

        TcpClient(uint32_t ip_addr,uint16_t port_no);
        void Display();

};


#endif