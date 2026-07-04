#pragma once

#include <functional>
#include <memory>

#include "EventLoop/EventLoopThreadPool.h"
#include "EventLoop/EventLoopEngine.h"
#include "Concurrency/Thread.h"
#include "Socket.h"

namespace GST {
namespace NET {

class ServerEventLoop {
public:
    ServerEventLoop() = default;
    ~ServerEventLoop();

    bool init(BASE::FdPtr server_fd, int worker_num);
    bool start();
    void stop();
    void wait();  // 阻塞直到 accept engine 退出

    // 设置新连接到来时的回调（在回调里执行 accept）
    void set_accept_callback(std::function<void(int)> cb);

    // 线程池按 round-robin 为新连接选一个 worker engine 作为它的家
    BASE::EnginePtr get_engine() {
        return _eventloop_pool.get_next_loop();
    }

private:
    bool _running = false;
    BASE::FdPtr _server_fd;
    BASE::EventLoopThreadPool _eventloop_pool;
    BASE::EventLoopEngine _accept_engine;
    BASE::Thread _accept_thread;
};

}  // namespace NET
}  // namespace GST
