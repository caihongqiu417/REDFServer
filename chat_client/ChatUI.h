#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "ChatNetworkClient.h"

struct ChatLine {
    enum class Kind { SYSTEM, SELF, OTHER } kind = Kind::SYSTEM;
    std::string nick;
    std::string text;
    std::string sender_username;   // OTHER 消息才有，用于点头像发起私聊
    int avatar_idx = -1;
};

struct PlayerInfo {
    std::string username;
    std::string nick;
};

class ChatUI {
public:
    ChatUI();

    /* 每帧调用一次，渲染所有 ImGui 控件并处理网络消息 */
    void Render(ChatNetworkClient& net);

    /* 返回 true 表示用户点了退出按钮 */
    bool WantsQuit() const { return wants_quit_; }

    /* 传入三张图标的 OpenGL 纹理 ID */
    void SetIcons(unsigned int world, unsigned int friend_icon, unsigned int private_icon);

    /* 传入头像纹理数组（由 main.cpp 从 Avatar 目录 PNG 头文件加载） */
    void SetAvatars(unsigned int* textures, int count);

    /* 传入登录页背景图纹理 */
    void SetBackground(unsigned int tex, int w, int h);

private:
    /* ================================================================
     * 界面状态
     * ================================================================ */
    enum class UiState { LOGIN, WAITING, CHAT };
    UiState ui_state_ = UiState::LOGIN;

    /* 左侧导航栏当前选中的功能 */
    enum class MainTab { WORLD, FRIENDS, PRIVATE };
    MainTab main_tab_ = MainTab::WORLD;

    /* 登录页背景图 */
    unsigned int bg_tex_ = 0;
    int          bg_w_   = 0;
    int          bg_h_   = 0;

    /* 图标纹理 ID（OpenGL GLuint，用 unsigned int 避免引入 GL 头文件） */
    unsigned int icon_world_   = 0;
    unsigned int icon_friend_  = 0;
    unsigned int icon_private_ = 0;

    /* 头像纹理数组 */
    static const int MAX_AVATARS = 16;
    unsigned int avatar_textures_[MAX_AVATARS] = {};
    int          avatar_count_   = 0;
    int          current_avatar_ = 0;  /* 当前使用的头像序号（0 = 第一张） */

    /* 主页弹窗状态 */
    bool open_profile_       = false; /* true 时下一帧打开弹窗 */
    int  profile_sel_avatar_ = 0;     /* 弹窗里临时选中的头像，保存前不影响 current_avatar_ */
    char profile_nick_buf_[64] = {};  /* 弹窗里临时编辑的昵称，保存前不影响 nick_buf_ */

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

    std::vector<ChatLine>    messages_;              // 世界频道聊天历史
    std::vector<PlayerInfo>  players_;              // 在线玩家列表

    /* 私聊状态 */
    std::string pm_target_uname_;
    std::string pm_target_nick_;
    std::map<std::string, std::vector<ChatLine>> pm_logs_;
    std::map<std::string, std::string>           pm_nicks_;
    std::map<std::string, int>                   pm_unread_;
    char pm_buf_[512] = {};

    /* ================================================================
     * 渲染子函数
     * ================================================================ */
    void RenderLoginPage(ChatNetworkClient& net);
    void HandleIncoming(ChatNetworkClient& net);
    void RenderNavSidebar();                        // 左侧竖向图标导航栏
    void RenderLoginBar(ChatNetworkClient& net);
    void RenderPlayerList();
    void RenderChatArea(ChatNetworkClient& net);
    void RenderProfileModal(ChatNetworkClient& net); // 主页弹窗（头像选择 + 昵称修改）

    /* ================================================================
     * 工具函数
     * ================================================================ */
    void AppendMsg(const std::string& line);
    void AppendChat(const std::string& nick, const std::string& text,
                    int av, bool is_self, const std::string& sender_username = "");
    void RenderChatMsg(const ChatLine& msg);
    static std::vector<PlayerInfo> ParsePlayerList(const std::string& body);
    void TrySendChat(ChatNetworkClient& net);
    void TrySendPM(ChatNetworkClient& net);
    void RenderPMTab(ChatNetworkClient& net);
};