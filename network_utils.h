#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <cstdint>

/**
 * IP地址转换工具函数
 * n: network format - 32位整数(网络字节序)
 * p: presentation format - "A.B.C.D"字符串
 */

// 整数IP -> 字符串IP
// 如果output_buffer为NULL，使用内部静态缓冲区(16字节)
// 注意：静态缓冲区非线程安全，连续调用会覆盖之前的结果
char* network_convert_ip_n_to_p(uint32_t ip_addr, char* output_buffer);

// 字符串IP -> 整数IP(网络字节序)
// 输入格式应为"A.B.C.D"，每个数字0-255
// 格式错误返回0
uint32_t network_convert_ip_p_to_n(const char* ip_addr);

#endif