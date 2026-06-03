#include "Server.h"
#include "GstLog.h"

namespace GST {
namespace NET {

Server::Server() : _running(false) {}

Server::~Server() {
    stop();
}

bool Server::init(SockOption opt) {
    _sock_ptr = std::make_shared<Socket>();
    if (!_sock_ptr->init(opt)) {
        return false;
    }

    return _loop.init(_sock_ptr, opt.thread_num);
}

bool Server::run() {
    if (_running) {
        return true;
    }
    _running = true;

    _loop.set_accept_callback([this](int) {
        on_new_connection();
    });

    if (!_loop.start()) {
        return false;
    }
    _loop.wait();
    return true;
}

void Server::on_new_connection() {
    SocketPtr client = _sock_ptr->new_client();
    if (!client || !client->is_avai()) {
        return;
    }

    auto conn = std::make_shared<Connection>();
    if (!conn->init(client, &_shared_cbs)) {
        return;
    }

    int client_fd = client->fd();
    conn->set_sys_close_callback([this, client_fd](ConnectionPtr) {
        std::lock_guard<std::mutex> lock(_conn_mutex);
        _connections.erase(client_fd);
    });

    std::weak_ptr<Connection> weak = conn;
    auto engine = _loop.add_fd_callback(client, [weak](int) {
        if (auto c = weak.lock()) {
            c->handle_read();
        }
    });

    // engine 即线程池为这条连接选中的家;Connection 后续 send 把写任务抛回它
    conn->set_owner_engine(engine.get());

    {
        std::lock_guard<std::mutex> lock(_conn_mutex);
        _connections[client_fd] = conn;
    }

    INFO("new connection fd=%d", client_fd);

    if (_connect_cb) {
        _connect_cb(conn);
    }
}

void Server::stop() {
    if (!_running) {
        return;
    }
    _running = false;
    _loop.stop();
}

} // namespace NET
} // namespace GST
