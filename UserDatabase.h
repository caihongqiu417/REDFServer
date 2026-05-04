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
    // 登录成功后调用一次，把欠的消息都还给他
    std::vector<MsgPMRecv> PopOfflineMsgs(const std::string& username);

    void Close();

    private:
    sqlite3* db_;
};
#endif // USER_DATABASE_H