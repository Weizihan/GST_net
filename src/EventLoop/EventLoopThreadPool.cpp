#include "EventLoopThreadPool.h"

namespace GST {
namespace BASE {

EventLoopThreadPool::EventLoopThreadPool(): _next(0) {
}

bool EventLoopThreadPool::init(int thread_num) {
    for (int i = 0; i < thread_num; ++i) {
        auto engine = std::make_shared<EventLoopEngine>();
        _workers.push_back(Worker{engine, std::make_unique<EventLoopThread>(engine)});
    }
    return true;
}

bool EventLoopThreadPool::start() {
    for (auto& worker : _workers) {
        if (!worker.thread->start()) {
            return false;
        }
    }
    return true;
}

EnginePtr EventLoopThreadPool::get_next_loop() {
    EnginePtr loop = _workers[_next].engine;
    _next = (_next + 1) % _workers.size();
    return loop;
}

EnginePtr EventLoopThreadPool::add_fd_callback(BASE::FdPtr fd, Task task) {
    auto loop = get_next_loop();
    loop->add_fd_callback(fd, std::move(task));
    return loop;
}

}
}