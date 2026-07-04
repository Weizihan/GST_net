#pragma once

#include <vector>
#include <memory>
#include "EventLoopThread.h"

namespace GST {
namespace BASE {

class EventLoopThreadPool {
public:
    EventLoopThreadPool();

    ~EventLoopThreadPool() = default;

    bool init(int thread_num);

    bool start();

    EnginePtr get_next_loop();

private:
    struct Worker {
        EnginePtr engine;
        std::unique_ptr<EventLoopThread> thread;
    };

    int _next;
    std::vector<Worker> _workers;
};

} // namespace BASE
} // namespace GST
