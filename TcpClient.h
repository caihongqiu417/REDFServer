#ifndef _TCP_CLIENT_
#define _TCP_CLIENT_

#include<stdint.h>
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

        /* 粘包/半包处理：每个客户端独立的接收缓冲区 */
        uint8_t  recv_buf[MSG_HDR_SIZE + MSG_MAX_BODY];
        uint32_t recv_buf_used;   /* 缓冲区中已积累的字节数 */

        TcpClient(uint32_t ip_addr,uint16_t port_no);
        void Display();

};


#endif