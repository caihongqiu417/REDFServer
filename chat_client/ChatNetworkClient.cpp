#include "ChatNetworkClient.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <openssl/sha.h>

ChatNetworkClient::ChatNetworkClient()
    : sock_fd_(-1), connected_(false), stop_recv_(false), recv_buf_used_(0),
      stop_hb_(true), hb_started_(false)
{
    memset(recv_buf_, 0, sizeof(recv_buf_));
}

ChatNetworkClient::~ChatNetworkClient()
{
    Disconnect();
}

bool ChatNetworkClient::Connect(const std::string& host, uint16_t port)
{
    if (connected_.load()) Disconnect();

    sock_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_fd_ < 0) {
        perror("[ChatNet] socket");
        return false;
    }

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = inet_addr(host.c_str());

    if (connect(sock_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[ChatNet] connect");
        close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }

    connected_.store(true);
    stop_recv_.store(false);
    recv_buf_used_ = 0;

    pthread_create(&recv_thread_, nullptr, RecvThreadFn, this);

    /* 启动心跳线程：每10秒发送一次 MSG_HEARTBEAT */
    stop_hb_.store(false);
    hb_started_ = true;
    pthread_create(&hb_thread_, nullptr, HeartbeatThreadFn, this);

    printf("[ChatNet] 已连接到 %s:%u\n", host.c_str(), port);
    return true;
}

void ChatNetworkClient::Disconnect()
{
    if (!connected_.load() && sock_fd_ < 0) return;

    stop_recv_.store(true);
    stop_hb_.store(true);
    connected_.store(false);

    if (sock_fd_ >= 0) {
        shutdown(sock_fd_, SHUT_RDWR);
        close(sock_fd_);
        sock_fd_ = -1;
    }
    /* 等接收线程退出 */
    pthread_join(recv_thread_, nullptr);
    /* 等心跳线程退出（最多等 ~100ms 轮询间隔） */
    if (hb_started_) {
        pthread_join(hb_thread_, nullptr);
        hb_started_ = false;
    }
}

/* ================================================================
 * 发送原语：所有客户端消息的最终出口
 *
 * 检查连接状态后，调用 msg_send 打包（6字节包头 + 消息体）发送给服务器
 * ================================================================ */
void ChatNetworkClient::SendRaw(const uint8_t* body,    // 消息体（可为 nullptr）
                                uint32_t len,           // 消息体长度（0 表示空包体）
                                uint16_t type)          // 消息类型（LOGIN/REG/JOIN/CHAT/HEARTBEAT...）
{
    if (!connected_.load() || sock_fd_ < 0) return;     // 未连接或 socket 无效，直接丢弃
    if (msg_send(sock_fd_, body, len, type) < 0)         // 加包头发送给服务器
        printf("[ChatNet] 发送失败, errno=%d\n", errno);
}

void ChatNetworkClient::SendJoin(const std::string& nickname)
{
    SendRaw((const uint8_t*)nickname.data(), (uint32_t)nickname.size(), MSG_JOIN);
}

void ChatNetworkClient::SendLeave(const std::string& nickname)
{
    SendRaw((const uint8_t*)nickname.data(), (uint32_t)nickname.size(), MSG_LEAVE);
}

void ChatNetworkClient::SendChat(const std::string& text)
{
    SendRaw((const uint8_t*)text.data(), (uint32_t)text.size(), MSG_CHAT);
}

/* ================================================================
 * 取走接收队列中所有待处理消息（线程安全）
 *
 * 调用方：UI 主线程（每帧 Render 开头调用一次）
 * 生产者：接收线程 RecvLoop() 不断往 incoming_ 末尾追加消息
 *
 * 两个线程不能同时操作 incoming_，用 queue_mutex_ 互斥锁保护。
 * 取走后队列清空，消息由 UI 线程逐条处理（更新聊天记录、玩家列表）。
 * ================================================================ */
std::vector<ChatMsg> ChatNetworkClient::Poll()
{
    std::vector<ChatMsg> out;                            // 用于返回的消息集合

    {
        std::lock_guard<std::mutex> lk(queue_mutex_);    // 加锁，阻塞接收线程写入
        out.assign(incoming_.begin(), incoming_.end());   // 队列中所有消息拷贝到 out
        incoming_.clear();                               // 清空队列，腾出空间
    }                                                    // 离开作用域，自动解锁

    return out;
}

/* ------------------------------------------------------------------ */
/*  接收线程：TCP 帧解析（与服务器 process_recv 逻辑对称）            */
/* ------------------------------------------------------------------ */
void* ChatNetworkClient::RecvThreadFn(void* arg)
{
    static_cast<ChatNetworkClient*>(arg)->RecvLoop();
    return nullptr;
}

void* ChatNetworkClient::HeartbeatThreadFn(void* arg)
{
    static_cast<ChatNetworkClient*>(arg)->HeartbeatLoop();
    return nullptr;
}

/* 每10秒发送一次心跳包（MSG_HEARTBEAT, body为空）
 * 以100ms为轮询粒度检查 stop_hb_，保证 Disconnect() 能快速退出 */
void ChatNetworkClient::HeartbeatLoop()
{
    int ticks = 0;
    while (!stop_hb_.load()) {
        usleep(100000);  /* 100ms */
        if (++ticks >= 100) {  /* 100 × 100ms = 10s */
            ticks = 0;
            if (connected_.load()) {
                SendRaw(nullptr, 0, MSG_HEARTBEAT);
                printf("[ChatNet] 心跳已发送\n");
            }
        }
    }
}

void ChatNetworkClient::RecvLoop()
{
    while (!stop_recv_.load()) {
        int n = recv(sock_fd_,
                     recv_buf_ + recv_buf_used_,
                     sizeof(recv_buf_) - recv_buf_used_,
                     0);
        if (n <= 0) {
            if (!stop_recv_.load())
                printf("[ChatNet] 连接断开 (recv=%d, errno=%d)\n", n, errno);
            connected_.store(false);
            break;
        }
        recv_buf_used_ += (uint32_t)n;

        /* 循环解包，一次 recv 可能带来多条完整消息 */
        while (recv_buf_used_ >= MSG_HDR_SIZE) {
            uint32_t body_len;
            memcpy(&body_len, recv_buf_, sizeof(uint32_t));
            body_len = ntohl(body_len);

            uint16_t msg_type;
            memcpy(&msg_type, recv_buf_ + sizeof(uint32_t), sizeof(uint16_t));
            msg_type = ntohs(msg_type);

            if (body_len > MSG_MAX_BODY) {
                printf("[ChatNet] 协议错误: body_len=%u 超限，重置缓冲区\n", body_len);
                recv_buf_used_ = 0;
                break;
            }

            uint32_t full_len = MSG_HDR_SIZE + body_len;
            if (recv_buf_used_ < full_len) break;  /* 半包，等待更多数据 */

            /* 完整消息：推入队列 */
            ChatMsg msg;
            msg.type = msg_type;
            msg.body = std::string((char*)recv_buf_ + MSG_HDR_SIZE, body_len);

            {
                std::lock_guard<std::mutex> lk(queue_mutex_);
                incoming_.push_back(std::move(msg));
            }

            /* 移除已消费数据 */
            recv_buf_used_ -= full_len;
            if (recv_buf_used_ > 0)
                memmove(recv_buf_, recv_buf_ + full_len, recv_buf_used_);
        }
    }
}

static std::string sha256hex(const std::string& input)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()),
           input.size(), hash);
    char hex[65];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(hex + i * 2, "%02x", hash[i]);
    hex[64] = '\0';
    return std::string(hex);
}

void ChatNetworkClient::SendLoginReq(const std::string& username,
                                     const std::string& password)
{
    MsgLoginReq req{};
    strncpy(req.username, username.c_str(), MSG_LOGIN_USERNAME_LEN - 1);
    std::string hash = sha256hex(password);
    strncpy(req.pw_hash, hash.c_str(), MSG_LOGIN_PWHASH_LEN - 1);
    SendRaw(reinterpret_cast<const uint8_t*>(&req), sizeof(req), MSG_LOGIN_REQ);
}

void ChatNetworkClient::SendRegisterReq(const std::string& username,
                                        const std::string& password)
{
    MsgLoginReq req{};
    strncpy(req.username, username.c_str(), MSG_LOGIN_USERNAME_LEN - 1);
    std::string hash = sha256hex(password);
    strncpy(req.pw_hash, hash.c_str(), MSG_LOGIN_PWHASH_LEN - 1);
    SendRaw(reinterpret_cast<const uint8_t*>(&req), sizeof(req), MSG_REG_REQ);
}

void ChatNetworkClient::SendProfileSetReq(const std::string& nickname, int avatar_idx)
{
    MsgProfileData req{};
    strncpy(req.nickname, nickname.c_str(), sizeof(req.nickname) - 1);
    req.avatar_idx = (uint8_t)avatar_idx;
    SendRaw(reinterpret_cast<const uint8_t*>(&req), sizeof(req), MSG_PROFILE_SET_REQ);
}

void ChatNetworkClient::SendPMReq(const std::string& target_username, const std::string& text)
{
    MsgPMReq req{};
    strncpy(req.target_username, target_username.c_str(), sizeof(req.target_username) - 1);
    strncpy(req.text,            text.c_str(),            sizeof(req.text)            - 1);
    SendRaw(reinterpret_cast<const uint8_t*>(&req), sizeof(req), MSG_PM_REQ);
}