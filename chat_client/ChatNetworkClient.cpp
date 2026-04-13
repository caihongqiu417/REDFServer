#include "ChatNetworkClient.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

ChatNetworkClient::ChatNetworkClient()
    : sock_fd_(-1), connected_(false), stop_recv_(false), recv_buf_used_(0)
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
    printf("[ChatNet] 已连接到 %s:%u\n", host.c_str(), port);
    return true;
}

void ChatNetworkClient::Disconnect()
{
    if (!connected_.load() && sock_fd_ < 0) return;

    stop_recv_.store(true);
    connected_.store(false);

    if (sock_fd_ >= 0) {
        shutdown(sock_fd_, SHUT_RDWR);
        close(sock_fd_);
        sock_fd_ = -1;
    }
    /* 等接收线程退出 */
    pthread_join(recv_thread_, nullptr);
}

/* ------------------------------------------------------------------ */
/*  发送原语                                                           */
/* ------------------------------------------------------------------ */
void ChatNetworkClient::SendRaw(const uint8_t* body, uint32_t len, uint16_t type)
{
    if (!connected_.load() || sock_fd_ < 0) return;
    if (msg_send(sock_fd_, body, len, type) < 0)
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

/* ------------------------------------------------------------------ */
/*  主线程调用：取走队列中所有待处理消息                               */
/* ------------------------------------------------------------------ */
std::vector<ChatMsg> ChatNetworkClient::Poll()
{
    std::vector<ChatMsg> out;
    std::lock_guard<std::mutex> lk(queue_mutex_);
    out.assign(incoming_.begin(), incoming_.end());
    incoming_.clear();
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
