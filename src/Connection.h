#pragma once

#include <functional>
#include <string>
#include <memory>
#include <string_view>
#include "Socket.h"
#include "Buffer.h"

// 单条消息长度上限，防止对方用伪造的超大长度头把 _recv_buf 撑爆（OOM DoS）。
// 暂定 16MB，后续接入配置库后改成可配置项。
const int MAX_MESSAGE_SIZE = 16 * 1024 * 1024;

namespace GST {
namespace BASE { class EventLoopEngine; }
namespace NET {
class Server;
class Connection;
using ConnectionPtr = std::shared_ptr<Connection>;

struct ConnectionCallbacks {
    std::function<void(ConnectionPtr, std::string)>      message_cb;
    std::function<void(ConnectionPtr)>                   close_cb;
};

class Connection : public std::enable_shared_from_this<Connection> {
    friend class Server;

public:
    using MessageCb = std::function<void(ConnectionPtr, std::string)>;
    using CloseCb   = std::function<void(ConnectionPtr)>;

    Connection() = default;
    ~Connection() = default;

    bool init(SocketPtr socket, const ConnectionCallbacks* cbs);

    bool send(const std::string& data);

    void handle_read();
    void handle_write();
    void handle_close();

private:
    int fd() const {
        return _socket ? _socket->fd() : -1;
    }

    void set_sys_close_callback(CloseCb cb) {
        _sys_close_cb = std::move(cb);
    }

    // 库内部下发,不对用户开放;engine 所有权归 EventLoopThreadPool,这里只观察使用,故存裸指针
    void set_owner_engine(BASE::EventLoopEngine* engine) {
        _owner_engine = engine;
    }

    BASE::EventLoopEngine*     _owner_engine = nullptr;
    SocketPtr                  _socket;
    Buffer                     _recv_buf;
    Buffer                     _send_buf;
    const ConnectionCallbacks* _cbs_ptr    = nullptr;
    CloseCb                    _sys_close_cb;

    // 对端已 EOF、但 _send_buf 还有没发完的数据:等 handle_write drain 到 0 再真正关
    bool                       _close_after_write = false;
};

} // namespace NET
} // namespace GST
