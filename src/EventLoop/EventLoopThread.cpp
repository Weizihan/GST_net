#include "EventLoopThread.h"

namespace GST {
namespace BASE {

EventLoopThread::EventLoopThread(EnginePtr engine_ptr):
    _engine_ptr(engine_ptr) {
}

EventLoopThread::~EventLoopThread() {
    _engine_ptr->stop();
    _thread.join(); 
}

bool EventLoopThread::start() {
    if (!_engine_ptr->init()) {
        return false;
    }
    _thread = Thread([this]() {
        _engine_ptr->start();
    });
    return true;
}

} // namespace BASE
} // namespace GST
