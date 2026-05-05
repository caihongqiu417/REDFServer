#ifndef USER_DATABASE_H
#define USER_DATABASE_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include "msg_protocol.h"

class UserDatabase
{
    public:
    UserDatabase();
    ~UserDatabase();

    // 打开（或创建）数据库文件，自动建表，返回是否成功
    bool Open(const std::string& db_path);

    // 注册新用户
    bool RegisterUser(const std::string& username,const std::string& pw_hash);

    // 验证用户登录：用户名+密码哈希匹配返回 true
    bool AuthUser(const std::string& username, const std::string& pw_hash);

    // 查询用户的昵称和头像序号，找不到时返回 false
    bool GetProfile(const std::string& username,
                    std::string& out_nickname, int& out_avatar_idx);

    // 保存/更新用户的昵称和头像序号，成功返回 true
    bool SetProfile(const std::string& username,
                    const std::string& nickname, int avatar_idx);

    // 把一条私聊消息存到 offline_msgs，等目标用户下次登录再投递
    // is_echo=true 表示这是给发送方存的历史副本，登录后按 SELF 渲染
    void StoreOfflineMsg(const std::string& target_username,
                         const MsgPMRecv& msg, bool is_echo = false);

    // 取出并删除某用户的所有离线消息，按到达顺序排列
    std::vector<MsgPMRecv> PopOfflineMsgs(const std::string& username);

    // ---- 好友关系 ----
    // 发好友申请（已存在则忽略），返回 false 表示已经是好友或申请已存在
    bool SendFriendReq(const std::string& from, const std::string& to);
    // 接受申请：把 status 改为 1
    bool AcceptFriend(const std::string& requester, const std::string& target);
    // 拒绝/删除申请
    void RejectFriend(const std::string& requester, const std::string& target);
    // 是否已是好友
    bool IsFriend(const std::string& a, const std::string& b);
    // 是否有从 from 发往 to 的待处理申请
    bool HasPendingReq(const std::string& from, const std::string& to);
    // 登录时推送：返回已接受的好友列表
    std::vector<MsgFriendEntry> GetFriends(const std::string& username);
    // 登录时推送：返回以我为目标的待处理申请（对方发来的、我还没处理的）
    std::vector<MsgFriendNotify> GetPendingRequests(const std::string& username);

    void Close();

    private:
    sqlite3* db_;
};
#endif // USER_DATABASE_H