# REDFServer + GameChat 完整流程文档

---

## 一、整体架构概览

```
┌─────────────────────────────────────────────────────────┐
│                     服务端进程 (testapp.exe)             │
│                                                         │
│  TcpNewConnectionAcceptor     TcpClientServiceManager   │
│  ┌────────────────────┐       ┌──────────────────────┐  │
│  │  accept_thread     │       │  svc_mgr_thread      │  │
│  │  accept() 阻塞等待  │       │  select() 多路复用    │  │
│  │  ↓ 新连接到来       │       │  ↓ 收到数据           │  │
│  │  client_connected  │       │  process_recv()      │  │
│  │  ProcessNewClient  │       │  client_msg_recvd()  │  │
│  └────────────────────┘       └──────────────────────┘  │
│                                                         │
│  TcpClientDBManager           TcpServerController       │
│  ┌─────────────────┐          ┌───────────────────────┐ │
│  │  list<TcpClient>│          │  Broadcast()          │ │
│  │  (全局客户端表)  │          │  SendPlayerListTo()   │ │
│  └─────────────────┘          └───────────────────────┘ │
└─────────────────────────────────────────────────────────┘
               │ TCP (127.0.0.1:40000)
┌──────────────▼──────────────────────────────────────────┐
│                   客户端进程 (chat_client)                │
│                                                         │
│  main thread (ImGui 渲染)    recv_thread (后台接收)      │
│  ┌────────────────────┐      ┌──────────────────────┐   │
│  │  glfwPollEvents()  │      │  recv() 阻塞读取      │   │
│  │  ChatUI::Render()  │      │  TCP 帧解析           │   │
│  │  HandleIncoming()  │      │  → incoming_ 队列    │   │
│  │  Poll() 取消息     │◄─────│  (线程安全 mutex)     │   │
│  └────────────────────┘      └──────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

---

## 二、消息协议

所有 TCP 消息统一格式，定义于 `msg_protocol.h`：

```
+──────────────────+──────────────────+──────────────────────+
│  body_len 4字节  │  msg_type 2字节  │  body  N字节         │
│  uint32_t 大端   │  uint16_t 大端   │  消息内容             │
+──────────────────+──────────────────+──────────────────────+
```

| 类型值 | 枚举名           | 方向          | body 内容                          |
|-------|-----------------|--------------|-------------------------------------|
| 1     | MSG_CHAT        | 客户端 → 服务端 → 广播 | 聊天文本（UTF-8 字符串）      |
| 2     | MSG_JOIN        | 客户端 → 服务端 → 广播 | 昵称字符串                    |
| 3     | MSG_LEAVE       | 客户端 → 服务端 → 广播 | 昵称字符串                    |
| 4     | MSG_NICK_SET    | 客户端 → 服务端        | 新昵称字符串                  |
| 5     | MSG_PLAYER_LIST | 服务端 → 新客户端      | 见下方格式说明                |

**MSG_PLAYER_LIST body 格式：**
```
[num_players: uint16_t 大端]
重复 num_players 次：
  [str_len: uint16_t 大端][str: str_len字节，无\0终止符]
str 内容：有昵称时为昵称，否则为 "ip:port"
```

---

## 三、服务端启动流程

```
main() in testapp.cpp
  │
  ├─ new TcpServerController("127.0.0.1", 40000, ...)
  │     │
  │     ├─ new TcpNewConnectionAcceptor(this)   // 创建 accept_fd (socket)
  │     ├─ new TcpClientDBManager(this)          // 初始化全局客户端表
  │     └─ new TcpClientServiceManager(this)     // 初始化 select 管理器
  │
  ├─ SetServerNotiCallbacks(
  │       appln_client_connected,       // 注册连接回调
  │       appln_client_disconnected,    // 注册断开回调
  │       appln_client_msg_recvd        // 注册消息回调
  │   )
  │
  └─ server->Start()
        │
        ├─ TcpNewConnectionAcceptor::StartTcpNewConnectionAcceptorThread()
        │     → 创建 accept_thread，bind + listen，进入 accept() 循环
        │
        ├─ TcpClientServiceManager::StartTcpClientServiceManagerThread()
        │     → 创建 svc_mgr_thread，进入 select() 循环（初始无客户端）
        │
        └─ TcpClientDBManager::StartTcpClientDbMgrInit()
              → 初始化全局客户端数据库
```

此后服务端有 **两个长期运行的线程**：
- `accept_thread`：阻塞在 `accept()`，等待新 TCP 连接
- `svc_mgr_thread`：阻塞在 `select()`，监听所有已连接客户端的数据

---

## 四、客户端连接流程（新客户端接入服务端侧）

以 Player2 连入（此时 Player1 已在线）为例：

```
accept_thread (TcpNewConnectionAcceptor)
  │
  ├─ accept() 返回新 comm_fd
  │
  ├─ new TcpClient(ip, port)           // 创建客户端对象，分配独立接收缓冲区
  │     TcpClient.recv_buf[65542]       // 用于粘包/半包处理
  │     TcpClient.recv_buf_used = 0
  │     TcpClient.nickname = ""        // 尚未收到 MSG_JOIN
  │
  ├─ 回调 appln_client_connected(server, player2)     ← testapp.cpp
  │     │
  │     └─ server->SendPlayerListTo(player2)
  │           │
  │           └─ TcpClientServiceManager::SendPlayerListTo()
  │                 遍历 tcp_client_db（此时含 Player1）：
  │                   Player1.nickname = "Player1" → 写入 "Player1"
  │                 组装 MSG_PLAYER_LIST body：
  │                   [0x00 0x01]              // num=1
  │                   [0x00 0x08] "Player1"   // len=8, str
  │                 msg_send(player2.comm_fd, body, MSG_PLAYER_LIST)
  │                   → 写 6字节包头 + body → TCP 发出
  │
  └─ ProcessNewClient(player2)
        │
        ├─ TcpClientDBManager::AddClienttoDB(player2)    // 加入全局表
        │
        └─ TcpClientServiceManager::ClientFDStartListen(player2)
              │
              ├─ StopTcpClientServiceManagerThread()      // 停止 select 线程
              ├─ AddClientToDB(player2)                   // 加入 svc_mgr 内部表
              └─ StartTcpClientServiceManagerThread()     // 重启 select 线程
                    → select() 现在监听 Player1 + Player2 两个 fd
```

> **关键时序**：`client_connected` 在 `ProcessNewClient` **之前**调用，
> 因此 `SendPlayerListTo` 遍历的列表里有 Player1 但不含 Player2 自己——语义正确。

---

## 五、客户端启动与连接流程（客户端侧）

```
main() in chat_client/main.cpp
  │
  ├─ glfwInit() + glfwCreateWindow(900, 600)
  ├─ ImGui::CreateContext()
  ├─ 加载 NotoSansCJK-Regular.ttc（CJK 字体，支持中文显示）
  ├─ ImGui_ImplGlfw_InitForOpenGL() + ImGui_ImplOpenGL3_Init()
  │
  ├─ new ChatNetworkClient
  ├─ new ChatUI
  │
  └─ net.Connect("127.0.0.1", 40000)
        │
        ├─ socket() + connect()                // TCP 三次握手
        │
        └─ pthread_create(&recv_thread_, RecvThreadFn, this)
              → 启动后台接收线程，立即开始 recv() 阻塞等待
              → 此时服务端已发来 MSG_PLAYER_LIST，会被接收线程读取
```

**接收线程 RecvLoop 工作原理（处理粘包/半包）：**

```
RecvLoop()
  循环:
    n = recv(sock_fd_, recv_buf_ + used, cap - used, 0)
    used += n

    while (used >= 6):                         // 至少有完整包头
      body_len = ntohl(recv_buf_[0..3])
      msg_type = ntohs(recv_buf_[4..5])

      if used < 6 + body_len: break            // 半包：等待更多数据

      // 完整消息就绪
      msg = {type: msg_type, body: recv_buf_[6..6+body_len]}
      lock(queue_mutex_) → incoming_.push_back(msg)

      // 移除已处理数据（处理粘包）
      memmove(recv_buf_, recv_buf_ + 6 + body_len, ...)
      used -= (6 + body_len)
```

---

## 六、ImGui 主循环与 UI 渲染

```
while (!glfwWindowShouldClose(window)):
  │
  ├─ glfwPollEvents()                          // 处理键盘/鼠标事件
  │
  ├─ ImGui::NewFrame()
  │
  ├─ ChatUI::Render(net)
  │     │
  │     ├─ HandleIncoming(net)                 // ① 处理网络消息
  │     │     msgs = net.Poll()               //   从队列取出所有新消息
  │     │     for each msg:
  │     │       MSG_PLAYER_LIST → 解析 body → 重置 players_ 列表
  │     │       MSG_JOIN        → 追加到 players_，写系统提示
  │     │       MSG_LEAVE       → 从 players_ 删除，写系统提示
  │     │       MSG_CHAT        → 追加到 messages_
  │     │
  │     ├─ RenderLoginBar(net)                 // ② 顶部昵称栏
  │     │     未加入: [昵称输入框] [Join按钮]
  │     │     已加入: "在线聊天中 昵称:xxx"  [Leave按钮]
  │     │
  │     ├─ RenderPlayerList()                  // ③ 左栏：在线玩家
  │     │     BeginChild("##playerlist", 155px宽)
  │     │     TextDisabled("在线 (N)")
  │     │     for p in players_: Bullet + Text(p)
  │     │     EndChild()
  │     │
  │     └─ RenderChatArea(net)                 // ④ 右栏：聊天区
  │           BeginChild("##chat_scroll")
  │             for line in messages_: TextUnformatted(line)
  │             if scroll_bottom_: SetScrollHereY(1.0)
  │           EndChild()
  │           InputText(##chatinput, EnterReturnsTrue)
  │           Button("Send") → TrySendChat()
  │                             net.SendChat(text)
  │                             AppendMsg("[你] " + text)
  │
  ├─ ImGui::Render()
  ├─ glClear() + ImGui_ImplOpenGL3_RenderDrawData()
  └─ glfwSwapBuffers()                         // 显示到屏幕
```

---

## 七、加入聊天室（点击 Join）

```
用户在昵称框输入 "Player2"，点击 Join 按钮：

ChatUI::RenderLoginBar()
  │
  ├─ net.SendJoin("Player2")
  │     → msg_send(sock_fd_, "Player2", 7, MSG_JOIN)
  │     → 服务端 svc_mgr_thread 的 select() 检测到数据就绪
  │     → process_recv() 解包 → 回调 appln_client_msg_recvd()
  │          │
  │          ├─ tcp_client->nickname = "Player2"   // 保存昵称
  │          └─ Broadcast("Player2", MSG_JOIN, exclude=Player2)
  │                → Player1 收到 MSG_JOIN("Player2")
  │                → Player1 的 HandleIncoming: players_.push_back("Player2")
  │
  ├─ players_.push_back("Player2 (你)")           // 本地立即显示自己
  ├─ AppendMsg("[系统] 你以昵称 \"Player2\" 加入聊天室")
  └─ joined_ = true                               // 解锁聊天输入框
```

---

## 八、聊天消息流转

```
Player2 输入 "hello"，按 Enter 或点 Send：

ChatUI::TrySendChat()
  ├─ net.SendChat("hello")
  │     → msg_send(sock_fd_, "hello", 5, MSG_CHAT)
  │
  └─ AppendMsg("[你] hello")                      // 本地立即显示

服务端 process_recv() → appln_client_msg_recvd(MSG_CHAT)
  └─ Broadcast("hello", MSG_CHAT, exclude=Player2)
        → Player1 收到 MSG_CHAT("hello")
        → Player1 HandleIncoming: AppendMsg("[他人] hello")
```

---

## 九、线程分工总结

| 线程 | 位置 | 职责 |
|------|------|------|
| `accept_thread` | 服务端 `TcpNewConnectionAcceptor` | `accept()` 等待新连接，调用 `client_connected` + `ProcessNewClient` |
| `svc_mgr_thread` | 服务端 `TcpClientServiceManager` | `select()` 监听所有客户端，解包，回调 `client_msg_recvd`，执行广播 |
| main thread | 客户端 | ImGui 渲染 + 事件处理 + 调用 `Poll()` 消费消息 |
| `recv_thread` | 客户端 `ChatNetworkClient` | `recv()` 阻塞读取 + TCP 帧解析 + 写入线程安全队列 |

---

## 十、关键文件索引

| 文件 | 作用 |
|------|------|
| `msg_protocol.h` | 协议包头格式、MsgType 枚举、`msg_send` 工具函数 |
| `TcpNewConnectionAcceptor.cpp` | accept 线程，触发 `client_connected` 回调 |
| `TcpClientServiceManager.cpp` | select 线程，`process_recv` 解包，`Broadcast`，`SendPlayerListTo` |
| `TcpServerController.cpp` | 服务器门面：`Start`、`Broadcast`、`SendPlayerListTo` |
| `TcpClient.h` | 单个客户端数据：fd、ip/port、接收缓冲区、nickname |
| `testapp.cpp` | 应用层回调：`appln_client_connected`，`appln_client_msg_recvd`（存昵称、广播） |
| `chat_client/ChatNetworkClient.cpp` | 客户端 TCP 连接 + 后台接收线程 + 消息队列 |
| `chat_client/ChatUI.cpp` | ImGui 渲染：玩家列表、聊天历史、输入框、消息分发 |
| `chat_client/main.cpp` | GLFW 窗口、ImGui 初始化、CJK 字体加载、主循环 |
