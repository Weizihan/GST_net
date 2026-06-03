#include "ServerEventLoop.h"

namespace GST {
namespace NET {

ServerEventLoop::~ServerEventLoop() {
    stop();
    _accept_thread.join();
}

void ServerEventLoop::stop() {
    if (!_running) {
        return;
    }
    _running = false;
    _accept_engine.stop();
}

void ServerEventLoop::wait() {
    _accept_thread.join();
}

bool ServerEventLoop::init(BASE::FdPtr server_fd, int worker_num) {
    _server_fd = server_fd;

    // accept engine：专门监听 server socket 的 EPOLLIN（新连接）
    if (!_accept_engine.init(server_fd->fd())) {
        return false;
    }
    if (!_accept_engine.add_watch(server_fd, NEW_CONNECTION)) {
        return false;
    }

    return _eventloop_pool.init(worker_num);
}

void ServerEventLoop::set_accept_callback(std::function<void(int)> cb) {
    _accept_engine.register_accept_callback(std::move(cb));
}

bool ServerEventLoop::start() {
    _running = true;

    if (!_eventloop_pool.start()) {
        return false;
    }

    // accept engine 在独立线程运行
    _accept_thread = BASE::Thread([this]() {
        _accept_engine.start();
    });

    return true;
}

}  // namespace NET
}  // namespace GST
