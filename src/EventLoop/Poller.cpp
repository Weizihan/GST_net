#include "Poller.h"
#include <string.h>
#include <iostream>
namespace GST {
namespace BASE {

Poller::Poller() : _poll_fd(-1) {}

Poller::~Poller() {
    if (_poll_fd >= 0) {
        close(_poll_fd);
    }
}

bool Poller::init() {
    _poll_fd = epoll_create1(0);
    return _poll_fd >= 0;
}

uint32_t Poller::to_epoll(uint32_t mask) const {
    uint32_t ev = 0;
    if (mask & (NEW_CONNECTION | READ_EVENT)) {
        ev |= EPOLLIN;
    }
    if (mask & WRITE_EVENT) {
        ev |= EPOLLOUT;
    }
    return ev;
}

bool Poller::add_fd(FdPtr fd, uint32_t mask) {
    int ofd = fd->fd();
    epoll_event ev;
    ev.data.fd = ofd;
    ev.events = to_epoll(mask);
    if (epoll_ctl(_poll_fd, EPOLL_CTL_ADD, ofd, &ev) != 0) {
        return false;
    }
    _interest[ofd] = mask;
    return true;
}

bool Poller::remove_fd(int fd) {
    _interest.erase(fd);
    return epoll_ctl(_poll_fd, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

bool Poller::enable(int fd, uint32_t mask) {
    auto it = _interest.find(fd);
    if (it == _interest.end()) {
        // 没 add_fd 过就 MOD,内核会 ENOENT;拒绝,逼调用方先 add
        return false;
    }
    it->second |= mask;
    return apply(fd);
}

bool Poller::disable(int fd, uint32_t mask) {
    auto it = _interest.find(fd);
    if (it == _interest.end()) {
        return false;
    }
    it->second &= ~mask;
    return apply(fd);
}

bool Poller::apply(int fd) {
    epoll_event ev;
    ev.data.fd = fd;
    ev.events  = to_epoll(_interest[fd]);
    return epoll_ctl(_poll_fd, EPOLL_CTL_MOD, fd, &ev) == 0;
}

int Poller::poll(std::vector<epoll_event>& events, int timeout_ms) {
    events.resize(DEFAULT_MAX_EVENTS);
    return epoll_wait(_poll_fd, events.data(), DEFAULT_MAX_EVENTS, timeout_ms);
}

}
}
