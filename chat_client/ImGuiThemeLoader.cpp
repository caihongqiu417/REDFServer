#include "ImGuiThemeLoader.h"
#include "imgui.h"
#include <tinyxml2.h>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <string>

using namespace tinyxml2;

/* ------------------------------------------------------------------ */
/*  字符串 → ImGuiCol_ 枚举的映射表                                    */
/* ------------------------------------------------------------------ */
static const std::unordered_map<std::string, ImGuiCol> kColorMap = {
    {"Text",                    ImGuiCol_Text},
    {"TextDisabled",            ImGuiCol_TextDisabled},
    {"WindowBg",                ImGuiCol_WindowBg},
    {"ChildBg",                 ImGuiCol_ChildBg},
    {"PopupBg",                 ImGuiCol_PopupBg},
    {"Border",                  ImGuiCol_Border},
    {"BorderShadow",            ImGuiCol_BorderShadow},
    {"FrameBg",                 ImGuiCol_FrameBg},
    {"FrameBgHovered",          ImGuiCol_FrameBgHovered},
    {"FrameBgActive",           ImGuiCol_FrameBgActive},
    {"TitleBg",                 ImGuiCol_TitleBg},
    {"TitleBgActive",           ImGuiCol_TitleBgActive},
    {"TitleBgCollapsed",        ImGuiCol_TitleBgCollapsed},
    {"MenuBarBg",               ImGuiCol_MenuBarBg},
    {"ScrollbarBg",             ImGuiCol_ScrollbarBg},
    {"ScrollbarGrab",           ImGuiCol_ScrollbarGrab},
    {"ScrollbarGrabHovered",    ImGuiCol_ScrollbarGrabHovered},
    {"ScrollbarGrabActive",     ImGuiCol_ScrollbarGrabActive},
    {"CheckMark",               ImGuiCol_CheckMark},
    {"SliderGrab",              ImGuiCol_SliderGrab},
    {"SliderGrabActive",        ImGuiCol_SliderGrabActive},
    {"Button",                  ImGuiCol_Button},
    {"ButtonHovered",           ImGuiCol_ButtonHovered},
    {"ButtonActive",            ImGuiCol_ButtonActive},
    {"Header",                  ImGuiCol_Header},
    {"HeaderHovered",           ImGuiCol_HeaderHovered},
    {"HeaderActive",            ImGuiCol_HeaderActive},
    {"Separator",               ImGuiCol_Separator},
    {"SeparatorHovered",        ImGuiCol_SeparatorHovered},
    {"SeparatorActive",         ImGuiCol_SeparatorActive},
    {"ResizeGrip",              ImGuiCol_ResizeGrip},
    {"ResizeGripHovered",       ImGuiCol_ResizeGripHovered},
    {"ResizeGripActive",        ImGuiCol_ResizeGripActive},
    {"Tab",                     ImGuiCol_Tab},
    {"TabHovered",              ImGuiCol_TabHovered},
    {"TabActive",               ImGuiCol_TabActive},
    {"TabUnfocused",            ImGuiCol_TabUnfocused},
    {"TabUnfocusedActive",      ImGuiCol_TabUnfocusedActive},
    {"PlotLines",               ImGuiCol_PlotLines},
    {"PlotLinesHovered",        ImGuiCol_PlotLinesHovered},
    {"PlotHistogram",           ImGuiCol_PlotHistogram},
    {"PlotHistogramHovered",    ImGuiCol_PlotHistogramHovered},
    {"TableHeaderBg",           ImGuiCol_TableHeaderBg},
    {"TableBorderStrong",       ImGuiCol_TableBorderStrong},
    {"TableBorderLight",        ImGuiCol_TableBorderLight},
    {"TableRowBg",              ImGuiCol_TableRowBg},
    {"TableRowBgAlt",           ImGuiCol_TableRowBgAlt},
    {"TextSelectedBg",          ImGuiCol_TextSelectedBg},
    {"DragDropTarget",          ImGuiCol_DragDropTarget},
    {"NavHighlight",            ImGuiCol_NavHighlight},
    {"NavWindowingHighlight",   ImGuiCol_NavWindowingHighlight},
    {"NavWindowingDimBg",       ImGuiCol_NavWindowingDimBg},
    {"ModalWindowDimBg",        ImGuiCol_ModalWindowDimBg},
};

/* ------------------------------------------------------------------ */
/*  字符串 → ImGuiStyle 浮点字段的映射（用函数分发避免指针偏移问题）    */
/* ------------------------------------------------------------------ */
static void ApplyStyleFloat(ImGuiStyle& s, const char* name, float val)
{
    if (!strcmp(name, "Alpha"))              { s.Alpha = val;             return; }
    if (!strcmp(name, "WindowRounding"))     { s.WindowRounding = val;    return; }
    if (!strcmp(name, "WindowBorderSize"))   { s.WindowBorderSize = val;  return; }
    if (!strcmp(name, "ChildRounding"))      { s.ChildRounding = val;     return; }
    if (!strcmp(name, "ChildBorderSize"))    { s.ChildBorderSize = val;   return; }
    if (!strcmp(name, "PopupRounding"))      { s.PopupRounding = val;     return; }
    if (!strcmp(name, "PopupBorderSize"))    { s.PopupBorderSize = val;   return; }
    if (!strcmp(name, "FrameRounding"))      { s.FrameRounding = val;     return; }
    if (!strcmp(name, "FrameBorderSize"))    { s.FrameBorderSize = val;   return; }
    if (!strcmp(name, "IndentSpacing"))      { s.IndentSpacing = val;     return; }
    if (!strcmp(name, "ColumnsMinSpacing"))  { s.ColumnsMinSpacing = val; return; }
    if (!strcmp(name, "ScrollbarSize"))      { s.ScrollbarSize = val;     return; }
    if (!strcmp(name, "ScrollbarRounding"))  { s.ScrollbarRounding = val; return; }
    if (!strcmp(name, "GrabMinSize"))        { s.GrabMinSize = val;       return; }
    if (!strcmp(name, "GrabRounding"))       { s.GrabRounding = val;      return; }
    if (!strcmp(name, "TabRounding"))        { s.TabRounding = val;       return; }
    if (!strcmp(name, "MouseCursorScale"))   { s.MouseCursorScale = val;  return; }
    fprintf(stderr, "[Theme] 未知 style float 字段: %s\n", name);
}

static void ApplyStyleVec2(ImGuiStyle& s, const char* name, float x, float y)
{
    if (!strcmp(name, "WindowPadding"))     { s.WindowPadding     = ImVec2(x,y); return; }
    if (!strcmp(name, "FramePadding"))      { s.FramePadding      = ImVec2(x,y); return; }
    if (!strcmp(name, "ItemSpacing"))       { s.ItemSpacing        = ImVec2(x,y); return; }
    if (!strcmp(name, "ItemInnerSpacing"))  { s.ItemInnerSpacing   = ImVec2(x,y); return; }
    if (!strcmp(name, "WindowTitleAlign"))  { s.WindowTitleAlign   = ImVec2(x,y); return; }
    fprintf(stderr, "[Theme] 未知 style vec2 字段: %s\n", name);
}

/* ------------------------------------------------------------------ */
/*  LoadImGuiTheme — 公开接口                                          */
/* ------------------------------------------------------------------ */
bool LoadImGuiTheme(const char* xml_path)
{
    XMLDocument doc;
    XMLError err = doc.LoadFile(xml_path);
    if (err != XML_SUCCESS) {
        fprintf(stderr, "[Theme] 无法加载 XML: %s (error=%d)\n", xml_path, err);
        return false;
    }

    XMLElement* root = doc.FirstChildElement("theme");
    if (!root) {
        fprintf(stderr, "[Theme] XML 缺少根元素 <theme>\n");
        return false;
    }

    const char* theme_name = root->Attribute("name");
    printf("[Theme] 加载主题: %s\n", theme_name ? theme_name : "(unnamed)");

    ImGuiStyle& style = ImGui::GetStyle();
    int color_count = 0, style_count = 0;

    /* ---- 解析 <style> 浮点参数和 Vec2 参数 ---- */
    XMLElement* styleElem = root->FirstChildElement("style");
    if (styleElem) {
        for (XMLElement* f = styleElem->FirstChildElement("float");
             f; f = f->NextSiblingElement("float"))
        {
            const char* name = f->Attribute("name");
            float val = f->FloatAttribute("value");
            if (name) { ApplyStyleFloat(style, name, val); style_count++; }
        }
        for (XMLElement* v = styleElem->FirstChildElement("vec2");
             v; v = v->NextSiblingElement("vec2"))
        {
            const char* name = v->Attribute("name");
            float x = v->FloatAttribute("x");
            float y = v->FloatAttribute("y");
            if (name) { ApplyStyleVec2(style, name, x, y); style_count++; }
        }
    }

    /* ---- 解析 <colors> ---- */
    XMLElement* colorsElem = root->FirstChildElement("colors");
    if (colorsElem) {
        for (XMLElement* c = colorsElem->FirstChildElement("color");
             c; c = c->NextSiblingElement("color"))
        {
            const char* name = c->Attribute("name");
            if (!name) continue;

            auto it = kColorMap.find(name);
            if (it == kColorMap.end()) {
                fprintf(stderr, "[Theme] 未知颜色 ID: %s\n", name);
                continue;
            }

            float r = c->FloatAttribute("r");
            float g = c->FloatAttribute("g");
            float b = c->FloatAttribute("b");
            float a = c->FloatAttribute("a", 1.0f);   /* a 默认 1.0 */

            style.Colors[it->second] = ImVec4(r, g, b, a);
            color_count++;
        }
    }

    printf("[Theme] 已应用 %d 项颜色，%d 项风格参数\n", color_count, style_count);
    return true;
}
