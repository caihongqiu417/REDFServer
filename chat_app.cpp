#include <iostream>
#include <unistd.h>
#include <string.h>
#include "TcpServerController.h"
#include "TcpClient.h"
#include"network_utils.h"
#include<arpa/inet.h>

/* 打印TCP客户端信息 */
static void print_client(const TcpClient* tcp_client)
{
    printf("[%s,%d] ", 
           network_convert_ip_n_to_p(tcp_client->ip_addr, 0),
           ntohs(tcp_client->port_no));  // 正确：网络转主机字节序
}

/* 打印TCP服务器信息 */  
static void print_server(const TcpServerController* tcp_server)
{
    printf("[%s,%d] ", 
           network_convert_ip_n_to_p(tcp_server->ip_addr, 0),
           ntohs(tcp_server->port_no));
}

/* 回调函数：客户端连接 */
static void appln_client_connected(const TcpServerController* tcp_server,const TcpClient* tcp_client)
{
    printf("\n===== 客户端连接 =====\n");
    printf("服务器: ");
    print_server(tcp_server);
    printf("\n客户端: ");
    print_client(tcp_client);
    printf("(fd=%d)\n", tcp_client->comm_fd);
    printf("========================\n");

    /* 向新客户端推送当前在线玩家列表
     * 此时 tcp_client 尚未进入 ServiceManager DB，所以列表只含旧玩家 */
    tcp_server->SendPlayerListTo(tcp_client);
}

/* 回调函数：客户端断开连接 */
static void appln_client_disconnected(const TcpServerController* tcp_server,const TcpClient* tcp_client)
{
    printf("\n===== 客户端断开连接 =====\n");
    printf("服务器: ");
    print_server(tcp_server);
    printf("\n客户端: ");
    print_client(tcp_client);
    printf("\n==========================\n");
}

/* 回调函数：接收到客户端消息 */
static void appln_client_msg_recvd(const TcpServerController* tcp_server,
                                  const TcpClient* tcp_client,
                                  unsigned char* msg,
                                  uint16_t msg_size,
                                  uint16_t msg_type)   /* 新增：消息类型，来自包头第5-6字节 */
{
    /* msg_type 取值见 msg_protocol.h 的 MsgType 枚举：
     * 1=MSG_CHAT, 2=MSG_JOIN, 3=MSG_LEAVE, 4=MSG_NICK_SET */
    const char* type_str = (msg_type == 1) ? "CHAT"        :
                           (msg_type == 2) ? "JOIN"        :
                           (msg_type == 3) ? "LEAVE"       :
                           (msg_type == 4) ? "NICK_SET"    :
                           (msg_type == 5) ? "PLAYER_LIST" : "UNKNOWN";

    printf("\n===== 收到消息 =====\n");
    printf("来自客户端: ");
    print_client(tcp_client);
    printf("\n");
    printf("消息类型: %s (%u)\n", type_str, msg_type);
    printf("消息长度: %d bytes\n", msg_size);
    printf("消息内容: ");

    if (msg_size > 0) {
        char* temp_buf = new char[msg_size + 1];
        memcpy(temp_buf, msg, msg_size);
        temp_buf[msg_size] = '\0';
        printf("'%s'\n", temp_buf);
        delete[] temp_buf;

        printf("十六进制: ");
        for (int i = 0; i < msg_size; i++) {
            printf("%02X ", msg[i]);
        }
        printf("\n");
    }
    printf("====================\n");

    /* MSG_JOIN：保存昵称，供后来连入的客户端通过 MSG_PLAYER_LIST 看到 */
    if (msg_type == MSG_JOIN && msg_size > 0) {
        tcp_client->nickname = std::string((char*)msg, msg_size);
        printf("已记录昵称: fd=%d -> \"%s\"\n", tcp_client->comm_fd, tcp_client->nickname.c_str());
    }

    /* MSG_CHAT：body 格式 = [1字节 avatar_idx][昵称: 消息文本] */
    if (msg_type == MSG_CHAT && msg_size > 0 && !tcp_client->nickname.empty()) {
        std::string full_msg = tcp_client->nickname + ": " + std::string((char*)msg, msg_size);
        std::string body;
        body += (char)(uint8_t)tcp_client->avatar_idx;
        body += full_msg;
        tcp_server->Broadcast((const uint8_t*)body.data(), (uint32_t)body.size(), MSG_CHAT, tcp_client);
    } else {
        tcp_server->Broadcast(msg, msg_size, msg_type, tcp_client);
    }
    printf("已广播 %d 字节(type=%s)给其他客户端\n", msg_size, type_str);
}

/* 显示菜单 */
static void show_menu()
{
    printf("\n===== TCP 服务器测试程序 =====\n");
    printf("1. 显示服务器状态\n");
    printf("2. 发送测试消息到所有客户端\n");
    printf("3. 退出程序\n");
    printf("选择: ");
}

/* 应用程序入口 */
int main()
{
    printf("===== TCP 服务器启动 =====\n");
    
    // 创建TCP服务器
    TcpServerController* server1 = new TcpServerController("127.0.0.1", 40000, "Redflag TCP Server");
    
    // 注册回调函数
    server1->SetServerNotiCallbacks(appln_client_connected,
                                   appln_client_disconnected,
                                   appln_client_msg_recvd);

    // 启动服务器
    printf("正在启动服务器...\n");
    server1->Start();  
    
    printf("服务器已启动！\n");
    
    int choice = 0;
    char input_buffer[256];
    
    while (choice != 3) {
        show_menu();
        
        if (fgets(input_buffer, sizeof(input_buffer), stdin)) {
            choice = atoi(input_buffer);
            
            switch (choice) {
                case 1:
                    printf("\n===== 服务器状态 =====\n");
                    server1->Display();
                    break;
                    
                case 2:
                    printf("\n===== 发送测试消息 =====\n");
                    printf("注意：此功能需要实现发送消息功能\n");
                    printf("当前版本仅演示接收功能\n");
                    break;
                    
                case 3:
                    printf("正在关闭服务器...\n");
                    break;
                    
                default:
                    printf("无效选择，请重试\n");
                    break;
            }
        }
    }
    
    // 清理资源
    delete server1;
    printf("Redflag服务器已关闭\n");
    
    return 0;
}