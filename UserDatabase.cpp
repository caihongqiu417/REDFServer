#include "UserDatabase.h"
#include <cstdio>
#include <cstring>
#include <ctime>

UserDatabase::UserDatabase() 
    : db_(nullptr)
    {

    }

UserDatabase::~UserDatabase() 
{
    Close();
}

bool UserDatabase::Open(const std::string& db_path)
{
    // 创建数据库,&db_
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        fprintf(stderr, "[UserDB] 打开数据库失败: %s\n", sqlite3_errmsg(db_));
        return false;
    }

    //在 SQLite 数据库里创建一个叫 users 的表，用来持久化存储用户账号信息。
    // 建表（如果已存在则跳过）
    const char* create_sql =
    "CREATE TABLE IF NOT EXISTS users ("   // 创建 users 表，存在就不重建
    "  id       INTEGER PRIMARY KEY AUTOINCREMENT,"  // 序号，自动编号，唯一标识
    "  username TEXT UNIQUE NOT NULL,"     // 用户名，文本，不能重，不能空
    "  pw_hash  TEXT NOT NULL"             // 密码哈希值，文本，不能空
    ");";

    // 执行建表SQL，失败则打印错误并返回false
    char* errmsg = nullptr;  
    if (sqlite3_exec(db_,           // 数据库连接
                 create_sql,    // 要执行的建表语句
                 nullptr,       // 不需要回调函数
                 nullptr,       // 不需要传给回调的参数
                 &errmsg)       // 出错时把错误信息写到这里
        != SQLITE_OK) 
    {         
    fprintf(stderr, "[UserDB] 建表失败: %s\n", errmsg);  // 打印到错误输出
    sqlite3_free(errmsg);       
    return false;               
    }

    /* 数据库迁移：给旧版 users 表加新列。
     * ALTER TABLE ADD COLUMN 在列已存在时会返回错误，这里直接忽略该错误。
     * 这样无论是新建数据库还是旧数据库升级，都能正常运行。*/
    sqlite3_exec(db_,
        "ALTER TABLE users ADD COLUMN nickname TEXT DEFAULT ''",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db_,
        "ALTER TABLE users ADD COLUMN avatar_idx INTEGER DEFAULT 0",
        nullptr, nullptr, nullptr);

    /* offline_msgs：对方不在线时把私聊消息存这里，下次登录一起推过去。
     * id 自增，查询时按 id 排序，保证投递顺序和发送顺序一致。
     * sent_at 存 unix 时间戳，以后客户端想显示"昨天 14:32"之类可以用。
     * 每个字段都对应 MsgPMRecv 里的一个成员，取出时直接填回去。*/
    const char* offline_sql =
        "CREATE TABLE IF NOT EXISTS offline_msgs ("
        "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  target_username TEXT    NOT NULL,"
        "  sender_username TEXT    NOT NULL,"
        "  sender_nick     TEXT    NOT NULL,"
        "  sender_avatar   INTEGER NOT NULL DEFAULT 0,"
        "  text            TEXT    NOT NULL,"
        "  sent_at         INTEGER NOT NULL,"
        "  is_echo         INTEGER NOT NULL DEFAULT 0"
        ");";

    if (sqlite3_exec(db_, offline_sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "[UserDB] 建离线消息表失败: %s\n", errmsg);
        sqlite3_free(errmsg);
        return false;
    }

    /* 按 target + id 建索引，登录时捞消息只扫目标用户的行，不全表扫 */
    sqlite3_exec(db_,
        "CREATE INDEX IF NOT EXISTS idx_offline_target "
        "ON offline_msgs (target_username, id);",
        nullptr, nullptr, nullptr);

    /* friends：好友关系表，单行存储（user_a < user_b 字典序）。
     * requester 记录谁主动发的申请，status=0 待确认，status=1 已是好友。
     * 用 requester 字段而不是 user_a/user_b 来表示方向，
     * 这样无论字典序怎么排，都能准确找到"我收到的待处理申请"。*/
    sqlite3_exec(db_,
        "CREATE TABLE IF NOT EXISTS friends ("
        "  user_a    TEXT NOT NULL,"
        "  user_b    TEXT NOT NULL,"
        "  status    INTEGER NOT NULL DEFAULT 0,"
        "  requester TEXT NOT NULL,"
        "  PRIMARY KEY (user_a, user_b)"
        ");",
        nullptr, nullptr, nullptr);

    fprintf(stdout, "[UserDB] 数据库已就绪: %s\n", db_path.c_str());
    return true;
}

// 注册新用户：往 users 表插入一行（用户名 + 密码哈希）
bool UserDatabase::RegisterUser(const std::string& username,   
                                const std::string& pw_hash) {  

    // 预编译SQL，? 是占位符，防止SQL注入攻击
    const char* sql = "INSERT INTO users (username, pw_hash) VALUES (?, ?);";

    sqlite3_stmt* stmt = nullptr;                              // 编译后的语句对象
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;                                          // 编译失败，直接返回

    // 把用户名绑到第1个?上
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    // 把密码哈希绑到第2个?上
    sqlite3_bind_text(stmt, 2, pw_hash.c_str(),  -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);     // 真正执行插入
    sqlite3_finalize(stmt);          // 释放语句资源

    // SQLITE_DONE = 插入成功
    // 其他（如SQLITE_CONSTRAINT）= 用户名已存在，插入失败
    return rc == SQLITE_DONE;
}

// 验证用户登录：检查用户名和密码哈希是否匹配
bool UserDatabase::AuthUser(const std::string& username,   
                            const std::string& pw_hash)
{
     // 按用户名查询数据库中存储的密码哈希
    const char* sql = "SELECT pw_hash FROM users WHERE username = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;                         // SQL编译失败

    // 把用户名填入第1个?，占位符防注入
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // 取出数据库里存的pw_hash（第0列）
        const char* stored = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 0)
        );
        // 跟客户端传来的pw_hash比较，一样就通过
        ok = (pw_hash == stored);
    }
    // 如果step没返回SQLITE_ROW，说明用户名不存在，ok保持false

    sqlite3_finalize(stmt);  // 释放语句资源
    return ok;   
}

bool UserDatabase::GetProfile(const std::string& username,
                               std::string& out_nickname, int& out_avatar_idx)
{
    const char* sql =
        "SELECT nickname, avatar_idx FROM users WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        /* sqlite3_column_text 返回该列的文本指针，可能为 NULL（列值为空时） */
        const char* nick = reinterpret_cast<const char*>(
                               sqlite3_column_text(stmt, 0));
        out_nickname   = nick ? nick : "";
        out_avatar_idx = sqlite3_column_int(stmt, 1);
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool UserDatabase::SetProfile(const std::string& username,
                               const std::string& nickname, int avatar_idx)
{
    /* 只更新昵称和头像序号，账号名和密码哈希不动 */
    const char* sql =
        "UPDATE users SET nickname = ?, avatar_idx = ? WHERE username = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, nickname.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, avatar_idx);
    sqlite3_bind_text(stmt, 3, username.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

void UserDatabase::Close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void UserDatabase::StoreOfflineMsg(const std::string& target_username,
                                   const MsgPMRecv& msg, bool is_echo)
{
    /* 旧数据库没有 is_echo 列，加上去，已有则忽略 */
    sqlite3_exec(db_,
        "ALTER TABLE offline_msgs ADD COLUMN is_echo INTEGER NOT NULL DEFAULT 0",
        nullptr, nullptr, nullptr);

    const char* sql =
        "INSERT INTO offline_msgs "
        "(target_username, sender_username, sender_nick, sender_avatar, text, sent_at, is_echo) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "[UserDB] StoreOfflineMsg 编译SQL失败: %s\n",
                sqlite3_errmsg(db_));
        return;
    }

    sqlite3_bind_text (stmt, 1, target_username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 2, msg.sender_username,     -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 3, msg.sender_nick,         -1, SQLITE_STATIC);
    sqlite3_bind_int  (stmt, 4, msg.avatar_idx);
    sqlite3_bind_text (stmt, 5, msg.text,                -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, (sqlite3_int64)time(nullptr));
    sqlite3_bind_int  (stmt, 7, is_echo ? 1 : 0);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        fprintf(stderr, "[UserDB] StoreOfflineMsg 写入失败: %s\n",
                sqlite3_errmsg(db_));

    sqlite3_finalize(stmt);
}

std::vector<MsgPMRecv> UserDatabase::PopOfflineMsgs(const std::string& username)
{
    std::vector<MsgPMRecv> result;

    /* 先把这个人的离线消息全读出来，is_echo 一起取回来 */
    const char* sel_sql =
        "SELECT sender_username, sender_nick, sender_avatar, text, is_echo "
        "FROM offline_msgs "
        "WHERE target_username = ? "
        "ORDER BY id ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sel_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "[UserDB] PopOfflineMsgs 编译SELECT失败\n");
        return result;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MsgPMRecv msg{};

        auto col_str = [&](int col) -> const char* {
            const unsigned char* p = sqlite3_column_text(stmt, col);
            return p ? reinterpret_cast<const char*>(p) : "";
        };

        strncpy(msg.sender_username, col_str(0), sizeof(msg.sender_username) - 1);
        strncpy(msg.sender_nick,     col_str(1), sizeof(msg.sender_nick)     - 1);
        msg.avatar_idx = (uint8_t)sqlite3_column_int(stmt, 2);
        strncpy(msg.text,            col_str(3), sizeof(msg.text)            - 1);
        msg.is_echo    = (uint8_t)sqlite3_column_int(stmt, 4);

        result.push_back(msg);
    }
    sqlite3_finalize(stmt);

    if (result.empty())
        return result;   /* 没有离线消息，跳过后面的删除 */

    /* 读完之后从库里删掉，防止下次登录重复投递 */
    const char* del_sql =
        "DELETE FROM offline_msgs WHERE target_username = ?;";

    sqlite3_stmt* del_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, del_sql, -1, &del_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(del_stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(del_stmt);
        sqlite3_finalize(del_stmt);
    }

    printf("[UserDB] 向 %s 投递 %zu 条离线消息\n", username.c_str(), result.size());
    return result;
}

/* ------------------------------------------------------------------ */
/*  好友关系                                                            */
/* ------------------------------------------------------------------ */

/* 把两个账号名规范化为字典序：user_a < user_b */
static void canon(const std::string& a, const std::string& b,
                  std::string& out_a, std::string& out_b)
{
    if (a < b) { out_a = a; out_b = b; }
    else        { out_a = b; out_b = a; }
}

bool UserDatabase::SendFriendReq(const std::string& from, const std::string& to)
{
    if (IsFriend(from, to)) return false;     /* 已经是好友，不重复申请 */
    if (HasPendingReq(from, to)) return false; /* 已发过申请 */

    std::string ua, ub;
    canon(from, to, ua, ub);

    /* INSERT OR IGNORE：如果对方已经发了申请（行已存在），就不覆盖 */
    const char* sql =
        "INSERT OR IGNORE INTO friends (user_a, user_b, status, requester) "
        "VALUES (?, ?, 0, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, ua.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ub.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, from.c_str(), -1, SQLITE_STATIC);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool UserDatabase::AcceptFriend(const std::string& requester, const std::string& target)
{
    std::string ua, ub;
    canon(requester, target, ua, ub);

    const char* sql =
        "UPDATE friends SET status = 1 "
        "WHERE user_a = ? AND user_b = ? AND requester = ? AND status = 0;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, ua.c_str(),        -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ub.c_str(),        -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, requester.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    bool ok = (sqlite3_changes(db_) > 0);
    sqlite3_finalize(stmt);
    return ok;
}

void UserDatabase::RejectFriend(const std::string& requester, const std::string& target)
{
    std::string ua, ub;
    canon(requester, target, ua, ub);

    const char* sql =
        "DELETE FROM friends WHERE user_a=? AND user_b=? AND requester=? AND status=0;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, ua.c_str(),        -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ub.c_str(),        -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, requester.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

bool UserDatabase::IsFriend(const std::string& a, const std::string& b)
{
    std::string ua, ub;
    canon(a, b, ua, ub);
    const char* sql =
        "SELECT 1 FROM friends WHERE user_a=? AND user_b=? AND status=1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, ua.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ub.c_str(), -1, SQLITE_STATIC);
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

bool UserDatabase::HasPendingReq(const std::string& from, const std::string& to)
{
    std::string ua, ub;
    canon(from, to, ua, ub);
    const char* sql =
        "SELECT 1 FROM friends WHERE user_a=? AND user_b=? AND requester=? AND status=0;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, ua.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ub.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, from.c_str(), -1, SQLITE_STATIC);
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

std::vector<MsgFriendEntry> UserDatabase::GetFriends(const std::string& username)
{
    std::vector<MsgFriendEntry> result;

    /* 找出所有 status=1 的行，取另一方的账号名，再 JOIN users 表拿昵称和头像 */
    const char* sql =
        "SELECT u.username, u.nickname, u.avatar_idx "
        "FROM friends f "
        "JOIN users u ON u.username = CASE WHEN f.user_a=? THEN f.user_b ELSE f.user_a END "
        "WHERE (f.user_a=? OR f.user_b=?) AND f.status=1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, username.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MsgFriendEntry e{};
        auto col = [&](int c) -> const char* {
            const unsigned char* p = sqlite3_column_text(stmt, c);
            return p ? reinterpret_cast<const char*>(p) : "";
        };
        strncpy(e.username,  col(0), sizeof(e.username)  - 1);
        strncpy(e.nick,      col(1), sizeof(e.nick)      - 1);
        e.avatar_idx = (uint8_t)sqlite3_column_int(stmt, 2);
        result.push_back(e);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<MsgFriendNotify> UserDatabase::GetPendingRequests(const std::string& username)
{
    std::vector<MsgFriendNotify> result;

    /* 找出所有以 username 为目标、还没处理的申请，JOIN users 取申请方的昵称和头像 */
    const char* sql =
        "SELECT f.requester, u.nickname, u.avatar_idx "
        "FROM friends f "
        "JOIN users u ON u.username = f.requester "
        "WHERE (f.user_a=? OR f.user_b=?) "
        "  AND f.requester != ? "
        "  AND f.status = 0;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, username.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MsgFriendNotify ntf{};
        ntf.event_type = 0;   /* 收到申请 */
        auto col = [&](int c) -> const char* {
            const unsigned char* p = sqlite3_column_text(stmt, c);
            return p ? reinterpret_cast<const char*>(p) : "";
        };
        strncpy(ntf.from_username, col(0), sizeof(ntf.from_username) - 1);
        strncpy(ntf.from_nick,     col(1), sizeof(ntf.from_nick)     - 1);
        ntf.from_avatar = (uint8_t)sqlite3_column_int(stmt, 2);
        result.push_back(ntf);
    }
    sqlite3_finalize(stmt);
    return result;
}