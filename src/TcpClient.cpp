#include "TcpClient.h"
#include "GstLog.h"

namespace GST {
namespace NET {

TcpClient::TcpClient(std::unique_ptr<Codec> codec)
    : _codec(std::move(codec)),
      _engine(std::make_shared<BASE::EventLoopEngine>()),
      _loop_thread(std::make_unique<BASE::EventLoopThread>(_engine)) {
    // 包一层做"主动 close 不回调":_active_close 和本回调都只在 IO 线程上跑,免锁
    _cbs.close_cb = [this](ConnectionPtr conn) {
        if (_active_close) {
            return;
        }
        if (_close_cb) {
            _close_cb(conn);
        }
    };
}

TcpClient::~TcpClient() {
    close();
    // 剩下交给成员析构:_loop_thread 最先死(stop + join IO 线程),
    // 没来得及跑的关闭任务随 engine 的 pending 队列销毁,fd 由 Socket 的 shared_ptr 兜底关
}

bool TcpClient::connect(const std::string& ip, int port) {
    if (!_codec) {
        WARN("connect rejected: no codec");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(_conn_mutex);
        if (_conn) {
            return false;   // 已有活连接,先 close 再连
        }
    }

    auto socket = std::make_shared<Socket>();
    if (!socket->connect(ip, port)) {
        return false;
    }

    // engine 线程首次 connect 才起;断线重连复用同一条。
    // 拿锁防两个线程同时首次 connect 时 double start;失败不置位,下次还能重试
    {
        std::lock_guard<std::mutex> lock(_conn_mutex);
        if (!_engine_started) {
            if (!_loop_thread->start()) {
                return false;
            }
            _engine_started = true;
        }
    }

    auto conn = std::make_shared<Connection>();
    if (!conn->init(socket, _engine.get(), &_cbs, _codec.get())) {
        return false;
    }
    conn->set_sys_close_callback([this](ConnectionPtr) {
        std::lock_guard<std::mutex> lock(_conn_mutex);
        _conn.reset();
    });

    // 先记账再注册:注册进 epoll 的瞬间事件就可能来,顺序反了会漏掉关闭清理
    {
        std::lock_guard<std::mutex> lock(_conn_mutex);
        _conn = conn;
    }

    // 注册走 run_in_loop 挪到 IO 线程做:poller 的关注集账本只归 IO 线程碰
    std::weak_ptr<Connection> weak = conn;
    auto* engine = _engine.get();
    _engine->run_in_loop([engine, socket, weak]() {
        if (!engine->add_fd_callback(socket, [weak](int) {
            if (auto c = weak.lock()) {
                c->handle_read();
            }
        })) {
            ERROR("failed to watch client fd=%d", socket->fd());
        }
    });

    return true;
}

bool TcpClient::send(const std::string& data) {
    ConnectionPtr conn;
    {
        std::lock_guard<std::mutex> lock(_conn_mutex);
        conn = _conn;
    }
    if (!conn) {
        return false;
    }
    return conn->send(data);
}

void TcpClient::close() {
    ConnectionPtr conn;
    {
        std::lock_guard<std::mutex> lock(_conn_mutex);
        conn = _conn;
    }
    if (!conn) {
        return;
    }
    _engine->run_in_loop([this, conn]() {
        {
            // 到 IO 线程时可能已被动关过/重复 close 过(_conn 已清空或换人),这单作废
            std::lock_guard<std::mutex> lock(_conn_mutex);
            if (_conn != conn) {
                return;
            }
        }
        _active_close = true;
        conn->handle_close();
        _active_close = false;
    });
}

} // namespace NET
} // namespace GST
