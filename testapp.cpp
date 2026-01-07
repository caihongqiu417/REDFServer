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
                                  uint16_t msg_size)
{
    printf("\n===== 收到消息 =====\n");
    printf("来自客户端: ");
    print_client(tcp_client);
    printf("\n");
    
    // 打印消息内容
    printf("消息长度: %d bytes\n", msg_size);
    printf("消息内容: ");
    
    // 以字符串形式显示
    if (msg_size > 0) {
        // 确保以null结尾
        char* temp_buf = new char[msg_size + 1];
        memcpy(temp_buf, msg, msg_size);
        temp_buf[msg_size] = '\0';
        printf("'%s'\n", temp_buf);
        delete[] temp_buf;
        
        // 以十六进制显示
        printf("十六进制: ");
        for (int i = 0; i < msg_size; i++) {
            printf("%02X ", msg[i]);
        }
        printf("\n");
    }
    printf("====================\n");
    
    // 示例：回显消息给客户端
    // 注意：这里只是打印，实际发送需要实现发送功能
    printf("准备回显 %d 字节给客户端\n", msg_size);
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