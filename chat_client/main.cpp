#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ChatNetworkClient.h"
#include "ChatUI.h"
#include "ImGuiThemeLoader.h"
#include "Icon/World_data.h"
#include "Icon/Friend_data.h"
#include "Icon/PrivateChat_data.h"

/* 头像数据头文件（由 Python 从 Avatar 目录 PNG 缩放至 64x64 生成） */
#include "Avatar/avatar_0_data.h"
#include "Avatar/avatar_1_data.h"
#include "Avatar/avatar_2_data.h"
#include "Avatar/avatar_3_data.h"
#include "Avatar/avatar_4_data.h"
#include "Avatar/avatar_5_data.h"
#include "Avatar/avatar_6_data.h"
#include "Avatar/avatar_7_data.h"
#include "Avatar/avatar_8_data.h"
#include "Avatar/avatar_9_data.h"
#include "Avatar/avatar_10_data.h"

static const int AVATAR_COUNT = 11;

static GLuint CreateTexture(const unsigned char* data, int w, int h)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    return tex;
}

/* 从文件加载图片（支持 JPEG/PNG 等），返回 OpenGL 纹理 ID，失败返回 0 */
static GLuint LoadTextureFromFile(const char* path, int* out_w = nullptr, int* out_h = nullptr)
{
    int w, h, ch;
    unsigned char* pixels = stbi_load(path, &w, &h, &ch, 4);   /* 强制转 RGBA */
    if (!pixels) {
        fprintf(stderr, "[main] 图片加载失败: %s\n", path);
        return 0;
    }
    GLuint tex = CreateTexture(pixels, w, h);
    stbi_image_free(pixels);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return tex;
}

/* 服务器地址（可改为命令行参数） */
static const char* SERVER_HOST = "127.0.0.1";
static uint16_t    SERVER_PORT = 40000;

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int argc, char** argv)
{
    /* 支持命令行指定服务器地址: ./chat_client 192.168.1.1 40000 */
    if (argc >= 2) SERVER_HOST = argv[1];
    if (argc >= 3) SERVER_PORT = (uint16_t)atoi(argv[2]);

    /* ---- GLFW 初始化 ---- */
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(960, 640, "REDF GameChat", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);    /* vsync */

    /* ---- ImGui 初始化 ---- */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   /* 不保存布局文件 */

    ImGui::StyleColorsDark();

    /* ---- 字体加载：Bitter（英文）+ NotoSansCJK（中文合并） ----
     *
     * 策略：
     *   1. 先用 AddFontFromFileTTF 加载 Bitter-Regular 作为主字体（覆盖 ASCII 范围）
     *   2. 再以 MergeMode=true 合并 NotoSansCJK，补充中文字形
     *
     * MergeMode：把第二次调用的字形追加进上一次创建的字体对象，
     *            而不是新建一个字体。这样中英文共用同一个 ImFont*。
     * ---------------------------------------------------------------- */
    {
        ImGuiIO& io2  = ImGui::GetIO();
        const float   sz = 22.0f;   /* 字号：比原来 19 大一些 */

        /* 步骤 1：加载 Bitter Regular（英文主字体） */
        ImFontConfig cfg_bitter;
        cfg_bitter.OversampleH = 2;
        cfg_bitter.OversampleV = 2;
        ImFont* font = io2.Fonts->AddFontFromFileTTF(
            "Font/Bitter/static/Bitter-Regular.ttf", sz, &cfg_bitter, nullptr);

        /* 步骤 2：合并 CJK 字体，补充中文字形 */
        static const ImWchar cjk_ranges[] = {
            0x2000, 0x206F,   /* 常用标点 */
            0x3000, 0x30FF,   /* CJK 符号、假名 */
            0x4E00, 0x9FAF,   /* CJK 统一汉字 */
            0xFF00, 0xFFEF,   /* 全角字符 */
            0,
        };
        ImFontConfig cfg_cjk;
        cfg_cjk.MergeMode  = true;    /* 合并进上一个字体而不是新建 */
        cfg_cjk.PixelSnapH = true;    /* 像素对齐，减少中文模糊 */
        io2.Fonts->AddFontFromFileTTF(
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            sz, &cfg_cjk, cjk_ranges);

        if (!font) {
            fprintf(stderr, "[main] 警告: Bitter 字体加载失败，使用默认字体\n");
            io2.Fonts->AddFontDefault();
        }
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    /* ---- 从 XML 加载红白主题（在字体构建之后，首帧之前） ---- */
    if (!LoadImGuiTheme("theme_red_white.xml"))
        fprintf(stderr, "[main] 主题加载失败，使用 ImGui 默认样式\n");

    /* ---- 加载图标纹理（OpenGL 上下文已就绪） ---- */
    GLuint icon_world   = CreateTexture(world_icon_rgba,        world_icon_width,        world_icon_height);
    GLuint icon_friend  = CreateTexture(friend_icon_rgba,       friend_icon_width,       friend_icon_height);
    GLuint icon_private = CreateTexture(private_chat_icon_rgba, private_chat_icon_width, private_chat_icon_height);

    /* ---- 加载头像纹理数组 ---- */
    static const unsigned char* avatar_data[AVATAR_COUNT] = {
        avatar_0_rgba, avatar_1_rgba, avatar_2_rgba, avatar_3_rgba,
        avatar_4_rgba, avatar_5_rgba, avatar_6_rgba, avatar_7_rgba,
        avatar_8_rgba, avatar_9_rgba, avatar_10_rgba
    };
    static const int avatar_wh[AVATAR_COUNT][2] = {
        {avatar_0_w,avatar_0_h},{avatar_1_w,avatar_1_h},{avatar_2_w,avatar_2_h},
        {avatar_3_w,avatar_3_h},{avatar_4_w,avatar_4_h},{avatar_5_w,avatar_5_h},
        {avatar_6_w,avatar_6_h},{avatar_7_w,avatar_7_h},{avatar_8_w,avatar_8_h},
        {avatar_9_w,avatar_9_h},{avatar_10_w,avatar_10_h}
    };
    GLuint avatar_textures[AVATAR_COUNT];
    for (int i = 0; i < AVATAR_COUNT; i++)
        avatar_textures[i] = CreateTexture(avatar_data[i], avatar_wh[i][0], avatar_wh[i][1]);

    /* ---- 加载登录页背景图 ---- */
    int bg_w = 0, bg_h = 0;
    GLuint bg_tex = LoadTextureFromFile("Background/FF7.jpeg", &bg_w, &bg_h);

    /* ---- 网络 + UI 对象 ---- */
    ChatNetworkClient net;
    ChatUI            ui;
    ui.SetIcons(icon_world, icon_friend, icon_private);
    ui.SetAvatars(avatar_textures, AVATAR_COUNT);
    ui.SetBackground(bg_tex, bg_w, bg_h);

    printf("[main] 正在连接到 %s:%u ...\n", SERVER_HOST, SERVER_PORT);
    if (!net.Connect(SERVER_HOST, SERVER_PORT))
        printf("[main] 连接失败，程序将以离线模式运行\n");

    /* ---- 主循环 ---- */
    while (!glfwWindowShouldClose(window) && !ui.WantsQuit()) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ui.Render(net);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    /* ---- 清理 ---- */
    net.Disconnect();
    if (bg_tex) glDeleteTextures(1, &bg_tex);
    glDeleteTextures(1, &icon_world);
    glDeleteTextures(1, &icon_friend);
    glDeleteTextures(1, &icon_private);
    glDeleteTextures(AVATAR_COUNT, avatar_textures);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
