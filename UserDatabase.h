#ifndef USER_DATABASE_H
#define USER_DATABASE_H

#include <string>
#include <sqlite3.h>

class UserDatabase
{
    public:
    UserDatabase();
    ~UserDatabase();

    // 打开（或创建）数据库文件，自动建表，返回是否成功
    bool Open(const std::string& db_path);

    // 注册新用户
    bool RegisterUser(const std::string& username,const std::string& pw_hash);

    void Close();

    private:
    sqlite3* db_;
}