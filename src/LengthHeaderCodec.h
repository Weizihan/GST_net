#pragma once

#include <cstdint>

#include "Codec.h"

namespace GST {
namespace NET {

// 具体协议:4 字节大端长度头 + 消息体。
// client 和 server 共用这一份,保证两端讲同一种帧格式。
class LengthHeaderCodec : public Codec {
public:
    // max_message_size:单条 body 上限,防对端用伪造的超大长度头把接收缓冲撑爆(OOM DoS)。
    explicit LengthHeaderCodec(uint32_t max_message_size = 16 * 1024 * 1024)
        : _max_size(max_message_size) {}

    Buffer encode(const std::string& message) override;
    DecodeResult decode(Buffer& in) override;

private:
    uint32_t _max_size;
};

} // namespace NET
} // namespace GST
