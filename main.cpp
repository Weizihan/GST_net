#include <iostream>
#include "src/Server.h"

int main() {
    GST::NET::Server server;

    if (!server.init()) {
        std::cerr << "server init failed\n";
        return -1;
    }

    server.set_connect_callback([](GST::NET::ConnectionPtr) {
        std::cout << "client connected\n";
    });

    server.set_message_callback([](GST::NET::ConnectionPtr conn, std::string data) {
        conn->send(data);
    });

    server.set_close_callback([](GST::NET::ConnectionPtr) {
        std::cout << "client disconnected\n";
    });

    server.run();
    return 0;
}
