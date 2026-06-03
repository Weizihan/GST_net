#pragma once

#include <iostream>
#include <pthread.h>
#include <tuple>
#include <utility>


namespace GST {
namespace BASE {

class Thread {
public:
    template<typename Func, typename... Args>
    explicit Thread(Func&& func, Args&&... args) {
        data = new ThreadData<Func, Args...>(std::forward<Func>(func), std::forward<Args>(args)...);
        pthread_create(&thread, nullptr, ThreadData<Func, Args...>::invoke, data);
    }

    Thread();
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    Thread(Thread&& other) noexcept : thread(other.thread), data(other.data) {
        other.data = nullptr;
    }

    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) {
            if (data) {
                delete data;
            }
            thread = other.thread;
            data = other.data;
            other.thread = 0;
            other.data = nullptr;
        }
        return *this;
    }

    void join();

    void detach();

    ~Thread();
private:
    pthread_t thread = 0;
    struct ThreadDataBase {
        virtual ~ThreadDataBase() = default;
    };

    template<typename Func, typename... Args>
    struct ThreadData : ThreadDataBase {
        Func func;
        std::tuple<Args...> args;

        ThreadData(Func&& f, Args&&... a) : func(std::forward<Func>(f)), args(std::forward<Args>(a)...) {}

        static void* invoke(void* arg) {
            auto* data = static_cast<ThreadData*>(arg);
            data->callFunc(std::index_sequence_for<Args...>{});
            return nullptr;
        }

        template<std::size_t... I>
        void callFunc(std::index_sequence<I...>) {
            func(std::get<I>(args)...);
        }
    };

    ThreadDataBase* data;
};

}
}
