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
#include <time.h>
#include <vector>
#include <arpa/inet.h>

#include "TcpServerController.h"
#include "TcpClientServiceManager.h"
#include "TcpClient.h"
#include "msg_protocol.h"

/*
 * process_recv: 从客户端 fd 读数据到缓冲区，循环拆包处理，解决粘包与半包。
 *
 * 粘包处理：一次 recv() 可能收到多条消息，while 循环逐条拆解。
 * 半包处理：缓冲区数据不足一条完整消息时 break，等下次 recv() 补全。
 * 消息分流：心跳包内部消化；登录/注册直接查库回复；其余转交上层回调。
 *
 * 参数：
 *   svc_mgr - 服务管理器指针（含用户库、控制器等）
 *   client  - 当前正在读取的客户端对象
 *
 * 返回值与 recv() 语义相同：
 *   >0  本次读取正常（消息回调已在内部触发）
 *    0  对端正常关闭连接
 *   -1  错误（包括协议错误、对端断开等），调用处应清理该客户端
 */
static int process_recv(TcpClientServiceManager *svc_mgr, TcpClient *client)
{
    /* 客户端专属的接收缓冲区及其使用量、总容量 */
    uint8_t  *buf     = client->recv_buf;                // 缓冲区起始地址
    uint32_t &used    = client->recv_buf_used;           // 已使用字节数（引用，修改即改 client）
    uint32_t  buf_cap = (uint32_t)sizeof(client->recv_buf); // 缓冲区总容量

    /* ===== 第 1 步：从 socket 读数据到缓冲区剩余空间 ===== */
    int n = recv(client->comm_fd,                       // 客户端 socket
                 buf + used,                            // 写入位置：已用区域之后
                 buf_cap - used,                        // 最多读：剩余空间
                 0);
    if (n <= 0) return n;                               // 0=对端关闭，-1=错误，直接返回
    used += (uint32_t)n;                                // 更新已用字节数

    /* ===== 第 2 步：循环拆包（一次 recv 可能带回多条完整消息）===== */
    while (used >= MSG_HDR_SIZE)                         // 至少要有 6 字节包头
    {
        /* --- 2a. 解析包头前 4 字节：消息体长度（网络字节序→主机） --- */
        uint32_t body_len;
        memcpy(&body_len, buf, sizeof(uint32_t));
        body_len = ntohl(body_len);

        /* --- 2b. 解析包头第 5-6 字节：消息类型（网络字节序→主机） --- */
        uint16_t msg_type;
        memcpy(&msg_type, buf + sizeof(uint32_t), sizeof(uint16_t));
        msg_type = ntohs(msg_type);

        /* --- 2c. 防御：消息体长度超出协议上限 → 协议错误，断开 --- */
        if (body_len > MSG_MAX_BODY) {
            printf("[process_recv] protocol error: body_len=%u > MAX=%u, fd=%d\n",
                   body_len, MSG_MAX_BODY, client->comm_fd);
            used = 0;                                    // 清空缓冲区，丢弃脏数据
            return -1;                                   // 返回错误，调用处会清理连接
        }

        uint32_t full_len = MSG_HDR_SIZE + body_len;     // 一条完整消息 = 包头 + 包体

        /* --- 2d. 半包检测：数据不够一条完整消息，退出循环等下次 recv --- */
        if (used < full_len) break;

        /* ================================================================
         * 2e. 完整消息就绪，按类型分流处理
         * ================================================================ */

        if (msg_type == MSG_HEARTBEAT) {
            /* ---------- 心跳包：只刷新存活时间，不上报 ---------- */
            client->last_heartbeat_time = time(nullptr);  // 更新最后心跳时间
            printf("[心跳] 收到客户端心跳, fd=%d\n", client->comm_fd);

        } else if (msg_type == MSG_LOGIN_REQ || msg_type == MSG_REG_REQ) {
            /* ---------- 登录/注册：解析 → 查数据库 → 回复 ---------- */
            MsgLoginRsp rsp{};                            // 响应结构体，默认 result=0

            if (body_len < sizeof(MsgLoginReq)) {
                // 包体太短，连请求结构体都装不下
                rsp.result = 0;
                strncpy(rsp.reason, "包体长度错误", sizeof(rsp.reason) - 1);
            } else {
                // 拷贝出请求数据
                MsgLoginReq req{};
                memcpy(&req, buf + MSG_HDR_SIZE, sizeof(MsgLoginReq));

                // 强制末尾置 \0，防止字符串越界
                req.username[MSG_LOGIN_USERNAME_LEN - 1] = '\0';
                req.pw_hash [MSG_LOGIN_PWHASH_LEN  - 1] = '\0';

                std::string uname(req.username);          // 用户名
                std::string pwhash(req.pw_hash);          // SHA-256 哈希字符串

                if (msg_type == MSG_REG_REQ) {
                    /* 注册：向 users 表插入新行 */
                    bool ok = svc_mgr->user_db_.RegisterUser(uname, pwhash);
                    rsp.result = ok ? 1 : 0;
                    strncpy(rsp.reason, ok ? "" : "用户名已存在", sizeof(rsp.reason) - 1);
                    printf("[注册] 用户 %s %s, fd=%d\n", uname.c_str(),
                           ok ? "成功" : "失败", client->comm_fd);
                } else {
                    /* 登录：查 users 表比对哈希值 */
                    bool ok = svc_mgr->user_db_.AuthUser(uname, pwhash);
                    rsp.result = ok ? 1 : 0;
                    strncpy(rsp.reason, ok ? "" : "用户名或密码错误", sizeof(rsp.reason) - 1);
                    if (ok) {
                        // 登录成功：升级认证状态，记录用户名
                        client->auth_state = TcpClient::AuthState::AUTHED;
                        client->username   = uname;
                    }
                    printf("[登陆] 用户 %s %s, fd=%d\n", uname.c_str(),
                           ok ? "成功" : "失败", client->comm_fd);
                }
            }

            // 选择响应类型（注册响应 / 登录响应），发送给客户端
            uint16_t rsp_type = (msg_type == MSG_REG_REQ) ? MSG_REG_RSP : MSG_LOGIN_RSP;
            msg_send(client->comm_fd, (const uint8_t*)&rsp, sizeof(rsp), rsp_type);

        } else if (svc_mgr->tcp_ctrl->client_msg_recvd) {
            /* ---------- 其他业务消息（聊天/加入/离开等） ---------- */
            if (client->auth_state != TcpClient::AuthState::AUTHED) {
                // 安全拦截：未登录不得发送业务消息
                printf("[安全] 未认证客户端发送 msg_type=%u，丢弃, fd=%d\n",
                       msg_type, client->comm_fd);
            } else {
                // 已登录，合法消息，通过控制器回调转交上层处理
                svc_mgr->tcp_ctrl->client_msg_recvd(
                    svc_mgr->tcp_ctrl,                    // 控制器
                    client,                               // 发送者
                    buf + MSG_HDR_SIZE,                   // 消息体起始（跳过包头）
                    (uint16_t)body_len,                   // 消息体长度
                    msg_type                              // 消息类型透传
                );
            }
        }

        /* ===== 第 3 步：将剩余数据前移，准备解下一条（粘包处理）===== */
        used -= full_len;                                 // 减去已消费的字节
        if (used > 0)
            memmove(buf, buf + full_len, used);           // 剩余数据移到缓冲区开头
    }

    return n;                                             // 返回 recv() 的原始返回值
}

TcpClientServiceManager::TcpClientServiceManager(TcpServerController* tcp_ctrl)
{
    // select 相关初始化
    this->tcp_ctrl = tcp_ctrl;
    this->max_fd = 0;
    FD_ZERO(&this->active_fd_set);
    FD_ZERO(&this->backup_fd_set);

    // 动态分配线程 ID（不用栈上局部变量，保证生命周期）
    this->client_svc_mgr_thread = new pthread_t;
    *this->client_svc_mgr_thread = 0;  // 初始化为0

    // 打开用户数据库（文件不存在时自动创建）
    if (!user_db_.Open("users.db")) {
        fprintf(stderr, "[ServiceManager] 警告：用户数据库打开失败！\n");
    }
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

        // 3. 调用select()，设置5秒超时以便定期执行心跳检查
        struct timeval tv;
        tv.tv_sec  = 5;
        tv.tv_usec = 0;
        int ready = select(this->max_fd + 1, &this->active_fd_set, 0, 0, &tv);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select error");
            break;
        }

        // 4. 心跳超时检查：每次 select 返回（含超时）都检查所有客户端
        //    客户端每10秒发一次心跳，30秒内共3次；超过30秒未收到则判定离线
        {
            time_t now = time(nullptr);
            it = this->tcp_client_db.begin();
            while (it != this->tcp_client_db.end()) {
                tcp_client = *it;
                if (now - tcp_client->last_heartbeat_time >= 30) {
                    // 构造离开消息体：优先用昵称，否则用 "ip:port"
                    std::string leave_body;
                    if (!tcp_client->nickname.empty()) {
                        leave_body = tcp_client->nickname;
                    } else {
                        struct in_addr addr;
                        addr.s_addr = tcp_client->ip_addr;
                        char tmp[64];
                        snprintf(tmp, sizeof(tmp), "%s:%d",
                                 inet_ntoa(addr), ntohs(tcp_client->port_no));
                        leave_body = tmp;
                    }
                    printf("[心跳] 客户端 %s 心跳超时（%lds），主动移除并广播离开\n",
                           leave_body.c_str(), (long)(now - tcp_client->last_heartbeat_time));

                    // 广播 MSG_LEAVE，通知其他客户端此人离线
                    Broadcast((const uint8_t*)leave_body.data(),
                              (uint32_t)leave_body.size(),
                              MSG_LEAVE, tcp_client);

                    FD_CLR(tcp_client->comm_fd, &this->backup_fd_set);
                    close(tcp_client->comm_fd);
                    it = this->tcp_client_db.erase(it);
                    continue;
                }
                ++it;
            }
        }

        if (ready == 0) {
            continue;  // 纯超时，无可读 fd，回到循环顶
        }

        // 5. 遍历所有客户端，处理可读 fd
        it = this->tcp_client_db.begin();
        while (it != this->tcp_client_db.end())
        {
            tcp_client = *it;

            // 检查socket是否就绪
            if (FD_ISSET(tcp_client->comm_fd, &this->active_fd_set))
            {
                /* 使用协议解包替代裸 recv()，自动处理粘包与半包 */
                rcv_bytes = process_recv(this, tcp_client);

                // 处理接收结果（消息回调已在 process_recv 内部触发）
                if (rcv_bytes > 0) {
                    /* 数据正常处理，无需额外操作 */
                }
                else if (rcv_bytes == 0 ||
                         (rcv_bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    // 客户端断开（正常关闭或错误），广播 MSG_LEAVE 通知其他人
                    printf("Client disconnected, fd=%d\n", tcp_client->comm_fd);

                    std::string leave_body;
                    if (!tcp_client->nickname.empty()) {
                        leave_body = tcp_client->nickname;
                    } else {
                        struct in_addr addr;
                        addr.s_addr = tcp_client->ip_addr;
                        char tmp[64];
                        snprintf(tmp, sizeof(tmp), "%s:%d",
                                 inet_ntoa(addr), ntohs(tcp_client->port_no));
                        leave_body = tmp;
                    }
                    Broadcast((const uint8_t*)leave_body.data(),
                              (uint32_t)leave_body.size(),
                              MSG_LEAVE, tcp_client);

                    FD_CLR(tcp_client->comm_fd, &this->backup_fd_set);
                    close(tcp_client->comm_fd);
                    it = this->tcp_client_db.erase(it);
                    continue;
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
    /* 不设置 DETACHED，保持默认 JOINABLE，以便 StopXxx 中 pthread_join 能正常等待 */

    if (pthread_create(this->client_svc_mgr_thread,
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
    /* 复用构造函数 new 出来的指针，不再 calloc（否则 delete 与 calloc 混用是 UB） */
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

// 向单个客户端发送一条完整消息（msg_send 负责写 6字节包头 + 包体）
void TcpClientServiceManager::SendToClient(TcpClient* client,
                                           const uint8_t* body,
                                           uint32_t len,
                                           uint16_t msg_type)
{
    if (!client || client->comm_fd < 0) return;
    printf("[SendToClient] 尝试发送 %u 字节(type=%u) 到 fd=%d\n", len, msg_type, client->comm_fd);
    int rc = msg_send(client->comm_fd, body, len, msg_type);
    if (rc < 0)
        printf("[SendToClient] 发送失败, fd=%d, errno=%d\n", client->comm_fd, errno);
    else
        printf("[SendToClient] 发送成功, fd=%d\n", client->comm_fd);
}

// 遍历 tcp_client_db，跳过发送者(exclude)，逐个转发消息
// 此函数在 client_msg_recvd 回调内被调用，与 select 线程同一上下文，无需加锁
void TcpClientServiceManager::Broadcast(const uint8_t* body,
                                        uint32_t len,
                                        uint16_t msg_type,
                                        const TcpClient* exclude)
{
    int total = 0, sent = 0;
    for (auto* target : tcp_client_db) {
        total++;
        if (target == exclude) continue;
        sent++;
        SendToClient(target, body, len, msg_type);
    }
    printf("[Broadcast] DB中共 %d 个客户端，发送给 %d 个（排除发送者 fd=%d）\n",
           total, sent, exclude ? exclude->comm_fd : -1);
}

/*
 * SendPlayerListTo: 将当前在线玩家列表打包为 MSG_PLAYER_LIST 发送给新客户端。
 *
 * body格式：[num: uint16_t 大端] 重复num次 [len: uint16_t 大端][str: len字节]
 * str 内容："ip:port"，如 "127.0.0.1:40001"
 *
 * 在 client_connected 回调中调用，此时 new_client 尚未加入 tcp_client_db，
 * 所以列表里只含已在线的旧玩家，语义正确。
 */
void TcpClientServiceManager::SendPlayerListTo(const TcpClient* new_client)
{
    if (!new_client || new_client->comm_fd < 0) return;

    std::vector<uint8_t> payload;

    /* 先写占位的 num_players，稍后回填 */
    uint16_t count = 0;
    payload.push_back(0); payload.push_back(0);   /* 2 字节占位 */

    char entry[256];
    for (auto* c : tcp_client_db) {
        if (c->nickname.empty()) continue;  /* 未 Join 的客户端不发，避免 ip:port 污染列表 */
        snprintf(entry, sizeof(entry), "%s", c->nickname.c_str());

        uint16_t slen = (uint16_t)strlen(entry);
        uint16_t net_slen = htons(slen);
        payload.push_back((uint8_t)(net_slen >> 8));
        payload.push_back((uint8_t)(net_slen & 0xFF));
        payload.insert(payload.end(), (uint8_t*)entry, (uint8_t*)entry + slen);
        count++;
    }

    /* 回填 num_players */
    uint16_t net_count = htons(count);
    payload[0] = (uint8_t)(net_count >> 8);
    payload[1] = (uint8_t)(net_count & 0xFF);

    printf("[SendPlayerListTo] 发送玩家列表给 fd=%d，当前在线 %u 人\n",
           new_client->comm_fd, count);

    int rc = msg_send(new_client->comm_fd, payload.data(), (uint32_t)payload.size(), MSG_PLAYER_LIST);
    if (rc < 0)
        printf("[SendPlayerListTo] 发送失败, fd=%d\n", new_client->comm_fd);
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