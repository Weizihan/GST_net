#pragma once
#include <sys/epoll.h>
#include <vector>
#include <unistd.h>
#include <cstdint>
#include <unordered_map>

#include "FdBase.h"

const int DEFAULT_MAX_EVENTS = 256;

// 抽象事件位,可按位 OR 组合(enable/disable 接收的就是这种组合掩码)。
// 是 2 的幂,别动间距;真正映射到 EPOLLIN/EPOLLOUT 在 Poller 内部完成。
enum EVENT_TYPE {

    // 新的客户端连接
    NEW_CONNECTION = 0x01,

    // 可读事件
    READ_EVENT     = 0x02,

    // 可写事件
    WRITE_EVENT    = 0x04
};

namespace GST {
namespace BASE {

class Poller {
public:
    ~Poller();

    Poller();
    bool init();

    // mask 为抽象位组合(EVENT_TYPE 的 OR)。add_fd 落初始掩码,enable/disable 在其上按位加减。
    bool add_fd(FdPtr fd, uint32_t mask);
    bool remove_fd(int fd);
    bool enable(int fd, uint32_t mask);
    bool disable(int fd, uint32_t mask);

    int poll(std::vector<epoll_event>& events, int timeout_ms = 500);

private:
    // 抽象位 → epoll 宏,唯一碰 EPOLLIN/EPOLLOUT 的地方
    uint32_t to_epoll(uint32_t mask) const;
    // 按 _interest[fd] 当前值重新 MOD 进内核
    bool apply(int fd);

    int _poll_fd;
    // 每个 fd 当前关注集(抽象位),关注集的唯一真相源。
    // 仅在 owner engine 的 IO 线程上读写,故不加锁;跨线程的改动都已走 run_in_loop。
    std::unordered_map<int, uint32_t> _interest;
};

}
}
