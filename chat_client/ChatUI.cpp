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
    AppendMsg("[系统] 请输入昵称并点击 Join 加入聊天室");
}

/* ------------------------------------------------------------------ */
/*  Render — 每帧入口                                                  */
/* ------------------------------------------------------------------ */
void ChatUI::Render(ChatNetworkClient& net)
{
    HandleIncoming(net);

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("GameChat", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoScrollbar);

    RenderLoginBar(net);

    /* 红色分隔线（颜色由 XML Separator 控制） */
    ImGui::Separator();

    RenderPlayerList();
    ImGui::SameLine();
    RenderChatArea(net);

    ImGui::End();
}

/* ------------------------------------------------------------------ */
/*  处理网络消息                                                        */
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
            players_ = ParsePlayerList(m.body);
            char buf[128];
            snprintf(buf, sizeof(buf), "[系统] 当前在线 %zu 人", players_.size());
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
        case MSG_CHAT:
            AppendMsg("[他人] " + m.body);
            break;
        default:
            break;
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
        if (!can_join) ImGui::BeginDisabled();

        /* Join 按钮：红底白字 */
        ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
        if (ImGui::Button("Join")) {
            net.SendJoin(nick_buf_);
            players_.push_back(std::string(nick_buf_) + " (你)");
            AppendMsg(std::string("[系统] 你以昵称 \"") + nick_buf_ + "\" 加入聊天室");
            joined_ = true;
            focus_input_ = true;
        }
        ImGui::PopStyleColor();

        if (!can_join) ImGui::EndDisabled();

        if (!net.IsConnected()) {
            ImGui::SameLine();
            ImGui::TextColored(kRedHov, "● 未连接");
        } else {
            ImGui::SameLine();
            ImGui::TextColored(kGray, "● 已连接");
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
    if (!can_send) ImGui::BeginDisabled();

    /* Send 按钮：红底白字 */
    ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
    if (ImGui::Button("Send") || (enter_pressed && can_send)) {
        TrySendChat(net);
    }
    ImGui::PopStyleColor();

    if (!can_send) ImGui::EndDisabled();
}

/* ------------------------------------------------------------------ */
/*  工具函数                                                            */
/* ------------------------------------------------------------------ */
void ChatUI::AppendMsg(const std::string& line)
{
    messages_.push_back(line);
    scroll_bottom_ = true;
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
