//
// Created by inory on 10/30/25.
//

#ifndef TIME_WHEEL_H
#define TIME_WHEEL_H

#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>
#include <optional>

class TimerWheel {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::milliseconds;
    using Callback = std::function<void()>;

    struct Timer {
        Duration timeout;
        size_t rotations;   // 剩余轮数
        Callback cb;
        bool repeat = false;
        bool canceled = false;
    };

    static TimerWheel& getInst() {
        static TimerWheel inst;
        return inst;
    }

    ~TimerWheel() {
        stop();
    }

    auto addTimer(double seconds, Callback cb, bool repeat = false) -> std::shared_ptr<Timer>{
        auto ms = Duration(static_cast<int>(seconds * 1000));
        return addTimer(ms, std::move(cb), repeat);
    }

    // 刷新：重新放回时间轮（例如 HTTP keepalive）
    void refresh(std::shared_ptr<Timer> timer);

    // 立即触发并弹出
    void triggerNow(std::shared_ptr<Timer> timer);

    // 取消
    void cancel(std::shared_ptr<Timer> timer);

    // 时间推进：外部定期调用（比如每 100ms）
    void tick();

    // 获取下次 tick 间隔（用于 epoll_wait 超时）
    int nextTimeoutMs() const { return static_cast<int>(tick_interval_.count()); }

    // 停止
    void stop() { running_ = false; }

private:
    explicit TimerWheel(size_t slots = 256, Duration tick_interval = Duration(100))
    : slots_(slots),
      tick_interval_(tick_interval),
      wheel_(slots),
      current_slot_(0),
      running_(true)
    {}

    auto addTimer(Duration timeout, Callback cb, bool repeat = false) -> std::shared_ptr<Timer>;

    void removeLocked(const std::shared_ptr<Timer>& timer);

private:
    size_t slots_;
    Duration tick_interval_;
    std::vector<std::list<std::shared_ptr<Timer>>> wheel_;
    size_t current_slot_;
    std::mutex mtx_;
    std::atomic<bool> running_;
};


#endif //TIME_WHEEL_H
