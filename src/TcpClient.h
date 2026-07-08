#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "Codec.h"
#include "Connection.h"
#include "EventLoop/EventLoopThread.h"

namespace GST {
namespace NET {

class TcpClient {
public:
    using MessageCb = Connection::MessageCb;
    using CloseCb   = Connection::CloseCb;

    // codec 必须显式传入,不设默认:默认协议会让"忘了配 codec"悄悄跑起来,
    // 和对端各说各话;不给 codec 则 connect 直接失败
    explicit TcpClient(std::unique_ptr<Codec> codec);
    ~TcpClient();

    // 阻塞式建连,成功返回后即可 send。对端不可达会阻塞到内核超时(约两分钟),
    // 别在 UI 线程调;connect 超时/自动重连等定时器就位后再补
    bool connect(const std::string& ip, int port);

    bool send(const std::string& data);

    // 主动断开,不触发 close_callback(用户自己调的自己知道)
    void close();

    // 回调都在库内 IO 线程上执行,别干重活;请在 connect 前设置
    void set_message_callback(MessageCb cb) {
        _cbs.message_cb = std::move(cb);
    }

    // 只在被动断线(对端关闭/出错)时触发
    void set_close_callback(CloseCb cb) {
        _close_cb = std::move(cb);
    }

private:
    std::unique_ptr<Codec> _codec;
    CloseCb                _close_cb;
    ConnectionCallbacks    _cbs;
    bool                   _active_close   = false;   // 只在 IO 线程读写
    bool                   _engine_started = false;   // 由 _conn_mutex 保护

    std::mutex             _conn_mutex;
    ConnectionPtr          _conn;

    // engine 和它的线程声明在最后 => 最先析构(stop + join),
    // 保证 IO 线程死透之前上面这些成员都还活着
    BASE::EnginePtr        _engine;
    std::unique_ptr<BASE::EventLoopThread> _loop_thread;
};

} // namespace NET
} // namespace GST
