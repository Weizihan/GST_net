# GST_net

基于 epoll 的 C++17 高性能 TCP 网络库，采用 Reactor 模式，多线程事件驱动 I/O，自带消息分帧。

---

## 特性

- **Reactor 多线程模型**：accept 独占一个线程，N 个 worker engine（每个一个 epoll 循环）各跑一条线程，accept 与 I/O 完全隔离。
- **消息流而非裸字节流**：`send(message)` 即可，库自动加 4 字节大端长度头、自动拆包，用户不碰粘包/半包。
- **跨线程 send 安全**：业务回调在独立线程池跑，`send` 经 `run_in_loop` 抛回连接的归属 IO 线程串行执行，one-loop-per-thread，天生无锁。
- **业务线程池（per-key 串行）**：同一条连接的消息按 fd 串行处理（保序 + 免锁），不同连接并行，单一共享队列负载均衡。
- **非阻塞 I/O 全链路**：所有 fd `O_NONBLOCK`，写不完进发送缓冲 + 按需注册/注销 `EPOLLOUT`。
- **健壮性**：半关闭（对端 FIN 后发完积压再关）、防伪造超大长度头 OOM、`MSG_NOSIGNAL` 防 SIGPIPE 杀进程、连接关闭时 epoll/回调账本全清理。

## 消息协议

本库是**消息流**库，不是裸字节流。每条消息在线路上的格式：

```
+------------------+--------------------------+
| 4 字节大端长度头  |   body（length 字节）     |
|   (htonl)        |                          |
+------------------+--------------------------+
```

任何客户端接入都必须讲这个协议（裸 socket 自己 `htonl` 加头、按头拆包，参见 `test_server.py`）；将来 `TcpClient` 会替 C++ 调用方把这层包掉。单条消息上限 16MB（防 OOM）。

## 目录结构

```
GST_rpc/
├── CMakeLists.txt              # FetchContent 拉取日志库 GST_log
├── main.cpp                    # 示例：echo 服务器
├── test_server.py             # 9 项压测/边界测试（裸 socket 自己讲协议）
└── src/
    ├── Server.h/.cpp           # 顶层入口，连接管理 + 用户回调
    ├── Connection.h/.cpp       # 单连接：分帧收发、send、关闭状态机
    ├── Socket.h/.cpp           # socket 系统调用封装（FdBase 子类）
    ├── Buffer.h/.cpp           # 收发缓冲（vector + 双指针 + compaction）
    ├── ServerEventLoop.h/.cpp  # accept engine + worker engine 池的组合
    ├── EventLoop/
    │   ├── FdBase.h            # fd 抽象基类
    │   ├── Poller.h/.cpp        # epoll 封装（关注集掩码所有权在此）
    │   ├── EventFd.h          # eventfd 封装（跨线程唤醒）
    │   ├── EventLoopEngine.h/.cpp     # 单个 epoll 事件循环 + run_in_loop
    │   ├── EventLoopThread.h/.cpp     # 把 engine 跑进独立线程
    │   └── EventLoopThreadPool.h/.cpp # N 个 worker engine，round-robin 分配
    └── Concurrency/
        ├── Thread.h/.cpp        # pthread RAII 封装
        └── ThreadPool.h/.cpp    # 业务线程池（per-key 串行调度 idea5）
```

## 架构

两个线程池，职责不同：

```
        ┌──────────────┐  accept 线程（1 条）
        │ accept engine│──── epoll 只盯 listen fd，新连接 round-robin 派给某 worker
        └──────────────┘
        ┌──────────────────────────────────────────┐
        │ EventLoopThreadPool（IO engine 池，N 条）   │  ← 网络/事件 IO
        │  每条线程 = 1 个 EventLoopEngine = 1 个 epoll│
        │  一条连接钉死在一个 engine 上，收发只在它上面发生
        └──────────────────────────────────────────┘
        ┌──────────────────────────────────────────┐
        │ ThreadPool（业务线程池）                    │  ← 用户回调/阻塞重活
        │  同一 fd 的消息串行、不同 fd 并行            │
        │  send 经 run_in_loop 抛回该连接的 IO engine │
        └──────────────────────────────────────────┘
```

## 构建

依赖：Linux（epoll/eventfd）、C++17、CMake ≥ 3.10、pthread。日志库 `GST_log` 由 CMake 的 FetchContent 自动从 GitHub 拉取，无需手动安装。

```bash
mkdir build && cd build
cmake ..
make
# 可执行文件输出到 output/main
```

## 快速开始

```cpp
#include "src/Server.h"

int main() {
    GST::NET::Server server;
    if (!server.init()) {
        return -1;
    }

    // 回调统一注册在 Server 上，作用于所有连接
    server.set_connect_callback([](GST::NET::ConnectionPtr) {
        // 新连接建立
    });
    server.set_message_callback([](GST::NET::ConnectionPtr conn, std::string data) {
        conn->send(data);   // echo：库自动加长度头发回
    });
    server.set_close_callback([](GST::NET::ConnectionPtr) {
        // 连接断开
    });

    server.run();  // 阻塞
    return 0;
}
```

## 测试

```bash
# 先起服务器
./output/main &
# 再跑测试（裸 socket，自己加 4 字节长度头）
python3 test_server.py
```

覆盖：正确性、并发（200×10000）、吞吐、延迟（p99）、1000 短连接、半包/分块、超大长度头拒绝、半关闭零丢失、RST 异常断开存活。
