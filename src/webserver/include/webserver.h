//
// Created by inory on 10/28/25.
//

#ifndef WEBSERVER_H
#define WEBSERVER_H


#include <string>
#include <unordered_set>

#include "http_conn.h"
#include "threadpool.h"
#include "time_wheel.h"


class TimerWheel;
constexpr const int MAX_FD = 65536;

class EpollServer {
public:
    EpollServer(const std::string &host, int port);
    ~EpollServer();

    void eventloop();

private:
    void acceptConnections();
    void handleRead(int fd);
    void handleWrite(int fd);

    void initLogger();
    void initEpoll();
    void initRouter();
    void initHttpPreHandlers();
    void initHttpPostHandlers();

    // services
    void initUserService();

private:
    int server_fd_;
    int epoll_fd_;
    std::string host_;
    int port_;

    // Config
    bool use_reactor_ {true};

    // thread pool
    FThreadPool &thread_pool_ = FThreadPool::getInst();

    // timer
    TimerWheel &timer_manager_ = TimerWheel::getInst();
    std::unordered_map<int, std::shared_ptr<TimerWheel::Timer>> timer_handles_;

    // http connections
    std::vector<std::unique_ptr<HttpConnection>> connections_;

    // Debug
    std::unordered_set<std::string> client_ips_;
};


#endif // WEBSERVER_H
