//
// Created by inory on 10/29/25.
//

#ifndef THREADPOOL_H
#define THREADPOOL_H

#pragma once
#include <complex>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class NoCopy {
public:
    ~NoCopy() = default;
    NoCopy(const NoCopy &) = delete;
    NoCopy &operator=(const NoCopy &) = delete;

protected:
    NoCopy() = default;

private:
};

class FThreadPool : private NoCopy {
public:
    explicit FThreadPool(size_t threadCount = std::thread::hardware_concurrency());

    ~FThreadPool();

    static FThreadPool &getInst();

    size_t getThreadCnt() const { return threadCnt_; }

    template<typename F>
    void pushTask(const F &task) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(std::function<void(size_t)>(task));
            ++tasksCnt_;
        }
        condition_.notify_one(); // 补全通知
    }

    // 只有当 task 是 可移动或一次性 lambda 时，缺少 std::forward 会 无法调用移动构造/移动 operator()
    template<typename F, typename... Args>
    auto pushTask(F &&task,
                  Args &&...args) -> std::future<decltype(std::forward<F>(task)(std::forward<Args>(args)...))> {
        using RetType = decltype(std::forward<F>(task)(std::forward<Args>(args)...));
        // std::packaged_task<>的模板参数是函数签名
        using Task = std::packaged_task<RetType()>;
        if (!running_)
            return {};

        auto pkg_task = std::make_shared<Task>(std::bind(std::forward<F>(task), std::forward<Args>(args)...));
        auto&& ret = pkg_task->get_future();

        pushTask([pkg_task](const size_t threadId) {
            // printf("Task %llu working\n", threadId);
            (*pkg_task)();
        });
        condition_.notify_one();
        return std::move(ret);
    }

    void waitTasksFinish() const;

private:
    size_t tasksQueuedCnt() const;
    size_t tasksRunningCnt() const { return tasksCnt_ - tasksQueuedCnt(); }


    void worker(size_t threadId);
    // void manager()

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable condition_;
    std::atomic<bool> running_{true};
    std::atomic<bool> paused_{false};

    std::vector<std::thread> threads_;
    std::atomic<size_t> threadCnt_{0};
    std::atomic<size_t> runningThreadCount_{0};

    std::queue<std::function<void(size_t)>> tasks_ = {};
    std::atomic<size_t> tasksCnt_{0};
};

#endif // THREADPOOL_H
