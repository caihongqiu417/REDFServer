#include "UserDatabase.h"
#include <cstdio>

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
        nullptr, nullptr, nullptr);   /* 忽略返回值（列已存在时会失败，属正常） */
    sqlite3_exec(db_,
        "ALTER TABLE users ADD COLUMN avatar_idx INTEGER DEFAULT 0",
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