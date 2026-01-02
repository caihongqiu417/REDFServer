#include"TcpServerController.h"
#include"TcpClientServiceManager.h"

TcpClientServiceManager::TcpClientServiceManager(TcpServerController* tcp_ctrl)
{
    this->tcp_ctrl=tcp_ctrl;
}

TcpClientServiceManager::~TcpClientServiceManager()
{

}

void TcpClientServiceManager::StartTcpClientServiceManagerThread()
{

}

void TcpClientServiceManager::ClientFDStartListen(TcpClient* tcp_client)
{
    
}