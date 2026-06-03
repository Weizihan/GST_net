#pragma once

#include <vector>
#include <list>
#include <unordered_set>
#include <mutex>
#include <future>
#include <condition_variable>
#include <functional>
#include <type_traits>

namespace GST {
namespace BASE {

struct TaskInfo
{
    std::function<void()> func;
    int key;
};


class ThreadPool {
public:
    explicit ThreadPool(size_t thread_count = std::thread::hardware_concurrency());
    ~ThreadPool();

    // 带 key：同一 key 的任务串行执行（保序 + 免锁）
    template<typename Func, typename... Args>
    typename std::enable_if<
        !std::is_void<typename std::result_of<Func(Args...)>::type>::value,
        std::future<typename std::result_of<Func(Args...)>::type>
    >::type
    submit(int key, Func&& f, Args&&... args);

    template<typename Func, typename... Args>
    typename std::enable_if<
        std::is_void<typename std::result_of<Func(Args...)>::type>::value,
        void
    >::type
    submit(int key, Func&& f, Args&&... args);

    template<typename Func, typename... Args>
    typename std::enable_if<
        !std::is_void<typename std::result_of<Func(Args...)>::type>::value,
        std::future<typename std::result_of<Func(Args...)>::type>
    >::type
    submit(Func&& f, Args&&... args) {
        return submit(-1, std::forward<Func>(f), std::forward<Args>(args)...);
    }

    template<typename Func, typename... Args>
    typename std::enable_if<
        std::is_void<typename std::result_of<Func(Args...)>::type>::value,
        void
    >::type
    submit(Func&& f, Args&&... args) {
        submit(-1, std::forward<Func>(f), std::forward<Args>(args)...);
    }

private:
    using Task = std::function<void()>;

    std::vector<std::thread> _workers;
    std::unordered_set<int> _running_key;
    std::list<TaskInfo> _tasks;
    std::mutex _mutex;
    std::condition_variable _cv;
    bool _stop;

    void worker_loop();
    std::list<TaskInfo>::iterator find_first_enable_run_key();
};

    // 非 void 返回版本
template<typename Func, typename... Args>
typename std::enable_if<
    !std::is_void<typename std::result_of<Func(Args...)>::type>::value,
    std::future<typename std::result_of<Func(Args...)>::type>
>::type
ThreadPool::submit(int key, Func&& f, Args&&... args) {
    using ReturnType = typename std::result_of<Func(Args...)>::type;

    auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<Func>(f), std::forward<Args>(args)...)
    );

    {
        std::unique_lock<std::mutex> lock(_mutex);
        _tasks.push_back(TaskInfo{ [task_ptr]() { (*task_ptr)(); }, key });
    }

    _cv.notify_one();
    return task_ptr->get_future();
}

// void 返回版本
template<typename Func, typename... Args>
typename std::enable_if<
    std::is_void<typename std::result_of<Func(Args...)>::type>::value,
    void
>::type
ThreadPool::submit(int key, Func&& f, Args&&... args) {
    auto bound_func = std::bind(std::forward<Func>(f), std::forward<Args>(args)...);
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _tasks.push_back(TaskInfo{ [bound_func]() { bound_func(); }, key });
    }

    _cv.notify_one();
}

} // namespace BASE
} // namespace GST



