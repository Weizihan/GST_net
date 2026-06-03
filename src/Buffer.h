#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <cstddef>

namespace GST {
namespace NET {

class Buffer {
public:
    explicit Buffer(size_t initial_size = 1024);

    void write(const char* data, size_t len);
    void write(const std::string& data);

    std::string_view peek() const;
    void consume(size_t n);

    size_t readable_bytes() const;

private:
    size_t writable_bytes() const;
    void reset_if_empty();

    std::vector<char> _buf;
    size_t _read_idx;
    size_t _write_idx;
};

} // namespace NET
} // namespace GST
