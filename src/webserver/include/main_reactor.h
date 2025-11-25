//
// Created by inory on 11/25/25.
//

#ifndef MAIN_REACTOR_H
#define MAIN_REACTOR_H

#include <string>
#include <thread>

class EpollServer;

class MainReactor {
public:
    friend EpollServer;
    MainReactor(EpollServer* server, const std::string& host, int port, int index);
    ~MainReactor();
    void start();
    void stop();
    void run();

private:
    void initEpoll();
    void acceptConnections();

    EpollServer* server_;
    std::string host_;
    int port_;
    int index_;
    int server_fd_;
    int epoll_fd_;
    std::thread thread_;
    bool running_;
    std::atomic<bool> stopped_;
};

#endif //MAIN_REACTOR_H
