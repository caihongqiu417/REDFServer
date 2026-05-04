#pragma once
#include <string>
#include <deque>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <pthread.h>
#include "../msg_protocol.h"

// 拆协议包 → 组装成 ChatMsg（去掉包头，只留类型和消息体）
struct ChatMsg {
    uint16_t    type;   /* MsgType 枚举值 */
    std::string body;   /* 消息体原始字节 */
};

class ChatNetworkClient {
public:
    ChatNetworkClient();
    ~ChatNetworkClient();

    /* 连接到服务器，成功返回 true */
    bool Connect(const std::string& host, uint16_t port);
    void Disconnect();
    bool IsConnected() const { return connected_.load(); }

    /* 发送各类消息 */
    void SendJoin(const std::string& nickname);
    void SendLeave(const std::string& nickname);
    void SendChat(const std::string& text);

    void SendLoginReq(const std::string& username, const std::string& password);
    void SendRegisterReq(const std::string& username, const std::string& password);

    /* 把主页改动（昵称 + 头像序号）发给服务器保存 */
    void SendProfileSetReq(const std::string& nickname, int avatar_idx);


    /* 从主线程调用：取走所有待处理的消息 */
    std::vector<ChatMsg> Poll();

private:
    int  sock_fd_;
    std::atomic<bool> connected_;
    std::atomic<bool> stop_recv_;
    pthread_t recv_thread_;

    /* 线程安全的接收队列 */
    std::mutex          queue_mutex_;
    std::deque<ChatMsg> incoming_;

    /* 每连接独立的接收缓冲区（处理粘包/半包） */
    uint8_t  recv_buf_[MSG_HDR_SIZE + MSG_MAX_BODY];
    uint32_t recv_buf_used_;

    void SendRaw(const uint8_t* body, uint32_t len, uint16_t type);
    static void* RecvThreadFn(void* arg);
    void RecvLoop();

    /* 心跳线程：每10秒向服务器发送一次 MSG_HEARTBEAT（共3次/30s） */
    pthread_t         hb_thread_;
    std::atomic<bool> stop_hb_;
    bool              hb_started_;
    static void* HeartbeatThreadFn(void* arg);
    void HeartbeatLoop();
};
