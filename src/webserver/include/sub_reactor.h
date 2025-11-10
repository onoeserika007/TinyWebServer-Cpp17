//
// Created by GitHub Copilot
//

#ifndef SUB_REACTOR_H
#define SUB_REACTOR_H

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <sys/epoll.h>

#include "http_conn.h"
#include "time_wheel.h"

class SubReactor {
public:
    SubReactor(int id);
    ~SubReactor();

    void start();
    
    void stop();
    
    // 添加新连接（由 MainReactor 调用）
    void addConnection(int client_fd, sockaddr_in client_addr);

    // 获取当前连接数（用于负载均衡）
    size_t getConnectionCount() const { return connection_count_.load(); }

private:
    void eventLoop();
    
    void handleRead(int fd);
    
    void handleWrite(int fd);
    
    void handleNewConnection();

private:
    int id_;            // SubReactor ID
    int epoll_fd_;      // 独立的 epoll 实例
    int wakeup_fd_;     // eventfd，用于唤醒 epoll_wait
    
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> thread_;
    
    // 连接管理
    static constexpr int MAX_FD = 65536;
    std::vector<std::unique_ptr<HttpConnection>> connections_;
    std::atomic<size_t> connection_count_{0};
    
    // 定时器管理（每个 SubReactor 独立的 TimerWheel - 无锁！）
    TimerWheel timer_wheel_;
    std::unordered_map<int, std::shared_ptr<TimerWheel::Timer>> timer_handles_;
    
    // 待添加的新连接队列
    struct PendingConnection {
        int fd;
        sockaddr_in addr;
    };
    std::queue<PendingConnection> pending_connections_;
    std::mutex pending_mutex_;  // 保护 pending_connections_

    // config
    bool use_thread_pool_ {false};
};

#endif // SUB_REACTOR_H
