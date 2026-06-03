#pragma once
#include "Poller.h"
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <iostream>

namespace GST {
namespace BASE {

using Task = std::function<void(int)>;

class EventLoopEngine {
friend class EventLoopThreadPool;
public:
    EventLoopEngine();
    ~EventLoopEngine();

    // server_fd=-1 时为 worker engine
    bool init(int server_fd = -1);
    void start();
    void stop();

    // accept engine 专用：注册新连接回调（全局唯一）
    void register_accept_callback(Task cb);

    // worker engine 专用：每个 fd 注册各自的读回调
    bool add_fd_callback(FdPtr fd, Task cb);

    bool add_watch(FdPtr fd, EVENT_TYPE type);

    // 按需启用/禁用 EPOLLOUT
    bool enable_write(FdPtr fd, Task cb);
    bool disable_write(int fd);

    // 半关闭:对端 EOF 后摘掉读关注,防 LT 下 recv 持续返回 0 空转
    bool disable_read(int fd);

    // 连接关闭时调用:从 epoll 摘掉 + 清掉本 engine 与 poller 里所有按 fd 存的账本
    bool remove_fd(int fd);

    void run_in_loop(std::function<void()> task);    

private:
    int  _server_fd;
    FdPtr _wakeup_fd;
    bool _is_running;
    Poller _poller;
    std::thread::id _thread_id;

    Task _accept_cb;
    std::vector<std::function<void()>> _pending;
    std::unordered_map<int, Task> _read_cbs;
    std::unordered_map<int, Task> _write_cbs;

    std::mutex _cb_mutex;
    std::mutex _pending_mutex;
};
} // namespace NET
} // namespace GST
