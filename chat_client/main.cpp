#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>

#include "ChatNetworkClient.h"
#include "ChatUI.h"

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

    GLFWwindow* window = glfwCreateWindow(900, 600, "GameChat", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);    /* vsync */

    /* ---- ImGui 初始化 ---- */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   /* 不保存布局文件 */

    ImGui::StyleColorsDark();

    /* 加载系统 CJK 字体，以支持中文显示 */
    {
        ImGuiIO& io2 = ImGui::GetIO();
        static const ImWchar ranges[] = {
            0x0020, 0x00FF,   /* 基本拉丁 + 补充拉丁 */
            0x2000, 0x206F,   /* 常用标点 */
            0x3000, 0x30FF,   /* CJK 符号、假名 */
            0x31F0, 0x31FF,   /* 片假名音标 */
            0xFF00, 0xFFEF,   /* 全角字符 */
            0x4E00, 0x9FAF,   /* CJK 统一汉字 */
            0,
        };
        const char* font_path =
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
        ImFont* font = io2.Fonts->AddFontFromFileTTF(
            font_path, 16.0f, nullptr, ranges);
        if (!font) {
            fprintf(stderr, "[main] 警告: 加载 CJK 字体失败，中文将显示为方块\n");
            io2.Fonts->AddFontDefault();
        }
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    /* ---- 网络 + UI 对象 ---- */
    ChatNetworkClient net;
    ChatUI            ui;

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
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
