/*
 * Network Utils 工具函数库
 * 
 * 函数命名规范：
 *   n = network (网络字节序/二进制格式)
 *   p = presentation (表示格式/字符串格式)
 *   n_to_p = network to presentation = 网络字节序转字符串
 *   p_to_n = presentation to network = 字符串转网络字节序
 */

#include "network_utils.h"
#include <cstdio>
#include <arpa/inet.h>  

/**
 * @brief 将网络字节序IP转换为点分十进制字符串
 * 
 * @param ip_addr 网络字节序的32位IP地址
 * @param output_buffer 输出缓冲区，可为NULL（使用内部静态缓冲区）
 * @return char* 转换后的字符串指针
 * 
 * @note 函数名n_to_p表示network to presentation
 *       内部使用ntohl将网络字节序转换为主机字节序
 *       使用静态缓冲区时注意线程安全和数据覆盖问题
 */
char* network_convert_ip_n_to_p(uint32_t ip_addr, char* output_buffer) {
    // 内部静态缓冲区，用于output_buffer为NULL的情况
    static char static_buffer[16];
    char* buffer = output_buffer ? output_buffer : static_buffer;
    
    // 网络字节序 → 主机字节序
    uint32_t host_order_ip = ntohl(ip_addr);
    
    // 从32位整数中提取4个字节
    unsigned char bytes[4];
    bytes[0] = (host_order_ip >> 24) & 0xFF;  // A部分（最高字节）
    bytes[1] = (host_order_ip >> 16) & 0xFF;  // B部分
    bytes[2] = (host_order_ip >> 8)  & 0xFF;  // C部分
    bytes[3] = host_order_ip & 0xFF;          // D部分（最低字节）
    
    // 格式化为"A.B.C.D"字符串
    snprintf(buffer, 16, "%d.%d.%d.%d", 
             bytes[0], bytes[1], bytes[2], bytes[3]);
    
    return buffer;
}

/**
 * @brief 将点分十进制IP字符串转换为网络字节序
 * 
 * @param ip_addr 点分十进制IP字符串，如"192.168.1.1"
 * @return uint32_t 网络字节序的32位IP地址，失败返回0
 * 
 * @note 函数名p_to_n表示presentation to network
 *       返回的是网络字节序（大端序），可直接用于socket
 */
uint32_t network_convert_ip_p_to_n(const char* ip_addr) {
    if (!ip_addr) return 0;  // 空指针检查
    
    unsigned int a, b, c, d;
    
    // 解析字符串，提取4个数字
    if (sscanf(ip_addr, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0;  // 格式错误：不是4个数字
    }
    
    // 验证每个数字范围(0-255)
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return 0;  // 超出IP地址范围
    }
    
    // 组合成32位整数(网络字节序：大端)
    return (a << 24) | (b << 16) | (c << 8) | d;
}

/*
 * 使用示例：
 * 
 * 1. IP字符串 → 网络字节序（用于存储/发送）
 *    uint32_t net_ip = network_convert_ip_p_to_n("192.168.1.1");
 *    // 或使用标准函数：inet_addr("192.168.1.1")
 * 
 * 2. 网络字节序 → 字符串（用于显示）
 *    char ip_str[16];
 *    network_convert_ip_n_to_p(net_ip, ip_str);
 *    printf("IP: %s\n", ip_str);
 *    // 或使用标准函数：inet_ntoa()
 * 
 * 3. 字节序转换流程：
 *    字符串      → 网络字节序 → 主机字节序 → 显示字符串
 *    "127.0.0.1" → 0x7F000001 → 0x0100007F → "127.0.0.1"
 *        p_to_n()        存储         ntohl()       n_to_p()
 * 
 * 注意事项：
 * 1. network_convert_ip_n_to_p()内部已包含ntohl转换
 *    实际执行：网络字节序 → 主机字节序 → 字符串
 *    这与函数名"n_to_p"略有不同，实际是"n→h→p"
 * 
 * 2. 使用NULL参数时，函数返回静态缓冲区地址
 *    连续调用会覆盖之前的数据：
 *    char* s1 = network_convert_ip_n_to_p(ip1, NULL);
 *    char* s2 = network_convert_ip_n_to_p(ip2, NULL); // s1被覆盖!
 * 
 * 3. network_convert_ip_p_to_n()返回0可能是错误，也可能是"0.0.0.0"
 *    需要额外检查输入是否为合法的"0.0.0.0"
 */