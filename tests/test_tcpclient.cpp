// TcpClient 冒烟测试:进程内起一个 echo Server,客户端对着它打
// 覆盖:echo 往返 / 主动 close 不触发 close_cb / close 后 send 被拒 / 重连 / 并发首次 connect
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "LengthHeaderCodec.h"
#include "Server.h"
#include "TcpClient.h"

using namespace GST::NET;
using namespace std::chrono_literals;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                          \
    do {                                           \
        if (cond) {                                \
            printf("PASS  %s\n", name);            \
            ++g_pass;                              \
        } else {                                   \
            printf("FAIL  %s\n", name);            \
            ++g_fail;                              \
        }                                          \
    } while (0)

int main() {
    const int port = 18402;

    Server server;
    SockOption opt;
    opt.port = port;
    if (!server.init(opt)) {
        printf("server init failed (port %d occupied?)\n", port);
        return 1;
    }
    server.set_message_callback([](ConnectionPtr conn, std::string data) {
        conn->send(data);
    });
    std::thread server_thread([&server]() {
        server.run();
    });
    std::this_thread::sleep_for(200ms);

    {
        TcpClient no_codec(nullptr);
        CHECK(!no_codec.connect("127.0.0.1", port), "connect without codec rejected");
    }

    {
        TcpClient client(std::make_unique<LengthHeaderCodec>());
        std::mutex mtx;
        std::condition_variable cv;
        std::string received;
        client.set_message_callback([&](ConnectionPtr, std::string data) {
            std::lock_guard<std::mutex> lock(mtx);
            received = std::move(data);
            cv.notify_one();
        });
        bool close_fired = false;
        client.set_close_callback([&](ConnectionPtr) {
            close_fired = true;
        });

        CHECK(client.connect("127.0.0.1", port), "connect ok");
        CHECK(client.send("hello gst"), "send ok");
        {
            std::unique_lock<std::mutex> lock(mtx);
            bool got = cv.wait_for(lock, 3s, [&]() { return !received.empty(); });
            CHECK(got && received == "hello gst", "echo roundtrip");
        }

        client.close();
        std::this_thread::sleep_for(300ms);
        CHECK(!close_fired, "active close does not fire close_cb");
        CHECK(!client.send("x"), "send after close rejected");
        CHECK(client.connect("127.0.0.1", port), "reconnect ok");
    }

    // 4 线程同时打第一枪:engine 不 double-start、事后连接可用
    {
        TcpClient client(std::make_unique<LengthHeaderCodec>());
        std::atomic<int> ok_cnt{0};
        std::atomic<bool> go{false};
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&]() {
                while (!go.load()) {
                    std::this_thread::yield();
                }
                if (client.connect("127.0.0.1", port)) {
                    ++ok_cnt;
                }
            });
        }
        go = true;
        for (auto& t : threads) {
            t.join();
        }
        CHECK(ok_cnt.load() >= 1, "concurrent first connect: at least one wins");
        std::this_thread::sleep_for(200ms);
        CHECK(client.send("after race"), "send works after concurrent connect");
    }

    printf("---\n%d passed, %d failed\n", g_pass, g_fail);
    // Server 还没有优雅退出接口(REPORT.md P1-5),先硬退进程
    fflush(stdout);
    _exit(g_fail == 0 ? 0 : 1);
}
