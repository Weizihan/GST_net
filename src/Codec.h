#pragma once

#include <string>

#include "Buffer.h"

namespace GST {
namespace NET {

// decode 一次调用的三种结果:
//   OK        -> 切出了一条完整消息,message 有效
//   NEED_MORE -> 数据不够一条,message 为空,且 in 未被消费(等下次更多数据)
//   BAD       -> 坏帧(如长度头声明的 body 超过上限),调用方应关闭连接
enum class DecodeStatus { OK, NEED_MORE, BAD };

struct DecodeResult {
    DecodeStatus status;
    std::string  message;
};

// 协议层:只做「消息 <-> 字节流」的翻译,不碰网络收发,也不碰业务。
// client 和 server 共享同一份实现,保证两端协议一致(机制在库,策略由用户)。
//
// 用户继承本类,自己实现一种具体协议(如「4 字节长度头 + 消息体」)。
class Codec {
public:
    virtual ~Codec() = default;

    // 消息 -> 字节流:把一条逻辑消息编码成一个完整帧(含协议头),整帧返回。
    // encode 无状态:一条消息永远对应一个自包含的帧,不依赖调用方已有的积压数据。
    // 调用方拿到帧后自行决定是直发还是排进发送缓冲。
    virtual Buffer encode(const std::string& message) = 0;

    // 从 in 尝试切出「一条」完整消息,三态含义见 DecodeResult。
    //   NEED_MORE:不得消费 in;OK:consume 掉这条占用的字节;BAD:交由调用方关连接。
    // 调用方在一次 read 后循环调用,直到返回 NEED_MORE。
    virtual DecodeResult decode(Buffer& in) = 0;
};

} // namespace NET
} // namespace GST
