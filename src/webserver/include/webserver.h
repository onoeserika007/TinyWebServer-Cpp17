//
// Created by inory on 10/28/25.
//

#ifndef WEBSERVER_H
#define WEBSERVER_H


#include <string>
#include "http_conn.h"
#include "threadpool.h"

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
    int setNonBlocking(int fd);

    void initLogger();
    void initEpoll();

private:
    int server_fd_;
    int epoll_fd_;
    std::string host_;
    int port_;

    // thread pool
    FThreadPool &thread_pool_ = FThreadPool::getInst();
    // 用于主线程接收<size_t> tasksCnt_{0};
    std::atomic<size_t> runningThreadCount_{0};

    std::queue<std::function<void(size_t)>> tasks_ = {};
    std::atomic<size_t> tasksCnt_{0};

    // http connections
    std::vector<std::unique_ptr<HttpConnection>> connections_;
};


#endif // WEBSERVER_H
