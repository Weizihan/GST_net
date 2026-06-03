#include "ThreadPool.h"


namespace GST {
namespace BASE {

ThreadPool::ThreadPool(size_t thread_count)
    : _stop(false) {
    for (size_t i = 0; i < thread_count; ++i) {
        _workers.emplace_back([this]() { this->worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _stop = true;
    }
    _cv.notify_all();
    for (auto& t : _workers) {
        if (t.joinable()) {
            t.join();
        }
    }
}

std::list<TaskInfo>::iterator ThreadPool::find_first_enable_run_key() {
    for (auto it = _tasks.begin(); it != _tasks.end(); ++it) {
        // key == -1 表示无 key（无约束），永远可执行；有 key 则要求当前没在飞
        if (it->key == -1 || !_running_key.count(it->key)) {
            return it;
        }
    }
    return _tasks.end();
}

void ThreadPool::worker_loop() {
    while (true) {
        Task fn;
        int  key = -1;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            auto task_it = find_first_enable_run_key();
            if (task_it == _tasks.end()) {
                if (_stop) {
                    return;
                }
                _cv.wait(lock);
                continue;
            }

            key = task_it->key;
            fn  = std::move(task_it->func);
            if (key != -1) {
                _running_key.insert(key);
            }
            _tasks.erase(task_it);
        }
    
        struct KeyReleaser {
            ThreadPool* pool;
            int key;
            ~KeyReleaser() {
                if (key == -1) {
                    return;   // 无 key 任务没占 in-flight，无需清理
                }
                {
                    std::unique_lock<std::mutex> lock(pool->_mutex);
                    pool->_running_key.erase(key);
                }
                pool->_cv.notify_one();
            }
        } releaser{this, key};

        try {
            fn();
        } catch (...) {
        }
    }
}

} // namespace BASE
} // namespace GST