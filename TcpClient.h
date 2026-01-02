#ifndef _TCP_CLIENT_
#define _TCP_CLIENT_

#include<stdint.h>

// TCP服务器中表示一个客户端连接的数据类
class TcpServerController;

class TcpClient{
    private:
    public:
        uint32_t ip_addr;
        uint16_t port_no;

        int comm_fd;
        TcpServerController* tcp_ctrlr;
        TcpClient(uint32_t ip_addr,uint16_t port_no);

};


#endif