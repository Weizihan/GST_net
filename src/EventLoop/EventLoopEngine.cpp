#include "EventLoopEngine.h"
#include "EventFd.h"

#include <cstdint>
namespace GST {
namespace BASE {

EventLoopEngine::EventLoopEngine() : _server_fd(-1), _is_running(false) {}

EventLoopEngine::~EventLoopEngine() {
    _is_running = false;
}

bool EventLoopEngine::init(int server_fd) {
    _server_fd = server_fd;
    _wakeup_fd = std::make_shared<EventFd>();
    return _wakeup_fd->is_avai() && _poller.init();
}

bool EventLoopEngine::add_watch(FdPtr fd, EVENT_TYPE type) {
    return _poller.add_fd(fd, type);
}

void EventLoopEngine::register_accept_callback(Task cb) {
    _accept_cb = std::move(cb);
}

bool EventLoopEngine::add_fd_callback(FdPtr fd, Task cb) {
    {
        std::lock_guard<std::mutex> lock(_cb_mutex);
        _read_cbs[fd->fd()] = std::move(cb);
    }
    return _poller.add_fd(fd, READ_EVENT);
}

bool EventLoopEngine::enable_write(FdPtr fd, Task cb) {
    {
        std::lock_guard<std::mutex> lock(_cb_mutex);
        _write_cbs[fd->fd()] = std::move(cb);
    }
    return _poller.enable(fd->fd(), WRITE_EVENT);
}

bool EventLoopEngine::disable_write(int fd) {
    {
        std::lock_guard<std::mutex> lock(_cb_mutex);
        _write_cbs.erase(fd);
    }
    return _poller.disable(fd, WRITE_EVENT);
}

bool EventLoopEngine::disable_read(int fd) {
    // 只摘 epoll 读关注,不动 _read_cbs:EPOLLERR 分支还要靠它找回调
    return _poller.disable(fd, READ_EVENT);
}

bool EventLoopEngine::remove_fd(int fd) {
    {
        std::lock_guard<std::mutex> lock(_cb_mutex);
        _read_cbs.erase(fd);
        _write_cbs.erase(fd);
    }
    return _poller.remove_fd(fd);
}

void EventLoopEngine::stop() {
    _is_running = false;
}

void EventLoopEngine::run_in_loop(std::function<void()> task) {
    // 我是不是这条连接的家 IO 线程?
    if (std::this_thread::get_id() == _thread_id) {
        // 是家线程,没人跟我抢 _send_buf,当场跑,省一次入队+唤醒
        task();
    } else {
        // 跨线程(worker)调进来:加锁入队,再戳 eventfd 把 IO 线程从 epoll_wait 捅醒
        {
            std::lock_guard<std::mutex> lock(_pending_mutex);
            _pending.emplace_back(std::move(task));
        }
        uint64_t one = 1;
        ::write(_wakeup_fd->fd(), &one, sizeof(one));   // eventfd 必须正好写 8 字节
    }
}

void EventLoopEngine::start() {
    _is_running = true;
    _thread_id = std::this_thread::get_id();
    std::vector<epoll_event> events;
    _poller.add_fd(_wakeup_fd, READ_EVENT);

    while (_is_running) {
        int num_events = _poller.poll(events, 500);

        for (int i = 0; i < num_events; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (ev & EPOLLIN) {
                if (fd == _server_fd) {
                    if (_accept_cb) {
                        _accept_cb(fd);
                    }
                } else if (fd == _wakeup_fd->fd()) {
                    uint64_t cnt;
                    ::read(fd, &cnt, sizeof(cnt));

                    decltype(_pending) tmp_pending; 
                    {
                        std::lock_guard<std::mutex> lock(_pending_mutex);
                        tmp_pending.swap(_pending);
                    }

                    for (auto& it : tmp_pending) {
                        it();
                    }
                } else {
                    Task cb;
                    {
                        std::lock_guard<std::mutex> lock(_cb_mutex);
                        auto it = _read_cbs.find(fd);
                        if (it != _read_cbs.end()) {
                            cb = it->second;
                        }
                    }
                    if (cb) {
                        cb(fd);
                    }
                }
            }

            if (ev & EPOLLOUT) {
                Task cb;
                {
                    std::lock_guard<std::mutex> lock(_cb_mutex);
                    auto it = _write_cbs.find(fd);
                    if (it != _write_cbs.end()) {
                        cb = it->second;
                    }
                }
                if (cb) {
                    cb(fd);
                }
            }

            if (ev & EPOLLERR) {
                Task cb;
                {
                    std::lock_guard<std::mutex> lock(_cb_mutex);
                    auto it = _read_cbs.find(fd);
                    if (it != _read_cbs.end()) {
                        cb = it->second;
                    }
                }
                if (cb) {
                    cb(fd);
                }
            }
        }
    }
}

}
}