//
// Created by inory on 10/28/25.
//

#ifndef WEBSERVER_H
#define WEBSERVER_H


#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "time_wheel.h"


class HttpConnectionSync;

namespace fiber {
    struct TimerNode;
}


class EpollServer: public std::enable_shared_from_this<EpollServer> {
public:
    EpollServer(const std::string &host, int port, int sub_reactor_count = 4);
    ~EpollServer();

    void start();
    void shutdown();
    void acceptLoop();

private:
    void initLogger();
    void initRouter();
    void initHttpPreHandlers();
    void initHttpPostHandlers();

    // services
    void initUserService();

private:
    static int createListenSocket(const std::string& host, uint16_t port);

    int listen_fd_;
    int epoll_fd_; // MainReactor 的 epoll（只监听 server_fd）
    std::string host_;
    int port_;
    bool running_;

    // connection pool
    static constexpr int MAX_FD = 65536;
    std::vector<std::unique_ptr<HttpConnectionSync>> connections_;

    // Debug
    std::unordered_set<std::string> client_ips_;
};


#endif // WEBSERVER_H
