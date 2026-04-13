import socket, struct

HOST, PORT = '127.0.0.1', 40000

# 包头格式与 msg_protocol.h 保持一致：
#   4 字节 body_len（大端 uint32）
#   2 字节 msg_type（大端 uint16）
HDR_FMT  = '!IH'       # I=uint32, H=uint16，! 表示网络字节序（大端）
HDR_SIZE = struct.calcsize(HDR_FMT)   # = 6 字节

MSG_TYPE_NAME = {1: 'CHAT', 2: 'JOIN', 3: 'LEAVE', 4: 'NICK_SET'}

def recv_all(sock, n):
    """保证读满 n 字节，处理 TCP 分片"""
    buf = b''
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("连接断开")
        buf += chunk
    return buf

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect((HOST, PORT))
print(f"已连接服务器 {HOST}:{PORT}，等待广播消息...\n")

while True:
    # 1. 读 6 字节包头
    hdr_bytes = recv_all(sock, HDR_SIZE)
    body_len, msg_type = struct.unpack(HDR_FMT, hdr_bytes)

    type_name = MSG_TYPE_NAME.get(msg_type, f'UNKNOWN({msg_type})')

    # 2. 读 body_len 字节包体
    body = recv_all(sock, body_len)

    print(f"[收到广播] type={type_name} | {body_len} 字节 | '{body.decode('utf-8', errors='replace')}'")