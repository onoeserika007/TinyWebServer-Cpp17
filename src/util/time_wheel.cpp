//
// Created by inory on 10/30/25.
//

#include "time_wheel.h"

auto TimerWheel::addTimer(Duration timeout, Callback cb, bool repeat) -> std::shared_ptr<TimerWheel::Timer> {
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
    removeLocked(timer);
    if (timer->cb && !timer->canceled)
        timer->cb();
}

void TimerWheel::cancel(std::shared_ptr<Timer> timer) {
    if (timer) {
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
        // 到期执行
        if (timer->cb)
            timer->cb();

        if (timer->repeat && !timer->canceled) {
            // 重新入轮
            addTimer(timer->timeout, timer->cb, true);
        }
        it = bucket.erase(it);
    }
    current_slot_ = (current_slot_ + 1) % slots_;
    last_tick_time_ = now;
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


