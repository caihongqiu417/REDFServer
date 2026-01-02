#ifndef _TcpClientDBManager_
#define _TcpClientDBManager_

#include<list>

class TcpClient;
class TcpServerController;

class TcpClientDBManager
{
    private:
        std::list<TcpClient*> tcp_client_db;

    public:
        TcpServerController* tcp_ctrl;

        TcpClientDBManager(TcpServerController*);
        ~TcpClientDBManager();

        void StartTcpClientDbMgrInit();
        void AddClienttoDB(TcpClient* tcp_client);
};


#endif 