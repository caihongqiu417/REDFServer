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

    /* 3. 根据当前状态显示不同界面 */
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
        /* "昵称:" 标签 → 红色 */
        ImGui::TextColored(kRed, "昵称:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputText("##nick", nick_buf_, sizeof(nick_buf_));
        ImGui::SameLine();

        bool can_join = net.IsConnected() && nick_buf_[0] != '\0';

        /* Join 按钮：红底白字 */
        ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
        if (ImGui::Button("Join") && can_join) {
            net.SendJoin(nick_buf_);
            players_.push_back(std::string(nick_buf_) + " (你)");
            AppendMsg(std::string("[系统] 你以昵称 \"") + nick_buf_ + "\" 加入聊天室");
            joined_ = true;
            focus_input_ = true;
        }
        ImGui::PopStyleColor();

        if (!net.IsConnected()) {
            ImGui::SameLine();
            ImGui::TextColored(kRedHov, "● 未连接");
        } else {
            ImGui::SameLine();
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
