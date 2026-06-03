#pragma once
#include <sys/eventfd.h>
#include <unistd.h>
#include "FdBase.h"

namespace GST {
namespace BASE {

// 用于某些防止worker线程和io线程抢同一段内存数据
class EventFd : public FdBase {
public:
    EventFd() {
        _event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    }
    ~EventFd() override {
        if (_event_fd >= 0) {
            close(_event_fd);
        }
    }
    int fd() const override {
        return _event_fd;
    }
private:
    int _event_fd;
};

} // namespace BASE
} // namespace GST
