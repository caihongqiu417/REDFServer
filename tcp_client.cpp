#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


//client exe
int main(int argc, char* argv[]) {
    // 1. 检查参数
    if (argc != 3) {
        printf("Usage: %s <server_ip> <server_port>\n", argv[0]);
        printf("Example: %s 127.0.0.1 4000\n", argv[0]);
        return 1;
    }
    
    const char* server_ip = argv[1];
    int server_port = atoi(argv[2]);
    
    printf("Client: Connecting to %s:%d\n", server_ip, server_port);
    
    // 2. 创建socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket() failed");
        return 1;
    }
    
    // 3. 设置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;                     // IPv4
    server_addr.sin_port = htons(server_port);           // 端口转网络字节序
    
    // IP地址转换（转机器码 + 按序排列)
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
    
    // 让客户端一直运行，不要退出
    while(1) {
        sleep(1);  // 永久保持连接
    }
    
    return 0;
}