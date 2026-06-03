#include "Buffer.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace GST {
namespace NET {

Buffer::Buffer(size_t initial_size)
    : _buf(initial_size), _read_idx(0), _write_idx(0) {
}

void Buffer::write(const char* data, size_t len) {
    if (writable_bytes() < len) {
        // 数据挪动 提升空间
        std::memmove(_buf.data(), _buf.data() + _read_idx, readable_bytes());
        _write_idx -= _read_idx;
        _read_idx = 0;
    }

    if (writable_bytes() < len) {
        _buf.resize(_write_idx + len);
    }
    std::copy(data, data + len, _buf.begin() + _write_idx);
    _write_idx += len;
}

void Buffer::write(const std::string& data) {
    write(data.data(), data.size());
}

std::string_view Buffer::peek() const {
    return { _buf.data() + _read_idx, _write_idx - _read_idx };
}

void Buffer::consume(size_t n) {
    n = std::min(n, readable_bytes());
    _read_idx += n;
    reset_if_empty();
}

size_t Buffer::readable_bytes() const {
    return _write_idx - _read_idx;
}

size_t Buffer::writable_bytes() const {
    return _buf.size() - _write_idx;
}

void Buffer::reset_if_empty() {
    if (_read_idx == _write_idx) {
        _read_idx = 0;
        _write_idx = 0;
    }
}

} // namespace NET
} // namespace GST
