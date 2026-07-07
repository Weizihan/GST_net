#include "LengthHeaderCodec.h"

#include <arpa/inet.h>
#include <cstring>

namespace GST {
namespace NET {

Buffer LengthHeaderCodec::encode(const std::string& message) {
    Buffer frame;
    uint32_t net_len = htonl(static_cast<uint32_t>(message.size()));
    frame.write(reinterpret_cast<const char*>(&net_len), 4);
    frame.write(message);
    return frame;
}

DecodeResult LengthHeaderCodec::decode(Buffer& in) {
    if (in.readable_bytes() < 4) {
        return {DecodeStatus::NEED_MORE, {}};
    }

    uint32_t body_length = 0;
    std::memcpy(&body_length, in.peek().data(), 4);
    body_length = ntohl(body_length);

    if (body_length > _max_size) {
        return {DecodeStatus::BAD, {}};
    }

    // body_length 已被 _max_size 卡住,这里用 size_t 相加纯为防溢出习惯
    if (in.readable_bytes() < static_cast<size_t>(body_length) + 4) {
        return {DecodeStatus::NEED_MORE, {}};
    }

    std::string body(in.peek().substr(4, body_length));
    in.consume(4 + body_length);
    return {DecodeStatus::OK, std::move(body)};
}

} // namespace NET
} // namespace GST
