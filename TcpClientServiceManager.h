#ifndef _TCPClientServiceManager_
#define _TCPClientServiceManager_

#include <pthread.h>
#include <list> 
#include <sys/select.h>

class TcpServerController;
class TcpClient;

class TcpClientServiceManager
{
    private:
        int max_fd;
        fd_set active_fd_set;
        fd_set backup_fd_set;
        pthread_t* client_svc_mgr_thread;  // ✅ 改为指针类型
        std::list<TcpClient*> tcp_client_db;
        
    public:
        TcpServerController* tcp_ctrl;

        TcpClientServiceManager(TcpServerController*);
        ~TcpClientServiceManager();

        void StartTcpClientServiceManagerThread();
        void StartTcpClientServiceManagerThreadInternal();
        static void* tcp_client_svc_manager_thread_fn(void* arg);
        void StopTcpClientServiceManagerThread();

        void ClientFDStartListen(TcpClient* tcp_client);

        int GetMaxFd();
        void CopyClientFDtoFDSet(fd_set *fdset);
        TcpClient* LookUpClientDB(uint32_t ip_addr, uint16_t port_no);
        void AddClientToDB(TcpClient *tcp_client);

        void SendToClient(TcpClient* client, const uint8_t* body, uint32_t len, uint16_t msg_type);
        void Broadcast(const uint8_t* body, uint32_t len, uint16_t msg_type, const TcpClient* exclude);

};

#endif