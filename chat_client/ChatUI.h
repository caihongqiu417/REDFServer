#pragma once
#include <string>
#include <vector>
#include "ChatNetworkClient.h"

class ChatUI {
public:
    ChatUI();

    /* 每帧调用一次，渲染所有 ImGui 控件并处理网络消息 */
    void Render(ChatNetworkClient& net);

    /* 返回 true 表示用户点了退出按钮 */
    bool WantsQuit() const { return wants_quit_; }

private:
    /* ================================================================
     * 界面状态
     * ================================================================ */
    enum class UiState { LOGIN, WAITING, CHAT };   // 当前界面：登录页 / 等待连接 / 聊天室
    UiState ui_state_ = UiState::LOGIN;            // 初始显示登录界面

    bool  joined_        = false;                  // 是否已发送 MSG_JOIN
    bool  wants_quit_    = false;                  // 用户是否点了退出
    bool  scroll_bottom_ = false;                  // 下一帧自动滚动聊天区到底部
    bool  focus_input_   = false;                  // 下一帧自动聚焦聊天输入框

    /* ================================================================
     * UI 输入框绑定的字符缓冲区（ImGui 直接读写）
     * ================================================================ */
    char  user_buf_[32]  = {};                     // 登录界面：用户名输入框
    char  pass_buf_[64]  = {};                     // 登录界面：密码输入框（明文，发送前转哈希）
    std::string login_error_;                      // 登录/注册失败时显示的红色提示

    char  nick_buf_[64]  = "Player";               // 聊天界面：昵称输入框，默认 "Player"
    char  chat_buf_[512] = {};                     // 聊天界面：消息输入框

    std::vector<std::string> messages_;             // 聊天历史（已格式化的行，如 "张三: 你好"）
    std::vector<std::string> players_;              // 在线玩家列表（昵称或 IP:Port）

    /* ================================================================
     * 渲染子函数
     * ================================================================ */
    void RenderLoginPage(ChatNetworkClient& net);
    void HandleIncoming(ChatNetworkClient& net);    // 处理服务器发来的消息，更新 messages_ / players_
    void RenderLoginBar(ChatNetworkClient& net);    // 渲染顶栏：登录/注册按钮 + 输入框 + 错误提示
    void RenderPlayerList();                        // 渲染侧边栏：在线玩家列表
    void RenderChatArea(ChatNetworkClient& net);    // 渲染主区域：聊天记录 + 输入框

    /* ================================================================
     * 工具函数
     * ================================================================ */
    void AppendMsg(const std::string& line);        // 往聊天历史追加一行
    static std::vector<std::string> ParsePlayerList(const std::string& body); // 解析 MSG_PLAYER_LIST 的二进制包体
    void TrySendChat(ChatNetworkClient& net);       // 用户按回车时发送聊天消息
};