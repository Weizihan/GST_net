#include "Socket.h"
#include "GstLog.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
namespace GST {
namespace NET {

Socket::Socket(int i):_sock_fd(i){
}

Socket::~Socket()
{
    if (_sock_fd >= 0){
        ::close(_sock_fd);
    }
}

bool Socket::init(SockOption opt) {
    int socket_type = (opt.type == NetType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    _sock_fd = ::socket(AF_INET, socket_type, 0);
    if (_sock_fd < 0) {
        return false;
    }

    int enable = 1;
    ::setsockopt(_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (opt.is_block == false) {
        ::fcntl(_sock_fd, F_SETFL, O_NONBLOCK); 
    }

    sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(opt.port);

    int ret = ::bind(_sock_fd, (sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        WARN("bind failed: %s", std::strerror(errno));
        return false;
    }

    ret = ::listen(_sock_fd, BACK_LOG);
    if (ret < 0) {
        return false;
    }

    return _sock_fd >= 0;
}

int Socket::fd() const {
    return _sock_fd;
}

SocketPtr Socket::new_client() {
    if (!is_avai()) {
        return nullptr;
    }

    struct sockaddr_in client_addr = {0};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = ::accept(_sock_fd, (sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        return nullptr;
    }

    ::fcntl(client_fd, F_SETFL, O_NONBLOCK);

    auto client_socket = std::make_shared<Socket>(client_fd);
    return client_socket;
}

int Socket::send(const char* data, size_t len) {
    // MSG_NOSIGNAL:对端已断时让 send 返回 -1/EPIPE,而不是发 SIGPIPE 杀整个进程
    return ::send(_sock_fd, data, len, MSG_NOSIGNAL);
}

int Socket::recv(std::string& msg) {
    char buffer[10240];
    int ret = ::recv(_sock_fd, buffer, sizeof(buffer), 0);
    if (ret > 0) {
        msg.assign(buffer, static_cast<size_t>(ret));
    } else {
        msg.clear();
    }
    return ret;
}


}
}