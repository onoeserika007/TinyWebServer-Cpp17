//
// Created by inory on 10/29/25.
//

#include "threadpool.h"

FThreadPool::FThreadPool(const size_t threadCount): threadCnt_(threadCount) {
  for (int i = 0; i < threadCnt_; i++) {
    threads_.emplace_back(&FThreadPool::worker, this, i);
  }
}

FThreadPool::~FThreadPool() {
  waitTasksFinish();
  running_ = false; // break while
  condition_.notify_all();
  for(auto&& t: threads_) {
    if (t.joinable()) {
      t.join();
    }
  }
}

FThreadPool & FThreadPool::getInst() {
  static FThreadPool inst;
  return inst;
}

void FThreadPool::waitTasksFinish() const {
  while (true) {
    if (paused) {
      // atomic value
      if (tasksCnt_ == 0) break;
    }
    else {
      if (tasksRunningCnt() == 0) break;
    }
    // sleep
    std::this_thread::yield();
  }
}

size_t FThreadPool::tasksQueuedCnt() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return tasks_.size();
}

void FThreadPool::worker(const size_t threadId) {
  while (running_) {
    std::function<void(size_t)> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      condition_.wait(lock, [this] {
          return !running_ || !tasks_.empty() && !paused && running_;
      });

      if (tasks_.empty() && !running_) {
        return;
      }

      // pop a task
      task = std::move(tasks_.front());
      tasks_.pop();
    }

    ++runningThreadCount_;
    // handle word
    task(threadId);
    --runningThreadCount_;
    --tasksCnt_;
  }
}