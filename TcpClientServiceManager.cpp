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
#include <arpa/inet.h>

#include "TcpServerController.h"
#include "TcpClientServiceManager.h"
#include "TcpClient.h"
#include "msg_protocol.h"

/*
 * process_recv: 从 tcp_client 的 fd 读数据，写入 per-client 缓冲区，
 *               循环解包并回调，处理粘包与半包。
 * 返回值与 recv() 语义相同：>0 正常，0 对端关闭，-1 错误。
 */
static int process_recv(TcpClientServiceManager *svc_mgr, TcpClient *client)
{
    uint8_t  *buf     = client->recv_buf;
    uint32_t &used    = client->recv_buf_used;
    uint32_t  buf_cap = (uint32_t)sizeof(client->recv_buf);

    /* 1. 读取数据到缓冲区剩余空间 */
    int n = recv(client->comm_fd, buf + used, buf_cap - used, 0);
    if (n <= 0) return n;   /* 0=对端关闭，-1=错误，交给调用处处理 */
    used += (uint32_t)n;

    /* 2. 循环解包（一次 recv 可能带来多条完整消息） */
    while (used >= MSG_HDR_SIZE)
    {
        /* 2a. 读包头，获取消息体长度 */
        uint32_t body_len;
        memcpy(&body_len, buf, MSG_HDR_SIZE);
        body_len = ntohl(body_len);   /* 网络字节序 → 主机字节序 */

        /* 防御：包体长度超出协议上限 → 协议错误，断开 */
        if (body_len > MSG_MAX_BODY) {
            printf("[process_recv] protocol error: body_len=%u > MAX=%u, fd=%d\n",
                   body_len, MSG_MAX_BODY, client->comm_fd);
            used = 0;
            return -1;
        }

        uint32_t full_len = MSG_HDR_SIZE + body_len;

        /* 2b. 半包：完整消息尚未到达，等下次 recv */
        if (used < full_len) break;

        /* 2c. 完整消息就绪，回调上层 */
        if (svc_mgr->tcp_ctrl->client_msg_recvd) {
            svc_mgr->tcp_ctrl->client_msg_recvd(
                svc_mgr->tcp_ctrl,
                client,
                buf + MSG_HDR_SIZE,    /* 只传消息体，不含包头 */
                (uint16_t)body_len
            );
        }

        /* 2d. 将剩余数据前移（处理粘包：同一次 recv 带来多条消息） */
        used -= full_len;
        if (used > 0)
            memmove(buf, buf + full_len, used);
    }

    return n;   /* 正数：本次 recv 正常 */
}

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

                /* 使用协议解包替代裸 recv()，自动处理粘包与半包 */
                rcv_bytes = process_recv(this, tcp_client);

                // 处理接收结果（消息回调已在 process_recv 内部触发）
                if (rcv_bytes > 0) {
                    /* 数据正常处理，无需额外操作 */
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

// 向单个客户端发送一条完整消息（msg_send 负责写包头+包体）
void TcpClientServiceManager::SendToClient(TcpClient* client,
                                           const uint8_t* body,
                                           uint32_t len)
{
    if (!client || client->comm_fd < 0) return;
    printf("[SendToClient] 尝试发送 %u 字节到 fd=%d\n", len, client->comm_fd);
    int rc = msg_send(client->comm_fd, body, len);
    if (rc < 0)
        printf("[SendToClient] 发送失败, fd=%d, errno=%d\n", client->comm_fd, errno);
    else
        printf("[SendToClient] 发送成功, fd=%d\n", client->comm_fd);
}

// 遍历 tcp_client_db，跳过发送者(exclude)，逐个转发消息
// 此函数在 client_msg_recvd 回调内被调用，与 select 线程同一上下文，无需加锁
void TcpClientServiceManager::Broadcast(const uint8_t* body,
                                        uint32_t len,
                                        const TcpClient* exclude)
{
    int total = 0, sent = 0;
    for (auto* target : tcp_client_db) {
        total++;
        if (target == exclude) continue;
        sent++;
        SendToClient(target, body, len);
    }
    printf("[Broadcast] DB中共 %d 个客户端，发送给 %d 个（排除发送者 fd=%d）\n",
           total, sent, exclude ? exclude->comm_fd : -1);
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