//
// Created by GitHub Copilot
//

#include "sub_reactor.h"
#include "epoll_util.h"
#include "logger.h"
#include "threadpool.h"

#include <arpa/inet.h>
#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>

#include "config_manager.h"

SubReactor::SubReactor(int id) : id_(id) {
    
    connections_.resize(MAX_FD);
    timer_handles_.reserve(10000);
    use_thread_pool_ = ConfigManager::Instance().get<bool>("server.use_thread_pool", true);
    
    // 创建独立的 epoll 实例
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        LOG_ERROR("[SubReactor {}] Failed to create epoll: {}", id_, strerror(errno));
        throw std::runtime_error("Failed to create epoll");
    }
    
    // 创建 eventfd 用于唤醒
    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeup_fd_ < 0) {
        LOG_ERROR("[SubReactor {}] Failed to create eventfd: {}", id_, strerror(errno));
        close(epoll_fd_);
        throw std::runtime_error("Failed to create eventfd");
    }
    
    // 将 eventfd 注册到 epoll
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = wakeup_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_fd_, &ev) < 0) {
        LOG_ERROR("[SubReactor {}] Failed to add eventfd to epoll: {}", id_, strerror(errno));
        close(epoll_fd_);
        close(wakeup_fd_);
        throw std::runtime_error("Failed to add eventfd");
    }
    
    LOG_DEBUG("[SubReactor {}] Initialized with epoll_fd={}, wakeup_fd={}",
             id_, epoll_fd_, wakeup_fd_);
}

SubReactor::~SubReactor() {
    stop();
    
    // 清理所有定时器，防止回调访问已销毁的成员变量
    for (auto& [fd, timer] : timer_handles_) {
        timer_wheel_.cancel(timer);
    }
    timer_handles_.clear();
    
    // 关闭所有连接
    for (auto& conn : connections_) {
        if (conn) {
            conn->Destroy();
        }
    }
    connections_.clear();
    
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
    if (wakeup_fd_ >= 0) {
        close(wakeup_fd_);
    }
    
    LOG_DEBUG("[SubReactor {}] Destroyed", id_);
}

void SubReactor::start() {
    if (running_.load()) {
        LOG_WARN("[SubReactor {}] Already running", id_);
        return;
    }
    
    running_.store(true);
    thread_ = std::make_unique<std::thread>(&SubReactor::eventLoop, this);
    LOG_DEBUG("[SubReactor {}] Started", id_);
}

void SubReactor::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // 唤醒 epoll_wait
    uint64_t val = 1;
    write(wakeup_fd_, &val, sizeof(val));
    
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    
    LOG_DEBUG("[SubReactor {}] Stopped", id_);
}

void SubReactor::addConnection(int client_fd, sockaddr_in client_addr) {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_connections_.push({client_fd, client_addr});
    }
    
    // 唤醒 epoll_wait
    uint64_t val = 1;
    ssize_t n = write(wakeup_fd_, &val, sizeof(val));
    if (n != sizeof(val)) {
        LOG_ERROR("[SubReactor {}] Failed to wake up: {}", id_, strerror(errno));
    }
}

void SubReactor::handleNewConnection() {
    // 读取 eventfd（清空计数）
    uint64_t val;
    read(wakeup_fd_, &val, sizeof(val));
    
    // 处理所有待添加的连接
    std::queue<PendingConnection> local_queue;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        local_queue.swap(pending_connections_);
    }
    
    while (!local_queue.empty()) {
        auto [client_fd, client_addr] = local_queue.front();
        local_queue.pop();

    }
}

void SubReactor::handleRead(int fd) {
    if (!connections_[fd]) {
        return;
    }
    
    auto timer = timer_handles_[fd];
    auto& http_conn = connections_[fd];
    
    if (http_conn->ReadOnce()) {
        if (use_thread_pool_) {
            FThreadPool::getInst().pushTask([this, fd = fd]() {
                if (connections_[fd]) {
                    connections_[fd]->ProcessHttp();
                }
            });

            if (timer) {
                timer_wheel_.refresh(timer);
            }
        } else {
            // 优化：直接在当前线程处理 HTTP 请求，避免线程池切换开销
            http_conn->ProcessHttp();

            // 处理完后立即尝试写入响应
            if (http_conn->WriteOnce()) {
                // 写入未完成，继续保持连接
                if (timer) {
                    timer_wheel_.refresh(timer);
                }
            } else {
                // 写完成或失败，关闭连接
                if (timer) {
                    timer_wheel_.cancel(timer);
                    timer_handles_.erase(fd);
                }
                connections_[fd]->Destroy();
                connections_[fd].reset();
                connection_count_.fetch_sub(1);
            }
        }

    } else {
        // 读取失败，关闭连接并释放内存
        if (timer) {
            timer_wheel_.cancel(timer);
            timer_handles_.erase(fd);
        }
        connections_[fd]->Destroy();
        connections_[fd].reset();  // 释放 HttpConnection 内存
        connection_count_.fetch_sub(1);
    }
}

void SubReactor::handleWrite(int fd) {
    if (!connections_[fd]) {
        return;
    }
    
    auto& http_conn = connections_[fd];
    auto timer = timer_handles_[fd];
    
    LOG_DEBUG("[SubReactor {}] Handle write fd:{}", id_, fd);
    if (http_conn->WriteOnce()) {
        // 继续保持连接
        if (timer) {
            timer_wheel_.refresh(timer);
        }
    } else {
        // 写完成或失败，关闭连接并释放内存
        if (timer) {
            timer_wheel_.cancel(timer);
            timer_handles_.erase(fd);
        }
        connections_[fd]->Destroy();
        connections_[fd].reset();  // 释放 HttpConnection 内存
        connection_count_.fetch_sub(1);
    }
}

void SubReactor::eventLoop() {
    LOG_DEBUG("[SubReactor {}] Event loop started", id_);
    
    std::vector<epoll_event> events(1024);
    
    while (running_.load()) {
        // 使用 timer_wheel_ 计算超时时间，避免长时间阻塞
        int timeout = timer_wheel_.nextTimeoutMs();
        int num_events = epoll_wait(epoll_fd_, events.data(), events.size(), timeout);
        
        if (num_events < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR("[SubReactor {}] epoll_wait failed: {}", id_, strerror(errno));
            break;
        }
        
        // 处理事件
        for (int i = 0; i < num_events; ++i) {
            int fd = events[i].data.fd;
            
            if (fd == wakeup_fd_) {
                // 新连接通知
                handleNewConnection();
            } else if (events[i].events & EPOLLIN) {
                handleRead(fd);
            } else if (events[i].events & EPOLLOUT) {
                handleWrite(fd);
            }
        }
        
        // 定时器 tick（每个 SubReactor 独立管理，无锁！）
        timer_wheel_.tick();
    }
    
    LOG_DEBUG("[SubReactor {}] Event loop stopped", id_);
}
