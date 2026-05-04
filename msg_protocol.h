#ifndef MSG_PROTOCOL_H
#define MSG_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>  /* htonl / ntohl / htons / ntohs */

/*
 * 消息格式（扩展版，新增 2 字节消息类型字段）：
 *
 * +--------------------+--------------------+---------------------------+
 * |  body_len (4 bytes)|  msg_type (2 bytes)|  body  (N bytes)          |
 * |  uint32_t 网络字节序 |  uint16_t 网络字节序 |  消息内容                  |
 * +--------------------+--------------------+---------------------------+
 *
 * body_len: 仅表示 body 的字节数，不含 6 字节包头自身。
 * msg_type: 消息语义类型，见 MsgType 枚举。
 */

/* 包头总长度 = 4字节长度 + 2字节类型 */
#define MSG_HDR_SIZE  ((uint32_t)(sizeof(uint32_t) + sizeof(uint16_t)))  /* 6 */
#define MSG_MAX_BODY  ((uint32_t)(64 * 1024))   /* 单条消息体最大 64 KB */

/* 消息类型枚举 */
typedef enum : uint16_t {
    MSG_CHAT        = 1,   /* 普通聊天消息 */
    MSG_JOIN        = 2,   /* 进入聊天室通知（body: nickname字符串） */
    MSG_LEAVE       = 3,   /* 离开聊天室通知（body: nickname字符串） */
    MSG_NICK_SET    = 4,   /* 设置昵称 */
    MSG_PLAYER_LIST = 5,   /* 服务器→新客户端：当前在线玩家列表
                            * body格式：
                            *   [num_players: uint16_t 大端]
                            *   重复 num_players 次：
                            *     [str_len: uint16_t 大端][str: str_len字节，无\0]
                            *   每条 str 格式为 "ip:port"（如 "127.0.0.1:52341"） */
    MSG_HEARTBEAT   = 6,   /* 客户端→服务器：心跳包（body为空，30s内发3次） */

    /* -------- 登陆/注册 -------- */
    MSG_LOGIN_REQ   = 7,   /* 客户端→服务器：登陆请求，body = MsgLoginReq  (96B) */
    MSG_LOGIN_RSP   = 8,   /* 服务器→客户端：登陆响应，body = MsgLoginRsp  (64B) */
    MSG_REG_REQ     = 9,   /* 客户端→服务器：注册请求，body = MsgLoginReq  (96B，复用) */
    MSG_REG_RSP     = 10,  /* 服务器→客户端：注册响应，body = MsgLoginRsp  (64B，复用) */

    /* -------- 用户主页（昵称 + 头像） -------- */
    MSG_PROFILE_RSP     = 18, /* 服务器→客户端：登录后推送已保存的昵称和头像序号，body = MsgProfileData (68B) */
    MSG_PROFILE_SET_REQ = 19, /* 客户端→服务器：保存主页改动，body = MsgProfileData (68B) */
    MSG_PROFILE_SET_RSP = 20, /* 服务器→客户端：保存结果，body = MsgLoginRsp (64B，复用 result+reason) */
} MsgType;


/* ------------------------------------------------------------------ */
/*  登陆/注册消息体结构（定长，直接 memcpy)              */
/* ------------------------------------------------------------------ */

#define MSG_LOGIN_USERNAME_LEN  32   /* 用户名最大字节数（含\0） */
#define MSG_LOGIN_PWHASH_LEN    64   /* SHA-256 hex字符串长度（含\0） */
#define MSG_LOGIN_REASON_LEN    63   /* 响应原因字符串最大字节数（含\0） */


/* 客户端 → 服务器：登陆请求 / 注册请求 (96 字节) */
#pragma pack(push, 1) //禁止填充对齐
typedef struct {
    char username[MSG_LOGIN_USERNAME_LEN];  /* UTF-8，右补\0 */
    char pw_hash [MSG_LOGIN_PWHASH_LEN];    /* SHA-256 hex，如 "a3f5...4d2e\0" */
} MsgLoginReq;
#pragma pack(pop)

/* 服务器 → 客户端：登陆响应 / 注册响应 (64 字节) */
#pragma pack(push, 1)
typedef struct {
    uint8_t result;                         /* 0 = 失败，1 = 成功 */
    char    reason[MSG_LOGIN_REASON_LEN];   /* 失败时填原因，成功时可为空 */
} MsgLoginRsp;
#pragma pack(pop)

/* 断言：确保结构体尺寸符合预期（编译期检查） */
static_assert(sizeof(MsgLoginReq) == 96, "MsgLoginReq size mismatch");
static_assert(sizeof(MsgLoginRsp) == 64, "MsgLoginRsp size mismatch");

/* ------------------------------------------------------------------ */
/*  主页数据结构（双向复用：服务器下发 / 客户端上传均用此结构）           */
/* ------------------------------------------------------------------ */
#define MSG_PROFILE_NICK_LEN 64   /* 昵称最大字节数（含 \0） */

#pragma pack(push, 1)
typedef struct {
    char    nickname[MSG_PROFILE_NICK_LEN]; /* 昵称，UTF-8，右补 \0 */
    uint8_t avatar_idx;                     /* 头像序号，0 = 第一张 */
    uint8_t _pad[3];                        /* 补齐到 4 字节倍数 */
} MsgProfileData;                           /* 共 68 字节 */
#pragma pack(pop)

static_assert(sizeof(MsgProfileData) == 68, "MsgProfileData size mismatch");
/* MSG_PROFILE_SET_RSP 的 body 直接复用 MsgLoginRsp（result + reason） */



/* ------------------------------------------------------------------ */
/*  内部工具：保证将 len 字节全部写入 fd（处理 TCP 短写）               */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/*  msg_send: 发送一条带包头（长度+类型）的完整消息                     */
/*  写顺序：① 4字节 body_len → ② 2字节 msg_type → ③ body             */
/*  返回 0 成功，-1 失败                                               */
/* ------------------------------------------------------------------ */
static inline int msg_send(int fd,
                           const uint8_t *body,
                           uint32_t       body_len,
                           uint16_t       msg_type)
{
    uint32_t net_len  = htonl(body_len);
    uint16_t net_type = htons(msg_type);

    if (msg_send_all(fd, (const uint8_t *)&net_len,  sizeof(net_len))  < 0) return -1;
    if (msg_send_all(fd, (const uint8_t *)&net_type, sizeof(net_type)) < 0) return -1;
    if (body_len > 0 && msg_send_all(fd, body, body_len) < 0)               return -1;
    return 0;
}

#endif /* MSG_PROTOCOL_H */