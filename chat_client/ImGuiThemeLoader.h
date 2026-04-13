#pragma once

/*
 * LoadImGuiTheme(path)
 *
 * 从 XML 文件读取颜色和风格参数，直接写入 ImGui::GetStyle()。
 * 需在 ImGui::CreateContext() 之后、第一帧渲染之前调用。
 *
 * 返回 true 表示加载成功；false 表示文件不存在或格式错误（使用默认样式）。
 *
 * 依赖：libtinyxml2-dev
 *   sudo apt install libtinyxml2-dev
 */
bool LoadImGuiTheme(const char* xml_path);
