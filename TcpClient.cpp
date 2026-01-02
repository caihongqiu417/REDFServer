#include "TcpClient.h"
#include"stdio.h"
#include"network_utils.h"
#include<arpa/inet.h>

// TcpClient.cpp
TcpClient::TcpClient(uint32_t ip_addr, uint16_t port_no)
{
    this->ip_addr = ip_addr;  // 已经是网络字节序
    this->port_no = port_no;  // 已经是网络字节序
}

void TcpClient::Display()
{
    printf("Tcp Client:[%s,%d]\n",
           network_convert_ip_n_to_p(this->ip_addr, 0),  // ✓ 直接传入网络字节序
           ntohs(this->port_no));                        // ✓ 网络→主机字节序
}