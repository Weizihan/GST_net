#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>

#include "EventLoop/FdBase.h"

namespace GST {
namespace NET {
class Socket;

using SocketPtr = std::shared_ptr<Socket>;
enum NetType {
    TCP,
    UDP
};

struct SockOption {
    NetType type           = TCP;
    int     port           = 8002;
    int     thread_num     = 4;
    bool    is_unique_port = true;
    bool    is_block       = false;
};

constexpr int BACK_LOG = 5;

class Socket : public BASE::FdBase{
public:
    Socket(int i = -1);
    ~Socket();
    
    int fd() const override;
    bool init(SockOption opt);
    std::shared_ptr<Socket> new_client();
    int send(const char* data, size_t len);
    int recv(std::string& msg);
private:
    int _sock_fd;
    NetType _type;
};

} // namespace NET
} // namespace GST
