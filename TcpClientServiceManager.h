#ifndef _TCPClientServiceManager_
#define _TCPClientServiceManager_

class TcpServerController;
class TcpClient;

class TcpClientServiceManager
{
    private:

    public:
        TcpServerController* tcp_ctrl;

        TcpClientServiceManager(TcpServerController*);
        ~TcpClientServiceManager();

        void StartTcpClientServiceManagerThread();

        void ClientFDStartListen(TcpClient* tcp_client);
};

#endif