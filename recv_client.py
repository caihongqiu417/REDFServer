import socket, struct

HOST, PORT = '127.0.0.1', 40000
HDR_SIZE = 4

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect((HOST, PORT))
print("已连接服务器，等待广播消息...")

while True:
    # 先读 4 字节包头
    hdr = b''
    while len(hdr) < HDR_SIZE:
        chunk = sock.recv(HDR_SIZE - len(hdr))
        if not chunk:
            print("连接断开"); break
        hdr += chunk

    body_len = struct.unpack('!I', hdr)[0]   # 网络字节序 uint32

    # 再读包体
    body = b''
    while len(body) < body_len:
        chunk = sock.recv(body_len - len(body))
        if not chunk:
            print("连接断开"); break
        body += chunk

    print(f"[收到广播] {body_len} 字节: {body.decode('utf-8', errors='replace')}")
