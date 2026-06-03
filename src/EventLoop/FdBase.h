#pragma once
#include <memory>
namespace GST {
namespace BASE {

class FdBase {
public:
    virtual ~FdBase() = default;
    virtual int fd() const = 0;

    bool is_avai() const { return fd() >= 0; }
};

using FdPtr = std::shared_ptr<FdBase>;
}
}