#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <memory.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <vector>

#include "TcpServerController.h"
#include "TcpClientServiceManager.h"
#include "TcpClient.h" 

#define TCP_CLIENT_RECV_BUFFER_SIZE 1024

TcpClientServiceManager::TcpClientServiceManager(TcpServerController* tcp_ctrl)
{
    this->tcp_ctrl = tcp_ctrl;
    this->max_fd = 0;
    FD_ZERO(&this->active_fd_set);
    FD_ZERO(&this->backup_fd_set);

    // ✅ 改为动态分配
    this->client_svc_mgr_thread = new pthread_t;
    *this->client_svc_mgr_thread = 0;  // 初始化为0
}

TcpClientServiceManager::~TcpClientServiceManager()
{
    // 停止线程（如果正在运行）
    StopTcpClientServiceManagerThread();
    
    // ✅ 释放动态分配的内存
    if (client_svc_mgr_thread) {
        delete client_svc_mgr_thread;
        client_svc_mgr_thread = nullptr;
    }
}

void* TcpClientServiceManager::tcp_client_svc_manager_thread_fn(void* arg)
{
    TcpClientServiceManager *svc_mgr = (TcpClientServiceManager *)arg;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED,NULL);
    svc_mgr->StartTcpClientServiceManagerThreadInternal();
    return NULL;
}


void TcpClientServiceManager::StartTcpClientServiceManagerThreadInternal()
{
    int rcv_bytes;
    TcpClient* tcp_client;
    std::list<TcpClient*>::iterator it;

    printf("TcpClientServiceManager thread started\n");

    while(true)
    {
        // 1. 重新计算max_fd并更新backup_fd_set
        this->max_fd = GetMaxFd();
        FD_ZERO(&this->backup_fd_set);
        CopyClientFDtoFDSet(&this->backup_fd_set);
        
        // 2. 将备份的fd_set复制到活动fd_set中
        memcpy(&this->active_fd_set, &this->backup_fd_set, sizeof(fd_set));
        
        // 3. 调用select()系统调用
        int ready = select(this->max_fd + 1, &this->active_fd_set, 0, 0, 0);
        
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select error");
            break;
        }
        
        if (ready == 0) {
            continue;
        }
        
        // 4. 遍历所有客户端
        it = this->tcp_client_db.begin();
        while (it != this->tcp_client_db.end())
        {
            tcp_client = *it;
            
            // 5. 检查socket是否就绪
            if (FD_ISSET(tcp_client->comm_fd, &this->active_fd_set))
            {
                // 使用简单的栈缓冲区
                char buffer[1024];
                
                // 使用 recv() 而不是 recvfrom()
                rcv_bytes = recv(tcp_client->comm_fd, buffer, sizeof(buffer), 0);
                
                // 处理接收结果
                if (rcv_bytes > 0) {
                    // 调用消息接收回调
                    if(this->tcp_ctrl->client_msg_recvd)
                    {
                        this->tcp_ctrl->client_msg_recvd(this->tcp_ctrl, 
                                                       tcp_client, 
                                                       (unsigned char*)buffer, 
                                                       rcv_bytes);
                    }
                }
                else if (rcv_bytes == 0) {
                    // 客户端断开连接
                    printf("Client disconnected, fd=%d\n", tcp_client->comm_fd);
                    
                    FD_CLR(tcp_client->comm_fd, &this->backup_fd_set);
                    close(tcp_client->comm_fd);
                    
                    // 从列表中移除
                    it = this->tcp_client_db.erase(it);
                    continue;
                }
                else {
                    // 接收错误
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        printf("Recv error on fd %d: %s\n", 
                               tcp_client->comm_fd, strerror(errno));
                        
                        FD_CLR(tcp_client->comm_fd, &this->backup_fd_set);
                        close(tcp_client->comm_fd);
                        
                        // 从列表中移除
                        it = this->tcp_client_db.erase(it);
                        continue;
                    }
                }
            }
            
            ++it;
        }
    }
}

void TcpClientServiceManager::StartTcpClientServiceManagerThread()
{
    // 检查线程是否已经在运行
    if (client_svc_mgr_thread && *client_svc_mgr_thread != 0) {
        printf("TcpClientServiceManagerThread is already running\n");
        return;
    }
    
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    // ✅ 现在传递指针
    if (pthread_create(this->client_svc_mgr_thread,  // 直接传递指针，不需要&
                      &attr,
                      tcp_client_svc_manager_thread_fn,
                      (void*)this) == 0) {
        printf("Service started: TcpClientServiceManagerThread\n");
    } else {
        printf("Failed to start TcpClientServiceManagerThread: %s\n", 
               strerror(errno));
        *client_svc_mgr_thread = 0;  // 确保标记为未运行
    }
    
    pthread_attr_destroy(&attr);
}

void TcpClientServiceManager::ClientFDStartListen(TcpClient* tcp_client)
{
    StopTcpClientServiceManagerThread();
    printf("ClientSvcMgr Thread is cancelled because of adding new ClientFD\n");

    AddClientToDB(tcp_client);
    client_svc_mgr_thread=(pthread_t*)calloc(1,sizeof(pthread_t));
    StartTcpClientServiceManagerThread();
    // 将客户端添加到自己的数据库
    AddClientToDB(tcp_client);
}

int TcpClientServiceManager::GetMaxFd() {
    int max_fd_lcl = 0;
    TcpClient* tcp_client;
    std::list<TcpClient *>::iterator it;

    for (it = this->tcp_client_db.begin(); it != this->tcp_client_db.end(); ++it) 
    {
        tcp_client = *it;
        if (tcp_client->comm_fd > max_fd_lcl) {
            max_fd_lcl = tcp_client->comm_fd;
        }
    }
    return max_fd_lcl;
}

void TcpClientServiceManager::CopyClientFDtoFDSet(fd_set *fdset) {
    TcpClient *tcp_client;
    std::list<TcpClient *>::iterator it;

    for (it = this->tcp_client_db.begin(); 
         it != this->tcp_client_db.end();
         ++it) {
        tcp_client = *it;
        FD_SET(tcp_client->comm_fd, fdset);
    }
}

TcpClient* TcpClientServiceManager::LookUpClientDB(uint32_t ip_addr, uint16_t port_no) 
{
    TcpClient *tcp_client;
    std::list<TcpClient *>::iterator it;
    for (it = this->tcp_client_db.begin(); it != this->tcp_client_db.end(); ++it) 
    {
        tcp_client = *it;
        if (tcp_client->ip_addr == ip_addr &&
            tcp_client->port_no == port_no) {
            return tcp_client;
        }
    }
    return NULL;
}

void TcpClientServiceManager::AddClientToDB(TcpClient *tcp_client)
{
    this->tcp_client_db.push_back(tcp_client);
    printf("Client added to service manager: fd=%d\n", tcp_client->comm_fd);
}

void TcpClientServiceManager::StopTcpClientServiceManagerThread()
{
    if (client_svc_mgr_thread && *client_svc_mgr_thread != 0) {
        // ✅ 取消线程
        pthread_cancel(*client_svc_mgr_thread);  // 解引用指针
        
        // ✅ 等待线程结束
        pthread_join(*client_svc_mgr_thread, NULL);  // 解引用指针
        
        // ✅ 重置线程ID为0
        *client_svc_mgr_thread = 0;
        
        printf("TcpClientServiceManagerThread stopped\n");
    } else {
        printf("TcpClientServiceManagerThread is not running\n");
    }
}