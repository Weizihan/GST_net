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

    // 把一条连接的 fd + 读回调注册到某个 worker engine，返回被选中的家 engine
    BASE::EnginePtr add_fd_callback(BASE::FdPtr fd, const std::function<void(int)>& callback) {
        return _eventloop_pool.add_fd_callback(fd, callback);
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
