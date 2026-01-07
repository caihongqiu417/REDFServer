// tcp_client.cpp (修改版)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <termios.h>
#include <signal.h>

// 全局变量用于信号处理
static volatile int keep_running = 1;

// 信号处理函数
void int_handler(int dummy) {
    keep_running = 0;
    printf("\n正在关闭客户端...\n");
}

// 设置非阻塞输入
void set_nonblocking(int enable) {
    static struct termios oldt, newt;
    
    if (enable) {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, int_handler);
    
    // 1. 检查参数
    if (argc != 3) {
        printf("Usage: %s <server_ip> <server_port>\n", argv[0]);
        printf("Example: %s 127.0.0.1 4000\n", argv[0]);
        return 1;
    }
    
    const char* server_ip = argv[1];
    int server_port = atoi(argv[2]);
    
    printf("Client: Connecting to %s:%d\n", server_ip, server_port);
    printf("Press Ctrl+C to exit\n");
    
    // 2. 创建socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket() failed");
        return 1;
    }
    
    // 3. 设置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        printf("Invalid IP address: %s\n", server_ip);
        close(sockfd);
        return 1;
    }
    
    // 4. 连接服务器
    printf("Client: Attempting connect()...\n");
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect() failed");
        close(sockfd);
        return 1;
    }
    
    // 5. 连接成功
    printf("Client: connect() successful!\n");
    printf("Client: Connected to server via socket %d\n", sockfd);
    printf("Enter messages to send (type 'quit' to exit):\n");
    
    // 设置非阻塞输入
    set_nonblocking(1);
    
    char input_buffer[1024];
    int input_index = 0;
    
    while (keep_running) {
        // 检查用户输入
        int ch = getchar();
        if (ch != EOF) {
            if (ch == '\n') {
                // 发送消息
                if (input_index > 0) {
                    input_buffer[input_index] = '\0';
                    
                    // 检查是否退出
                    if (strcmp(input_buffer, "quit") == 0) {
                        printf("Disconnecting...\n");
                        break;
                    }
                    
                    // 发送消息到服务器
                    int bytes_sent = send(sockfd, input_buffer, input_index, 0);
                    if (bytes_sent > 0) {
                        printf("Sent %d bytes: %s\n", bytes_sent, input_buffer);
                    } else {
                        perror("send() failed");
                        break;
                    }
                    
                    // 接收服务器的响应（如果有）
                    char recv_buffer[1024];
                    int bytes_received = recv(sockfd, recv_buffer, sizeof(recv_buffer) - 1, 0);
                    if (bytes_received > 0) {
                        recv_buffer[bytes_received] = '\0';
                        printf("Server response: %s\n", recv_buffer);
                    } else if (bytes_received == 0) {
                        printf("Server disconnected\n");
                        break;
                    }
                    
                    input_index = 0;
                }
            } else {
                // 添加到缓冲区
                if (input_index < sizeof(input_buffer) - 1) {
                    input_buffer[input_index++] = ch;
                    putchar(ch);
                    fflush(stdout);
                }
            }
        }
        
        // 短暂休眠以避免CPU占用过高
        usleep(10000); // 10ms
    }
    
    // 恢复终端设置
    set_nonblocking(0);
    
    // 关闭socket
    close(sockfd);
    printf("Client disconnected\n");
    
    return 0;
}