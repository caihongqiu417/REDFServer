#pragma once
#include <string>
#include <vector>
#include "ChatNetworkClient.h"

class ChatUI {
public:
    ChatUI();

    /* 每帧调用一次，渲染所有 ImGui 控件并处理网络消息 */
    void Render(ChatNetworkClient& net);

    /* 返回 true 表示用户请求退出 */
    bool WantsQuit() const { return wants_quit_; }

private:
    /* ---- 状态 ---- */
    bool  joined_        = false;   /* 是否已 Join */
    bool  wants_quit_    = false;
    bool  scroll_bottom_ = false;   /* 下一帧滚动到底部 */
    bool  focus_input_   = false;   /* 下一帧聚焦输入框 */

    char  nick_buf_[64]  = "Player";
    char  chat_buf_[512] = {};

    std::vector<std::string> messages_; /* 聊天历史（格式化后的行） */
    std::vector<std::string> players_;  /* 在线玩家列表 */

    /* ---- 渲染子函数 ---- */
    void HandleIncoming(ChatNetworkClient& net);
    void RenderLoginBar(ChatNetworkClient& net);
    void RenderPlayerList();
    void RenderChatArea(ChatNetworkClient& net);

    /* ---- 工具 ---- */
    void AppendMsg(const std::string& line);
    static std::vector<std::string> ParsePlayerList(const std::string& body);
    void TrySendChat(ChatNetworkClient& net);
};
