#pragma once

#include <unordered_map>
#include <mutex>
#include <functional>
#include <string>
#include <memory>

#include "Socket.h"
#include "ServerEventLoop.h"
#include "Connection.h"
#include "Concurrency/ThreadPool.h"

namespace GST {
namespace NET {

class Server {
public:
    using ConnectCb = std::function<void(ConnectionPtr)>;
    using MessageCb = Connection::MessageCb;
    using CloseCb   = Connection::CloseCb;

    Server();
    ~Server();

    bool init(SockOption opt = {});
    bool run();
    void stop();

    void set_connect_callback(ConnectCb cb) {
        _connect_cb = std::move(cb);
    }

    void set_message_callback(MessageCb cb) {
        _shared_cbs.message_cb = [this, cb](ConnectionPtr conn, std::string data) {
            // key=fd：同一条连接的消息在线程池里按 fd 串行处理（保序 + 免锁）
            _thread_pool.submit(conn->fd(), [cb, conn, data = std::move(data)]() {
                cb(conn, data);
            });
        };
    }

    void set_close_callback(CloseCb cb) {
        _shared_cbs.close_cb = std::move(cb);
    }

private:
    void on_new_connection();

    SocketPtr           _sock_ptr;
    bool                _running = false;
    ServerEventLoop     _loop;
    GST::BASE::ThreadPool _thread_pool;
    ConnectCb           _connect_cb;
    ConnectionCallbacks _shared_cbs;
    std::unique_ptr<Codec> _codec;   // 全连接共享一份;无状态,未来可换用户自定义协议
    std::mutex          _conn_mutex;
    std::unordered_map<int, ConnectionPtr> _connections;
};

} // namespace NET
} // namespace GST
