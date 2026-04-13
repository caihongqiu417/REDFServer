#include "ChatUI.h"
#include "imgui.h"
#include <arpa/inet.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

ChatUI::ChatUI()
{
    AppendMsg("[系统] 请输入昵称并点击 Join 加入聊天室");
}

/* ------------------------------------------------------------------ */
/*  Render — 每帧入口                                                  */
/* ------------------------------------------------------------------ */
void ChatUI::Render(ChatNetworkClient& net)
{
    HandleIncoming(net);

    /* 全屏主窗口，无边距、无移动 */
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("GameChat", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoScrollbar);

    RenderLoginBar(net);
    ImGui::Separator();

    /* 双列布局：左=玩家列表，右=聊天区 */
    float list_width = 160.0f;
    RenderPlayerList();
    ImGui::SameLine();
    RenderChatArea(net);

    (void)list_width;
    ImGui::End();
}

/* ------------------------------------------------------------------ */
/*  处理网络消息，更新 players_ / messages_                            */
/* ------------------------------------------------------------------ */
void ChatUI::HandleIncoming(ChatNetworkClient& net)
{
    if (!net.IsConnected() && joined_) {
        AppendMsg("[系统] 与服务器连接断开");
        joined_ = false;
    }

    auto msgs = net.Poll();
    for (auto& m : msgs) {
        switch (m.type) {
        case MSG_PLAYER_LIST: {
            /* 初始在线列表：重置 players_ */
            players_ = ParsePlayerList(m.body);
            char buf[128];
            snprintf(buf, sizeof(buf), "[系统] 当前在线 %zu 人",
                     players_.size());
            AppendMsg(buf);
            break;
        }
        case MSG_JOIN: {
            std::string nick = m.body.empty() ? "unknown" : m.body;
            players_.push_back(nick);
            AppendMsg("[系统] " + nick + " 加入聊天室");
            break;
        }
        case MSG_LEAVE: {
            std::string nick = m.body.empty() ? "unknown" : m.body;
            auto it = std::find(players_.begin(), players_.end(), nick);
            if (it != players_.end()) players_.erase(it);
            AppendMsg("[系统] " + nick + " 离开聊天室");
            break;
        }
        case MSG_CHAT: {
            /* 服务器广播来的聊天，格式：body 就是原文 */
            AppendMsg("[他人] " + m.body);
            break;
        }
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  顶部登录栏（未加入时）/ 状态栏（已加入时）                         */
/* ------------------------------------------------------------------ */
void ChatUI::RenderLoginBar(ChatNetworkClient& net)
{
    if (!joined_) {
        ImGui::Text("昵称:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputText("##nick", nick_buf_, sizeof(nick_buf_));
        ImGui::SameLine();

        bool can_join = net.IsConnected() && nick_buf_[0] != '\0';
        if (!can_join) ImGui::BeginDisabled();
        if (ImGui::Button("Join")) {
            net.SendJoin(nick_buf_);
            /* 将自己加入玩家列表 */
            players_.push_back(std::string(nick_buf_) + " (你)");
            AppendMsg(std::string("[系统] 你以昵称 \"") + nick_buf_ + "\" 加入聊天室");
            joined_ = true;
            focus_input_ = true;
        }
        if (!can_join) ImGui::EndDisabled();

        if (!net.IsConnected()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "未连接");
        }
    } else {
        ImGui::Text("在线聊天中  昵称: %s", nick_buf_);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
        if (ImGui::Button("Leave")) {
            net.SendLeave(nick_buf_);
            joined_ = false;
            AppendMsg("[系统] 你已离开聊天室");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  左栏：在线玩家列表                                                  */
/* ------------------------------------------------------------------ */
void ChatUI::RenderPlayerList()
{
    float avail_h = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##playerlist", ImVec2(155, avail_h), true);
    /* TextDisabled 不解析 ###，直接格式化显示 */
    ImGui::TextDisabled("在线 (%zu)", players_.size());
    ImGui::Separator();
    for (auto& p : players_) {
        ImGui::Bullet();
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
    float input_h = ImGui::GetFrameHeightWithSpacing() + 4;

    /* 聊天历史滚动区 */
    ImGui::BeginChild("##chat_scroll",
                      ImVec2(avail_w, avail_h - input_h),
                      true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& line : messages_) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (scroll_bottom_) {
        ImGui::SetScrollHereY(1.0f);
        scroll_bottom_ = false;
    }
    ImGui::EndChild();

    /* 输入框 + 发送按钮 */
    if (focus_input_) {
        ImGui::SetKeyboardFocusHere();
        focus_input_ = false;
    }
    ImGui::SetNextItemWidth(avail_w - 70);
    bool enter_pressed = ImGui::InputText(
        "##chatinput", chat_buf_, sizeof(chat_buf_),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();

    bool can_send = joined_ && net.IsConnected() && chat_buf_[0] != '\0';
    if (!can_send) ImGui::BeginDisabled();
    if (ImGui::Button("Send") || (enter_pressed && can_send)) {
        TrySendChat(net);
    }
    if (!can_send) ImGui::EndDisabled();
}

/* ------------------------------------------------------------------ */
/*  工具函数                                                            */
/* ------------------------------------------------------------------ */
void ChatUI::AppendMsg(const std::string& line)
{
    messages_.push_back(line);
    scroll_bottom_ = true;
    /* 防止历史无限增长 */
    if (messages_.size() > 500)
        messages_.erase(messages_.begin());
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

/* 解析 MSG_PLAYER_LIST body → vector<string> */
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
