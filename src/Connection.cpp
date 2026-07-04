#include "Connection.h"
#include "EventLoop/EventLoopEngine.h"
#include "GstLog.h"
#include <cerrno>

namespace GST {
namespace NET {

bool Connection::init(SocketPtr socket, BASE::EventLoopEngine* engine, const ConnectionCallbacks* cbs) {
    if (!socket || !socket->is_avai()) {
        return false;
    }
    _socket = socket;
    _cbs_ptr = cbs;
    _owner_engine = engine;
    return true;
}

bool Connection::send(const std::string& data) {
    if (!_socket || !_socket->is_avai()) {
        return false;
    }
    
    // self 把连接续命到任务执行结束;data 按值拷贝进任务,避免异步执行时形参引用悬空
    auto self = shared_from_this();
    _owner_engine->run_in_loop([this, self, data]() {
        // 添加一个长度信息用于拆包分包
        uint32_t net_len = htonl(data.size());
        std::string framed;
        framed.reserve(4 + data.size());
        framed.append(reinterpret_cast<const char*>(&net_len), 4);
        framed.append(data);

        if (_send_buf.readable_bytes() > 0) {
            _send_buf.write(framed);
            return;
        }

        int n = _socket->send(framed.data(), framed.size());
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                return;
            }
            n = 0;
        }

        size_t sent = static_cast<size_t>(n);
        if (sent < framed.size()) {
            _send_buf.write(framed.data() + sent, framed.size() - sent);
            // 注册写关注;回调里用 weak 观察连接,不僭越所有权
            std::weak_ptr<Connection> weak = weak_from_this();
            _owner_engine->enable_write(_socket, [weak](int) {
                if (auto c = weak.lock()) {
                    c->handle_write();
                }
            });
        }
    });

    return true;
}

void Connection::handle_write() {
    if (_send_buf.readable_bytes() == 0) {
        _owner_engine->disable_write(_socket->fd());
        return;
    }
    auto view = _send_buf.peek();
    int n = _socket->send(view.data(), view.size());
    if (n > 0) {
        _send_buf.consume(static_cast<size_t>(n));
    }
    if (_send_buf.readable_bytes() == 0) {
        _owner_engine->disable_write(_socket->fd());
        // drain 干净了:若对端已 EOF 在等我发完,现在才真正关。
        if (_close_after_write) {
            handle_close();   // 必为最后一句;close 后 this 可能即死,靠 engine lambda 里 weak.lock() 的 c 续命护住
        }
    }
}

void Connection::handle_read() {
    std::string data;
    int n = _socket->recv(data);
    int saved_errno = errno;   // recv 后立刻存,后面任何 libc 调用(WARN 也算)都会改写 errno

    if (n < 0) {
        if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
            return;   // 内核接收缓冲暂时空了,不是错误;LT 下还会再叫,等下次
        }
        WARN("recv error fd=%d: %s", _socket->fd(), std::strerror(saved_errno));
        handle_close();   // 真错误(ECONNRESET 等)必须关:否则坏 fd 留在 LT epoll 里被反复报可读→busy loop+泄漏
        return;           // handle_close 后 this 可能即死,本路径到此为止
    }

    if (n == 0) {
        // 对端关了写方向(EOF),但它可能还在等我把没发完的发完
        if (_send_buf.readable_bytes() == 0) {
            handle_close();   // 没有待发数据,直接关
            return;           // close 后立刻收,别再碰成员
        }
        // 还有没发完的:摘掉读关注(否则 LT 下 recv 会一直返 0、CPU 空转),记下"发完就关",
        // 剩下交给已在转的 handle_write —— drain 到 0 时它来收尾
        _close_after_write = true;
        _owner_engine->disable_read(_socket->fd());
        return;
    }

    _recv_buf.write(data);
    if (!_cbs_ptr || !_cbs_ptr->message_cb) {
        return;
    }
    while (_recv_buf.readable_bytes() > 0) {
        if (_recv_buf.readable_bytes() < 4) {
            DEBUG("header incomplete, waiting: have %zu bytes", _recv_buf.readable_bytes());
            break;
        }

        uint32_t body_length = 0;
        std::memcpy(&body_length, _recv_buf.peek().data(), 4);
        body_length = ntohl(body_length);

        if (body_length > MAX_MESSAGE_SIZE) {
            WARN("body_length=%u exceeds MAX_MESSAGE_SIZE, closing connection", body_length);
            handle_close();
            return;
        }

        if (_recv_buf.readable_bytes() < 4 + body_length) {
            DEBUG("body incomplete, need %u have %zu bytes", 4 + body_length, _recv_buf.readable_bytes());
            break;
        }

        std::string msg(_recv_buf.peek().substr(4, body_length));
        _recv_buf.consume(4 + body_length);
        _cbs_ptr->message_cb(shared_from_this(), std::move(msg));
    }
}

void Connection::handle_close() {
    // 先库清理,后用户回调,符合"_sys_close_cb 先于 close_cb"那条约束。
    // 库清理内部:先摘 epoll + 清 fd 账本(此刻 fd 还开着,c 续命护住),再从 Server::_connections 移除。
    if (_owner_engine) {
        _owner_engine->remove_fd(_socket->fd());
    }
    if (_sys_close_cb) {
        _sys_close_cb(shared_from_this());
    }
    if (_cbs_ptr && _cbs_ptr->close_cb) {
        _cbs_ptr->close_cb(shared_from_this());
    }
}

} // namespace NET
} // namespace GST
