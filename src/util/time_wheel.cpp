//
// Created by inory on 10/30/25.
//

#include "time_wheel.h"

auto TimerWheel::addTimer(Duration timeout, Callback cb, bool repeat) -> std::shared_ptr<TimerWheel::Timer> {
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto timer = std::make_shared<Timer>();
    timer->timeout = timeout;
    timer->cb = std::move(cb);
    timer->repeat = repeat;

    size_t ticks = timeout.count() / tick_interval_.count();
    size_t slot = (current_slot_ + ticks) % slots_;
    timer->rotations = ticks / slots_;
    timer->slot = slot;  // 记录 slot

    wheel_[slot].push_back(timer);
    return timer;
}

void TimerWheel::refresh(std::shared_ptr<Timer> timer) {
    if (!timer || timer->canceled) return;
    
    std::lock_guard<std::mutex> lock(mtx_);

    removeLocked(timer);

    // 重新计算 slot 和 rotations
    size_t ticks = timer->timeout.count() / tick_interval_.count();
    size_t new_slot = (current_slot_ + ticks) % slots_;
    timer->rotations = ticks / slots_;
    timer->slot = new_slot;  // 更新 slot

    wheel_[new_slot].push_back(timer);
}

void TimerWheel::triggerNow(std::shared_ptr<Timer> timer) {
    if (!timer)
        return;
    
    Callback cb;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        removeLocked(timer);
        if (timer->cb && !timer->canceled) {
            cb = timer->cb;  // 拷贝回调，避免在锁内执行
        }
    }
    
    // 在锁外执行回调，避免死锁
    if (cb) {
        cb();
    }
}

void TimerWheel::cancel(std::shared_ptr<Timer> timer) {
    if (timer) {
        std::lock_guard<std::mutex> lock(mtx_);
        timer->canceled = true;
        removeLocked(timer);  // 立即从 wheel 中移除
    }
}

void TimerWheel::tick() {
    // check real tick
    auto now = Clock::now();
    auto elapsed = std::chrono::duration_cast<Duration>(now - last_tick_time_);
    constexpr auto tolerance = Duration(1);
    if (elapsed + tolerance < tick_interval_) {
        return;
    }
    
    std::vector<Callback> expired_callbacks;
    
    {
        std::lock_guard<std::mutex> lock(mtx_);
        
        auto &bucket = wheel_[current_slot_];
        for (auto it = bucket.begin(); it != bucket.end();) {
            auto timer = *it;
            if (timer->canceled) {
                it = bucket.erase(it);
                continue;
            }
            if (timer->rotations > 0) {
                timer->rotations--;
                ++it;
                continue;
            }
            // 到期，收集回调
            if (timer->cb) {
                expired_callbacks.push_back(timer->cb);
            }

            if (timer->repeat && !timer->canceled) {
                // 重新入轮（内部已加锁，这里会死锁，需要修改）
                // 临时方案：记录需要重新添加的定时器
                size_t ticks = timer->timeout.count() / tick_interval_.count();
                size_t new_slot = (current_slot_ + 1 + ticks) % slots_;
                timer->rotations = ticks / slots_;
                timer->slot = new_slot;
                wheel_[new_slot].push_back(timer);
            }
            it = bucket.erase(it);
        }
        current_slot_ = (current_slot_ + 1) % slots_;
        last_tick_time_ = now;
    }
    
    // 在锁外执行回调，避免死锁和长时间持锁
    for (auto& cb : expired_callbacks) {
        cb();
    }
}

int TimerWheel::nextTimeoutMs() const {
    auto now = Clock::now();
    auto elapsed = std::chrono::duration_cast<Duration>(now - last_tick_time_);
    auto remaining = tick_interval_ - elapsed;
    if (remaining.count() <= 0) {
        return 0;
    }
    return static_cast<int>(remaining.count());
}

void TimerWheel::removeLocked(const std::shared_ptr<Timer> &timer) {
    auto& bucket = wheel_[timer->slot];
    for (auto it = bucket.begin(); it != bucket.end();) {
        if (it->get() == timer.get()) {
            bucket.erase(it);
            return;
        } else ++it;
    }
}


