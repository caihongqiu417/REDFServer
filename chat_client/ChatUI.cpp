#include "ChatUI.h"
#include "imgui.h"
#include <arpa/inet.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

/* ------------------------------------------------------------------ */
/*  统一色板（与 theme_red_white.xml 保持一致）                         */
/* ------------------------------------------------------------------ */
static const ImVec4 kRed     = {0.76f, 0.05f, 0.05f, 1.00f};  /* 品牌红  #C20C0C */
static const ImVec4 kRedHov  = {0.88f, 0.08f, 0.08f, 1.00f};  /* 高亮红  Hover   */
static const ImVec4 kRedDark = {0.60f, 0.04f, 0.04f, 1.00f};  /* 深压红  Active  */
static const ImVec4 kRedDim  = {0.76f, 0.05f, 0.05f, 0.65f};  /* 半透红  系统消息 */
static const ImVec4 kWhite   = {1.00f, 1.00f, 1.00f, 1.00f};  /* 白色    按钮文字 */
static const ImVec4 kGray    = {0.46f, 0.46f, 0.46f, 1.00f};  /* 次要灰  */

/* ------------------------------------------------------------------ */
/*  根据消息前缀选色并渲染一行聊天文本                                  */
/* ------------------------------------------------------------------ */
static void RenderChatLine(const std::string& line)
{
    /* UTF-8 前缀匹配：[系统] / [你] / 其他 */
    if (line.compare(0, 7, "[系统]") == 0) {          /* "[系统]" = 7 bytes UTF-8 */
        ImGui::TextColored(kRedDim, "%s", line.c_str());
    } else if (line.compare(0, 5, "[你]") == 0) {     /* "[你]"  = 5 bytes UTF-8  */
        ImGui::TextColored(kRedDark, "%s", line.c_str());
    } else {
        ImGui::TextUnformatted(line.c_str());
    }
}

/* ================================================================== */

ChatUI::ChatUI()
{
    AppendMsg("[系统] 请先登陆或注册账号");
}
/* ------------------------------------------------------------------ */
/*  Render — 每帧入口                                                  */
/* ------------------------------------------------------------------ */
/* ================================================================
 * 主渲染函数：每帧调用一次，绘制整个客户端 UI
 * ================================================================ */
void ChatUI::Render(ChatNetworkClient& net)
{
    /* 1. 先处理服务器发来的所有消息（更新聊天记录、玩家列表等） */
    HandleIncoming(net);

    /* 2. 设置全屏无边框 ImGui 窗口 */
    ImGuiIO& io = ImGui::GetIO();                   // 获取屏幕信息（宽高等）
    ImGui::SetNextWindowPos(ImVec2(0, 0));          // 窗口左上角对齐屏幕 (0,0)
    ImGui::SetNextWindowSize(io.DisplaySize);        // 窗口大小铺满整个屏幕
    ImGui::Begin("GameChat", nullptr,               // 开始绘制，窗口内部名 "GameChat"
        ImGuiWindowFlags_NoTitleBar |               // 隐藏标题栏
        ImGuiWindowFlags_NoResize   |               // 禁止拖拽边框调整大小
        ImGuiWindowFlags_NoMove     |               // 禁止拖拽标题栏移动
        ImGuiWindowFlags_NoScrollbar);              // 窗口本身不显示滚动条

    /* 3. 主页弹窗触发
     * 思路：ImGui 的 Popup 系统要求 OpenPopup 与 BeginPopupModal 在同一窗口层级里调用。
     * "GameChat" 是最外层窗口，所以弹窗的控制逻辑放在这里最安全。
     * open_profile_ 由子窗口（侧边栏）里的头像按钮设置，
     * 这里检测到后调用 OpenPopup，再每帧调用 RenderProfileModal 渲染弹窗内容。*/
    if (open_profile_) {
        ImGui::OpenPopup("##profile_modal");
        open_profile_ = false;   /* 重置标志，防止重复打开 */
    }
    RenderProfileModal(net);     /* 每帧都调用；弹窗关闭时内部立刻返回，几乎零开销 */

    /* 4. 根据当前状态显示不同界面 */
    if (ui_state_ == UiState::LOGIN || ui_state_ == UiState::WAITING) {
        RenderLoginPage(net);
    } else {
        /* 左侧固定宽度的图标导航栏 */
        ImGui::BeginChild("##navsidebar", ImVec2(64.0f, -1), false);
        RenderNavSidebar();
        ImGui::EndChild();

        ImGui::SameLine();

        /* 右侧内容区（剩余全部宽度） */
        ImGui::BeginChild("##maincontent", ImVec2(-1, -1), false);
        if (main_tab_ == MainTab::WORLD) {
            RenderLoginBar(net);
            ImGui::Separator();
            RenderPlayerList();
            ImGui::SameLine();
            RenderChatArea(net);
        } else {
            /* 好友 / 私聊功能占位 */
            float h = ImGui::GetContentRegionAvail().y;
            ImGui::SetCursorPosY(h * 0.45f);
            const char* tip = (main_tab_ == MainTab::FRIENDS) ? "好友功能即将上线..." : "私聊功能即将上线...";
            float tw = ImGui::CalcTextSize(tip).x;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tw) * 0.5f);
            ImGui::TextColored(kGray, "%s", tip);
        }
        ImGui::EndChild();
    }

    /* 4. 结束本帧绘制 */
    ImGui::End();
}

/* ================================================================
 * 处理服务器发来的所有消息，更新 UI 状态
 * 每帧 Render 开头调用一次
 * ================================================================ */
void ChatUI::HandleIncoming(ChatNetworkClient& net)
{
    /* --- 断线检测：在聊天室状态但连接断了 → 退回登录页 --- */
    if (!net.IsConnected() && ui_state_ == UiState::CHAT) {
        AppendMsg("[系统] 与服务器连接断开");
        joined_    = false;
        ui_state_  = UiState::LOGIN;                  // 退回登录界面
        login_error_ = "连接已断开，请重新登陆";       // 显示红色提示
    }

      /* --- 取出接收队列中所有消息，逐条处理 --- */
    auto msgs = net.Poll();
    for (auto& m : msgs) {
        switch (m.type) {

        /* ========================================================
         * 登录响应 / 注册响应
         * ======================================================== */
        case MSG_LOGIN_RSP:
        case MSG_REG_RSP: {
            if (m.body.size() < sizeof(MsgLoginRsp)) break;    // 包太短，忽略
            MsgLoginRsp rsp{};
            memcpy(&rsp, m.body.data(), sizeof(MsgLoginRsp));  // 拷贝响应结构体
            rsp.reason[MSG_LOGIN_REASON_LEN - 1] = '\0';       // 防溢出

            if (rsp.result == 1) {
                /* 登录/注册成功 → 进入聊天室 */
                ui_state_    = UiState::CHAT;
                login_error_ = "";
                // 用登录用户名预填昵称，方便用户直接使用
                strncpy(nick_buf_, user_buf_, sizeof(nick_buf_) - 1);
                AppendMsg(std::string("[系统] 登陆成功，欢迎 ") + user_buf_);
            } else {
                /* 失败 → 留在登录页，显示服务器返回的原因 */
                ui_state_    = UiState::LOGIN;
                login_error_ = std::string(rsp.reason);        // "用户名已存在" / "用户名或密码错误"
            }
            break;
        }

        /* ========================================================
         * 玩家列表（登录后服务器主动发送）
         * ======================================================== */
        case MSG_PLAYER_LIST: {
            players_ = ParsePlayerList(m.body);                // 解析二进制格式的玩家列表
            char buf[128];
            snprintf(buf, sizeof(buf), "[系统] 当前在线 %zu 人", players_.size());
            AppendMsg(buf);
            break;
        }

        /* ========================================================
         * 有人加入聊天室（服务器广播）
         * ======================================================== */
        case MSG_JOIN: {
            std::string nick = m.body.empty() ? "unknown" : m.body;
            players_.push_back(nick);                          // 添加到本地玩家列表
            AppendMsg("[系统] " + nick + " 加入聊天室");
            break;
        }

        /* ========================================================
         * 有人离开聊天室（服务器广播）
         * ======================================================== */
        case MSG_LEAVE: {
            std::string nick = m.body.empty() ? "unknown" : m.body;
            auto it = std::find(players_.begin(), players_.end(), nick);
            if (it != players_.end()) players_.erase(it);      // 从本地玩家列表移除
            AppendMsg("[系统] " + nick + " 离开聊天室");
            break;
        }

        /* ========================================================
         * 聊天消息（他人发送，服务器转发）
         * ======================================================== */
        case MSG_CHAT:
            AppendMsg(m.body);                                 // body 已含 "昵称: 消息文本"
            break;

        /* 服务器登录后推送的主页数据（昵称 + 头像序号） */
        case MSG_PROFILE_RSP: {
            if (m.body.size() < sizeof(MsgProfileData)) break;
            MsgProfileData prof{};
            memcpy(&prof, m.body.data(), sizeof(MsgProfileData));
            prof.nickname[sizeof(prof.nickname) - 1] = '\0';

            /* 用服务器存的数据覆盖本地（服务器是权威来源） */
            strncpy(nick_buf_,          prof.nickname, sizeof(nick_buf_)          - 1);
            strncpy(profile_nick_buf_,  prof.nickname, sizeof(profile_nick_buf_)  - 1);
            current_avatar_     = prof.avatar_idx;
            profile_sel_avatar_ = prof.avatar_idx;
            break;
        }

        /* 服务器返回的主页保存结果 */
        case MSG_PROFILE_SET_RSP: {
            if (m.body.size() < sizeof(MsgLoginRsp)) break;
            MsgLoginRsp rsp{};
            memcpy(&rsp, m.body.data(), sizeof(MsgLoginRsp));
            rsp.reason[sizeof(rsp.reason) - 1] = '\0';
            if (rsp.result == 1) {
                AppendMsg("[系统] 主页已保存");
            } else {
                AppendMsg(std::string("[系统] 主页保存失败: ") + rsp.reason);
                /* 保存失败：回滚本地状态，避免客户端与服务器不一致 */
                strncpy(nick_buf_,      profile_nick_buf_, sizeof(nick_buf_)      - 1);
                current_avatar_ = profile_sel_avatar_;
            }
            break;
        }

        default:
            break;                                             // 未知消息类型，忽略
        }
    }
}

/* ------------------------------------------------------------------ */
/*  顶部栏：昵称输入 / 在线状态                                         */
/* ------------------------------------------------------------------ */
void ChatUI::RenderLoginBar(ChatNetworkClient& net)
{
    if (!joined_) {
        /* 昵称来自主页设置，这里只读显示，不可编辑 */
        ImGui::TextColored(kGray, "昵称:");
        ImGui::SameLine();
        if (nick_buf_[0] != '\0') {
            ImGui::TextColored(kRed, "%s", nick_buf_);
        } else {
            ImGui::TextColored(kGray, "（请在主页设置昵称）");
        }
        ImGui::SameLine();

        bool can_join = net.IsConnected() && nick_buf_[0] != '\0';
        ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
        if (ImGui::Button("Join") && can_join) {
            net.SendJoin(nick_buf_);
            players_.push_back(std::string(nick_buf_) + " (你)");
            AppendMsg(std::string("[系统] 你以昵称 \"") + nick_buf_ + "\" 加入聊天室");
            joined_ = true;
            focus_input_ = true;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (!net.IsConnected()) {
            ImGui::TextColored(kRedHov, "● 未连接");
        } else {
            ImGui::TextColored(ImVec4(0.10f, 0.78f, 0.10f, 1.00f), "●");
            ImGui::SameLine();
            ImGui::Text(" 已连接");
        }
    } else {
        /* 已加入：昵称用红色高亮 */
        ImGui::TextColored(kGray, "在线聊天中   昵称:");
        ImGui::SameLine();
        ImGui::TextColored(kRed, "%s", nick_buf_);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70);

        /* Leave 按钮：红底白字 */
        ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
        if (ImGui::Button("Leave")) {
            net.SendLeave(nick_buf_);
            std::string self = std::string(nick_buf_) + " (你)";
            auto it = std::find(players_.begin(), players_.end(), self);
            if (it != players_.end()) players_.erase(it);
            joined_ = false;
            AppendMsg("[系统] 你已离开聊天室");
        }
        ImGui::PopStyleColor();
    }
}

/* ------------------------------------------------------------------ */
/*  左栏：在线玩家列表                                                  */
/* ------------------------------------------------------------------ */
void ChatUI::RenderPlayerList()
{
    float avail_h = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##playerlist", ImVec2(158, avail_h), true);

    /* 标题：红色 + 计数 */
    ImGui::TextColored(kRed, "在线玩家");
    ImGui::SameLine();
    ImGui::TextColored(kGray, "(%zu)", players_.size());

    /* 红色分隔线 */
    ImGui::Separator();

    /* 玩家列表：红色子弹点 + 默认文字 */
    for (auto& p : players_) {
        ImGui::PushStyleColor(ImGuiCol_Text, kRed);
        ImGui::Bullet();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(p.c_str());
    }

    ImGui::EndChild();
}

/* ------------------------------------------------------------------ */
/*  右栏：聊天历史 + 输入框                                             */
/* ------------------------------------------------------------------ */
void ChatUI::RenderChatArea(ChatNetworkClient& net)
{
    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;
    float input_h = ImGui::GetFrameHeightWithSpacing() + 6;

    /* 聊天历史滚动区 */
    ImGui::BeginChild("##chat_scroll",
                      ImVec2(avail_w, avail_h - input_h),
                      true);
    for (auto& line : messages_) {
        RenderChatLine(line);
    }
    if (scroll_bottom_) {
        ImGui::SetScrollHereY(1.0f);
        scroll_bottom_ = false;
    }
    ImGui::EndChild();

    /* 输入框 + Send 按钮 */
    if (focus_input_) {
        ImGui::SetKeyboardFocusHere();
        focus_input_ = false;
    }
    ImGui::SetNextItemWidth(avail_w - 74);
    bool enter_pressed = ImGui::InputText(
        "##chatinput", chat_buf_, sizeof(chat_buf_),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();

    bool can_send = joined_ && net.IsConnected() && chat_buf_[0] != '\0';

    /* Send 按钮：红底白字 */
    ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
    if ((ImGui::Button("Send") || enter_pressed) && can_send) {
        TrySendChat(net);
    }
    ImGui::PopStyleColor();
}

/* ------------------------------------------------------------------ */
/*  图标设置                                                            */
/* ------------------------------------------------------------------ */
void ChatUI::SetIcons(unsigned int world, unsigned int friend_icon, unsigned int private_icon)
{
    icon_world_   = world;
    icon_friend_  = friend_icon;
    icon_private_ = private_icon;
}

void ChatUI::SetAvatars(unsigned int* textures, int count)
{
    avatar_count_ = (count > MAX_AVATARS) ? MAX_AVATARS : count;
    for (int i = 0; i < avatar_count_; i++)
        avatar_textures_[i] = textures[i];
}

/* ------------------------------------------------------------------ */
/*  左侧导航栏                                                          */
/* ------------------------------------------------------------------ */
void ChatUI::RenderNavSidebar()
{
    const float radius   = 20.0f;   /* 圆圈半径 */
    const float icon_sz  = 26.0f;   /* 图标显示尺寸（正方形边长） */
    const float sidebar_w = 64.0f;
    const float cx = sidebar_w * 0.5f;   /* 水平中心 */

    struct TabDef { MainTab tab; unsigned int* tex; const char* tip; };
    TabDef tabs[] = {
        { MainTab::WORLD,   &icon_world_,   "世界" },
        { MainTab::FRIENDS, &icon_friend_,  "好友" },
        { MainTab::PRIVATE, &icon_private_, "私聊" },
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos  = ImGui::GetWindowPos();

    float cursor_y = 14.0f;
    for (auto& t : tabs) {
        bool active = (main_tab_ == t.tab);

        /* 用 InvisibleButton 捕捉点击/悬停 */
        ImGui::SetCursorPos(ImVec2(cx - radius, cursor_y));
        char id[32];
        snprintf(id, sizeof(id), "##nav_%d", (int)t.tab);
        ImGui::InvisibleButton(id, ImVec2(radius * 2.0f, radius * 2.0f));
        bool clicked = ImGui::IsItemClicked();
        bool hovered = ImGui::IsItemHovered();
        if (clicked) main_tab_ = t.tab;
        if (hovered) ImGui::SetTooltip("%s", t.tip);

        /* 圆圈中心（屏幕坐标） */
        ImVec2 center = { win_pos.x + cx, win_pos.y + cursor_y + radius };

        /* 圆圈颜色：选中→红，悬停→浅红，默认→灰 */
        ImU32 ring_col;
        float thickness;
        if (active) {
            ring_col  = IM_COL32(194, 13, 13, 230);
            thickness = 2.0f;
        } else if (hovered) {
            ring_col  = IM_COL32(220, 80, 80, 180);
            thickness = 1.5f;
        } else {
            ring_col  = IM_COL32(140, 140, 140, 130);
            thickness = 1.5f;
        }
        dl->AddCircle(center, radius, ring_col, 48, thickness);

        /* 图标居中绘制（屏幕坐标） */
        float half = icon_sz * 0.5f;
        ImVec2 icon_min = { center.x - half, center.y - half };
        ImVec2 icon_max = { center.x + half, center.y + half };
        dl->AddImage((void*)(uintptr_t)(*t.tex), icon_min, icon_max);

        cursor_y += radius * 2.0f + 12.0f;   /* 下一个图标的起始 y */
    }

    /* ================================================================
     * 底部头像入口
     * 思路：
     *   导航栏高度固定（-1 = 撑满父窗口），所以可以用 GetWindowHeight()
     *   取到整条侧边栏的像素高度，再从底部往上留 10px 定位头像圆心。
     *
     *   头像用 AddImageRounded 画成圆形：
     *     rounding 参数等于半径时，图片的四个角恰好被裁成圆弧，
     *     整体看起来就是一个圆形头像，无需额外裁剪。
     *
     *   点击时把当前昵称/头像序号复制到"临时编辑缓冲"，
     *   再设 open_profile_=true，下一帧 Render() 会开启弹窗。
     * ================================================================ */
    {
        const float av_r  = 22.0f;                          /* 头像圆半径，单位像素 */
        const float av_cx = sidebar_w * 0.5f;               /* 侧边栏水平中心 */
        /* win_h 是当前子窗口（侧边栏）的完整像素高度 */
        float win_h   = ImGui::GetWindowHeight();
        /* 头像顶部 y（相对于子窗口左上角） */
        float av_ytop = win_h - av_r * 2.0f - 10.0f;

        /* InvisibleButton：ImGui 用于捕捉鼠标事件的不可见矩形区域。
         * 大小与头像圆恰好吻合，点击时触发 IsItemClicked()。*/
        /* SetCursorPos 设置相对坐标后，立刻调用 GetCursorScreenPos()，
         * 让 ImGui 把相对坐标转成屏幕绝对坐标（自动处理窗口偏移和内边距）。
         * 这比手动用 GetWindowPos() + 偏移量更准确，不会受子窗口内边距影响。*/
        ImGui::SetCursorPos(ImVec2(av_cx - av_r, av_ytop));
        ImVec2 av_min = ImGui::GetCursorScreenPos();   /* 头像矩形左上角的屏幕坐标 */
        ImGui::InvisibleButton("##avatar_entry", ImVec2(av_r * 2.0f, av_r * 2.0f));
        bool av_hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
            strncpy(profile_nick_buf_, nick_buf_, sizeof(profile_nick_buf_) - 1);
            profile_sel_avatar_ = current_avatar_;
            open_profile_ = true;
        }
        if (av_hovered) ImGui::SetTooltip("我的主页");

        /* 圆心 = 矩形左上角 + 半径，所以图片恰好填满 InvisibleButton 区域 */
        ImVec2 avctr = { av_min.x + av_r, av_min.y + av_r };

        if (avatar_count_ > 0) {
            /* AddImageRounded：rounding = av_r（= 图片边长一半）时裁成完整圆形 */
            dl->AddImageRounded(
                (void*)(uintptr_t)avatar_textures_[current_avatar_],
                av_min,
                ImVec2(av_min.x + av_r * 2.0f, av_min.y + av_r * 2.0f),
                ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(255, 255, 255, 255), av_r
            );
        } else {
            dl->AddCircleFilled(avctr, av_r, IM_COL32(80, 80, 80, 200), 48);
        }

        ImU32 ring = av_hovered
            ? IM_COL32(255, 255, 255, 180)
            : IM_COL32(160, 160, 160, 120);
        dl->AddCircle(avctr, av_r, ring, 48, av_hovered ? 2.0f : 1.5f);
    }
}

/* ------------------------------------------------------------------ */
/*  工具函数                                                            */
/* ------------------------------------------------------------------ */
//往聊天记录里加一行，超过 500 条就删最旧的
void ChatUI::AppendMsg(const std::string& line)
{
    messages_.push_back(line);
    scroll_bottom_ = true;
    if (messages_.size() > 500)
        messages_.erase(messages_.begin());
}

/* ------------------------------------------------------------------ */
/*  登陆页面：居中显示用户名/密码输入框 + 登陆/注册按钮                  */
/* ------------------------------------------------------------------ */
void ChatUI::RenderLoginPage(ChatNetworkClient& net)
{
    ImGuiIO& io = ImGui::GetIO();

    // 登陆面板居中
    float panel_w = 320.0f;
    float panel_h = 210.0f;
    ImGui::SetCursorPos(ImVec2(
        (io.DisplaySize.x - panel_w) * 0.5f,
        (io.DisplaySize.y - panel_h) * 0.5f));

    ImGui::BeginChild("##login_panel", ImVec2(panel_w, panel_h), true);

    // 标题
    ImGui::SetCursorPosX((panel_w - ImGui::CalcTextSize("REDF Chat").x) * 0.5f);
    ImGui::TextColored(kRed, "REDF Chat");
    ImGui::Separator();
    ImGui::Spacing();

    // 用户名
    ImGui::TextColored(kGray, "用户名");
    ImGui::SetNextItemWidth(panel_w - 20);
    ImGui::InputText("##user", user_buf_, sizeof(user_buf_));
    ImGui::Spacing();

    // 密码
    ImGui::TextColored(kGray, "密码");
    ImGui::SetNextItemWidth(panel_w - 20);
    ImGui::InputText("##pass", pass_buf_, sizeof(pass_buf_),
                     ImGuiInputTextFlags_Password);
    ImGui::Spacing();

    // 错误提示
    if (!login_error_.empty())
        ImGui::TextColored(kRedHov, "%s", login_error_.c_str());
    else
        ImGui::Spacing();

    ImGui::Spacing();

    bool can_act = net.IsConnected() &&
                   user_buf_[0] != '\0' &&
                   pass_buf_[0] != '\0' &&
                   ui_state_ != UiState::WAITING;

    // 登陆按钮（红底白字）
    ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
    if (ImGui::Button("登陆", ImVec2(140, 0)) && can_act) {
        ui_state_ = UiState::WAITING;
        login_error_ = "";
        net.SendLoginReq(user_buf_, pass_buf_);
    }
    ImGui::SameLine();
    if (ImGui::Button("注册", ImVec2(140, 0)) && can_act) {
        ui_state_ = UiState::WAITING;
        login_error_ = "";
        net.SendRegisterReq(user_buf_, pass_buf_);
    }
    ImGui::PopStyleColor();

    // 验证中提示
    if (ui_state_ == UiState::WAITING) {
        ImGui::Spacing();
        ImGui::SetCursorPosX((panel_w - ImGui::CalcTextSize("验证中...").x) * 0.5f);
        ImGui::TextColored(kGray, "验证中...");
    }

    // 未连接提示
    if (!net.IsConnected()) {
        ImGui::Spacing();
        ImGui::TextColored(kRedHov, "● 未连接服务器");
    }

    ImGui::EndChild();
}

void ChatUI::TrySendChat(ChatNetworkClient& net)
{
    std::string text(chat_buf_);
    if (text.empty()) return;
    net.SendChat(text);
    AppendMsg(std::string("[你] ") + text);
    chat_buf_[0] = '\0';
    focus_input_  = true;
}

std::vector<std::string> ChatUI::ParsePlayerList(const std::string& body)
{
    std::vector<std::string> result;
    if (body.size() < 2) return result;

    uint16_t num;
    memcpy(&num, body.data(), 2);
    num = ntohs(num);

    size_t offset = 2;
    for (uint16_t i = 0; i < num; i++) {
        if (offset + 2 > body.size()) break;
        uint16_t slen;
        memcpy(&slen, body.data() + offset, 2);
        slen = ntohs(slen);
        offset += 2;
        if (offset + slen > body.size()) break;
        result.push_back(body.substr(offset, slen));
        offset += slen;
    }
    return result;
}

/* ================================================================
 * 主页弹窗
 * 思路：
 *   弹窗分两列：
 *     左列（宽约 110px）：大号头像预览 + 用户名（账号）显示
 *     右列（剩余宽度）  ：头像选择网格 + 昵称输入框 + 保存/取消按钮
 *
 *   头像网格：把所有头像缩略图排成一行（或多行），点击选中高亮。
 *     高亮用 DrawList->AddRect 在缩略图外面画红色边框实现，
 *     不需要改按钮样式，Push/Pop 更少，更安全。
 *
 *   保存逻辑（当前阶段只存本地，服务器同步在下一步实现）：
 *     把 profile_nick_buf_ 写回 nick_buf_，
 *     把 profile_sel_avatar_ 写回 current_avatar_，
 *     然后关闭弹窗。
 *
 *   BeginPopupModal 的特点：
 *     - 调用后会阻断背景窗口的输入（背景变暗），用户只能操作弹窗
 *     - 关闭方式：调用 ImGui::CloseCurrentPopup()
 * ================================================================ */
void ChatUI::RenderProfileModal(ChatNetworkClient& net)
{
    /* BeginPopupModal 返回 false 说明弹窗当前没有打开，直接返回 */
    ImGuiIO& io   = ImGui::GetIO();
    ImVec2 msz    = ImVec2(480.0f, 300.0f);   /* 弹窗尺寸 */

    /* 每次弹窗打开时强制居中（ImGuiCond_Always = 每帧都设置） */
    ImGui::SetNextWindowPos(
        ImVec2((io.DisplaySize.x - msz.x) * 0.5f,
               (io.DisplaySize.y - msz.y) * 0.5f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(msz);

    if (!ImGui::BeginPopupModal("##profile_modal", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize   |
            ImGuiWindowFlags_NoMove))
        return;   /* 弹窗未打开，什么都不做 */

    ImDrawList* dl  = ImGui::GetWindowDrawList();

    /* ---- 标题栏区域 ---- */
    ImGui::Spacing();
    ImGui::SetCursorPosX(14.0f);
    ImGui::TextColored(kRed, "我的主页");
    ImGui::SameLine(msz.x - 28.0f);
    /* 右上角关闭：把按钮四种状态的背景色全部设为透明，只剩红色文字 "×" */
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Text,          kRed);
    if (ImGui::Button(u8"\xc3\x97##close_profile"))   /* × = UTF-8 U+00D7 */
        ImGui::CloseCurrentPopup();
    ImGui::PopStyleColor(4);
    ImGui::Separator();

    /* ================================================================
     * 左列：头像大图预览 + 账号名
     * 宽度固定 110px，用 BeginChild 隔离布局，防止影响右列坐标计算
     * ================================================================ */
    ImGui::BeginChild("##prof_left", ImVec2(110.0f, -1), false);
    {
        const float pv_r  = 40.0f;
        const float pv_cx = 55.0f;   /* 左列宽 110 / 2 */

        /* 垂直居中：左列可用高度一半减去头像半径，得到头像顶部 y。
         * GetContentRegionAvail().y 返回当前子窗口内剩余可绘制高度。*/
        float col_h  = ImGui::GetContentRegionAvail().y;
        float top_y  = (col_h - pv_r * 2.0f) * 0.5f;   /* 居中 y（相对子窗口） */
        if (top_y < 4.0f) top_y = 4.0f;                 /* 至少留 4px 顶部间距 */

        ImGui::SetCursorPos(ImVec2(pv_cx - pv_r, top_y));
        ImVec2 pv_min = ImGui::GetCursorScreenPos();
        ImVec2 pvctr  = { pv_min.x + pv_r, pv_min.y + pv_r };

        /* 子窗口内调用 GetWindowDrawList，返回带正确裁剪区的画布 */
        ImDrawList* cdl = ImGui::GetWindowDrawList();
        if (avatar_count_ > 0 && profile_sel_avatar_ < avatar_count_) {
            cdl->AddImageRounded(
                (void*)(uintptr_t)avatar_textures_[profile_sel_avatar_],
                pv_min,
                ImVec2(pv_min.x + pv_r * 2.0f, pv_min.y + pv_r * 2.0f),
                ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(255, 255, 255, 255), pv_r);
            cdl->AddCircle(pvctr, pv_r, IM_COL32(194, 13, 13, 200), 48, 2.0f);
        }

        /* Dummy 占位：告知 ImGui 这片空间已被头像占用，光标跳过去 */
        ImGui::Dummy(ImVec2(pv_r * 2.0f, pv_r * 2.0f + 10.0f));
        ImGui::SetCursorPosX(4.0f);
        ImGui::TextColored(kGray, "@%s", user_buf_[0] ? user_buf_ : "unknown");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    /* ================================================================
     * 右列：头像选择网格 + 昵称输入 + 操作按钮
     * ================================================================ */
    ImGui::BeginChild("##prof_right", ImVec2(-1, -1), false);
    {
        /* ---- 头像网格 ---- */
        ImGui::TextColored(kGray, "选择头像");
        const float thumb = 44.0f;   /* 缩略图边长 */
        const float gap   = 6.0f;    /* 缩略图间距 */
        float avail_w     = ImGui::GetContentRegionAvail().x;
        /* 每行能放几个缩略图（向下取整） */
        int per_row = (int)((avail_w + gap) / (thumb + gap));
        if (per_row < 1) per_row = 1;

        for (int i = 0; i < avatar_count_; i++) {
            int col = i % per_row;
            if (col == 0 && i > 0) {
                /* 换行：ImGui 默认每个控件后换行，SameLine 让同行控件不换 */
            } else if (col > 0) {
                ImGui::SameLine(0.0f, gap);
            }

            /* 选中时：在缩略图外画红框（用 DrawList，不影响 Push/Pop 栈） */
            if (profile_sel_avatar_ == i) {
                ImVec2 sp = ImGui::GetCursorScreenPos();   /* 当前绘制位置屏幕坐标 */
                dl->AddRect(
                    ImVec2(sp.x - 2, sp.y - 2),
                    ImVec2(sp.x + thumb + 2, sp.y + thumb + 2),
                    IM_COL32(194, 13, 13, 220), 4.0f, 0, 2.0f
                );
            }

            char btn_id[16];
            snprintf(btn_id, sizeof(btn_id), "##av_%d", i);
            /* PushStyleVar FramePadding=(0,0)：消除 ImageButton 的内边距，
             * 让图片完整填满按钮区域而不是缩小显示在左上角。
             * 配合上面 AddRect 的 sp+thumb 计算，选中框大小与图片完全对齐。*/
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            if (ImGui::ImageButton(btn_id,
                    (void*)(uintptr_t)avatar_textures_[i],
                    ImVec2(thumb, thumb),
                    ImVec2(0, 0), ImVec2(1, 1),
                    ImVec4(0, 0, 0, 0)))
                profile_sel_avatar_ = i;
            ImGui::PopStyleVar();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        /* ---- 昵称输入 ---- */
        ImGui::TextColored(kGray, "昵称");
        ImGui::SetNextItemWidth(-1);   /* -1 = 占满右列剩余宽度 */
        ImGui::InputText("##pnick", profile_nick_buf_, sizeof(profile_nick_buf_));

        ImGui::Spacing();

        /* ---- 保存 / 取消 ---- */
        const float btn_w = 80.0f;
        /* 按钮居右：从右边留 btn_w*2+gap 的位置开始 */
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - btn_w * 2.0f - gap);

        ImGui::PushStyleColor(ImGuiCol_Text, kWhite);

        bool can_save = profile_nick_buf_[0] != '\0';
        if (ImGui::Button("保存##prof", ImVec2(btn_w, 0)) && can_save) {
            /* 立即更新本地显示（乐观更新：先改界面，等服务器确认）。
             * 若服务器返回失败，MSG_PROFILE_SET_RSP 处理里会回滚。 */
            strncpy(nick_buf_, profile_nick_buf_, sizeof(nick_buf_) - 1);
            current_avatar_ = profile_sel_avatar_;
            /* 发给服务器保存，结果由 HandleIncoming 的 MSG_PROFILE_SET_RSP 分支处理 */
            net.SendProfileSetReq(std::string(profile_nick_buf_), profile_sel_avatar_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0.0f, gap);
        if (ImGui::Button("取消##prof", ImVec2(btn_w, 0))) {
            /* 丢弃临时修改，不写回正式变量 */
            ImGui::CloseCurrentPopup();
        }

        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    ImGui::EndPopup();
}
