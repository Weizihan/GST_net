#pragma once

#include "Concurrency/Thread.h"
#include "EventLoopEngine.h"
#include <memory>

namespace GST {
namespace BASE {
using EnginePtr = std::shared_ptr<EventLoopEngine>;

class EventLoopThread {
public:
    explicit EventLoopThread(EnginePtr engine_ptr);
    ~EventLoopThread();

    bool start();

private:
    EnginePtr _engine_ptr;
    Thread _thread;
};

} // namespace BASE
} // namespace GST
