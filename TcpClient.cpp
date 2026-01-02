#include "TcpClient.h"

// TcpClient.cpp
TcpClient::TcpClient(uint32_t ip_addr, uint16_t port_no)
{
    this->ip_addr = ip_addr;  // 已经是网络字节序
    this->port_no = port_no;  // 已经是网络字节序
}