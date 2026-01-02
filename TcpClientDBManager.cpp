#include"TcpServerController.h"
#include"TcpClientDBManager.h"
#include"TcpClient.h"

TcpClientDBManager::TcpClientDBManager(TcpServerController* tcp_ctrl)
{
    this->tcp_ctrl=tcp_ctrl;
}

TcpClientDBManager::~TcpClientDBManager()
{

}

void TcpClientDBManager::StartTcpClientDbMgrInit()
{
    
}

void TcpClientDBManager::AddClienttoDB(TcpClient* tcp_client)
{
    this->tcp_client_db.push_back(tcp_client);
}