//
// Created by inory on 10/28/25.
//

#ifndef WEBSERVER_H
#define WEBSERVER_H


#include <string>
#include <unordered_set>
#include <memory>
#include <vector>

#include "http_conn.h"
#include "threadpool.h"
#include "sub_reactor.h"


class EpollServer {
public:
    EpollServer(const std::string &host, int port, int sub_reactor_count = 4);
    ~EpollServer();

    void eventloop();

private:
    void acceptConnections();

    void initLogger();
    void initEpoll();
    void initRouter();
    void initHttpPreHandlers();
    void initHttpPostHandlers();

    // services
    void initUserService();
    
    // 负载均衡：选择连接数最少的 SubReactor
    SubReactor* selectSubReactor();

private:
    int server_fd_;
    int epoll_fd_;  // MainReactor 的 epoll（只监听 server_fd）
    std::string host_;
    int port_;

    // Sub Reactors
    std::vector<std::unique_ptr<SubReactor>> sub_reactors_;
    std::atomic<size_t> next_sub_reactor_{0};  // Round-Robin 索引

    // Debug
    std::unordered_set<std::string> client_ips_;
};


#endif // WEBSERVER_H
