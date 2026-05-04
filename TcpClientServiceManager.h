#ifndef _TCPClientServiceManager_
#define _TCPClientServiceManager_

#include <pthread.h>
#include <list> 
#include <sys/select.h>
#include "UserDatabase.h"

class TcpServerController;
class TcpClient;

class TcpClientServiceManager
{
     private:
        int max_fd;                         // select 用的最大 fd 编号
        fd_set active_fd_set;               // select 返回后有事件的 fd 集合
        fd_set backup_fd_set;               // 完整 fd 集合的备份，每轮 select 前恢复
        pthread_t* client_svc_mgr_thread;   // 运行管理主循环的线程
        
        std::list<TcpClient*> tcp_client_db;// 所有在线客户端的链表
        
    public:
        UserDatabase user_db_;              // 用户账号数据库（注册/登录用）
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
        /* 向新连入的客户端发送当前在线玩家列表（MSG_PLAYER_LIST） */
        void SendPlayerListTo(const TcpClient* new_client);

};

#endif