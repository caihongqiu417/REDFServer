#ifndef MSG_PROTOCOL_H
#define MSG_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>  /* htonl / ntohl */

/*
 * 消息格式：
 * +--------------------+---------------------------+
 * |  Header (4 bytes)  |  Body  (N bytes)          |
 * |  uint32_t 网络字节序 |  消息内容                  |
 * +--------------------+---------------------------+
 * Header 存储 Body 的字节长度，不含 Header 自身。
 */

#define MSG_HDR_SIZE  ((uint32_t)sizeof(uint32_t))   /* 包头固定 4 字节 */
#define MSG_MAX_BODY  ((uint32_t)(64 * 1024))         /* 单条消息体最大 64 KB */

/*
 * msg_send_all: 保证将 len 字节全部写入 fd（处理 TCP 短写）
 * 返回 0 成功，-1 失败
 */
static inline int msg_send_all(int fd, const uint8_t *buf, uint32_t len)
{
    uint32_t sent = 0;
    while (sent < len) {
        int n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        sent += (uint32_t)n;
    }
    return 0;
}

/*
 * msg_send: 发送一条带包头的完整消息
 * 先写 4 字节长度头（网络字节序），再写消息体
 * 返回 0 成功，-1 失败
 */
static inline int msg_send(int fd, const uint8_t *body, uint32_t body_len)
{
    uint32_t net_len = htonl(body_len);
    if (msg_send_all(fd, (const uint8_t *)&net_len, MSG_HDR_SIZE) < 0)
        return -1;
    if (body_len > 0 && msg_send_all(fd, body, body_len) < 0)
        return -1;
    return 0;
}

#endif /* MSG_PROTOCOL_H */