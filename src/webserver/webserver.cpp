//
// Created by inory on 10/28/25.
//

#include "webserver.h"
#include "logger.h"

#include <stdexcept>
#include <sys/epoll.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>

int EpollServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error("fcntl failed");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("fcntl failed to set O_NONBLOCK");
    }
    return flags;
}

void EpollServer::initLogger() {
    Logger::Instance().Init("webserver", true, 10000, 8192, 10 * 1024 * 1024, 0);
    LOG_INFO("[EpollServer] - Log Init.");
}

void EpollServer::initEpoll() {
    // 创建 server socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        throw std::runtime_error("Failed to create server socket");
    }

    // 设置端口复用
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throw std::runtime_error("setsockopt SO_REUSEADDR failed");
    }

    // 设置 socket 地址信息
    sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = inet_addr(host_.c_str());

    // 绑定服务器地址
    if (bind(server_fd_, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        throw std::runtime_error("Failed to bind server socket");
    }

    // 开始监听
    if (listen(server_fd_, 10) == -1) {
        throw std::runtime_error("Failed to listen on server socket");
    }

    // 设置 server socket 为非阻塞
    setNonBlocking(server_fd_);

    // 创建 epoll 实例
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        throw std::runtime_error("Failed to create epoll instance");
    }

    // 将 server socket 注册到 epoll 中
    epoll_event ev {};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &ev) == -1) {
        throw std::runtime_error("Failed to add server socket to epoll");
    }
}

EpollServer::EpollServer(const std::string& host, int port)
    : host_(host), port_(port) {


    initLogger();
    initEpoll();
}

EpollServer::~EpollServer() {
    close(server_fd_);
    close(epoll_fd_);
}

void EpollServer::start() {
    std::vector<epoll_event> events(10);  // 用于保存 epoll 返回的事件

    while (true) {
        int num_events = epoll_wait(epoll_fd_, events.data(), events.size(), -1);
        if (num_events == -1) {
            if (errno == EINTR) {
                continue;  // 被中断，忽略
            } else {
                std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
                break;
            }
        }

        for (int i = 0; i < num_events; ++i) {
            int fd = events[i].data.fd;

            if (fd == server_fd_) {
                // 处理新的连接
                acceptConnections();
            } else if (events[i].events & EPOLLIN) {
                // 处理读取事件
                handleRead(fd);
            } else if (events[i].events & EPOLLOUT) {
                // 处理写事件
                handleWrite(fd);
            }
        }
    }
}

void EpollServer::acceptConnections() {
    sockaddr_in client_addr {};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd_, (sockaddr*)&client_addr, &client_len);
    if (client_fd == -1) {
        LOG_ERROR("Failed to accept connection: errno=%d, error:%s", errno, strerror(errno));
        return;
    }

    // 设置客户端 socket 为非阻塞
    setNonBlocking(client_fd);

    // 将新的客户端连接注册到 epoll 中
    epoll_event ev {};
    ev.events = EPOLLIN | EPOLLOUT;  // 关注读写事件
    ev.data.fd = client_fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
        LOG_ERROR("Failed to add client socket to epoll: errno=%d, error: %s", errno, strerror(errno));
        close(client_fd);
    }
}

void EpollServer::handleRead(int fd) {
    char buffer[1024];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
    if (bytes_read == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Read error: errno=%d, error: %s", errno, strerror(errno));
        }
        return;
    } else if (bytes_read == 0) {
        std::cerr << "Client disconnected" << std::endl;
        close(fd);
    } else {
        // std::cout << "Received: " << std::string(buffer, bytes_read) << std::endl;
    }
}

void EpollServer::handleWrite(int fd) {
    const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";

    size_t total = 0;
    while (total < response.size()) {
        ssize_t n = write(fd, response.c_str() + total, response.size() - total);
        if (n > 0) {
            total += n;
        } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 输出缓冲区满，等待下次 EPOLLOUT
            break;
        } else {
            LOG_ERROR("Write error: errno=%d, error: %s", errno, strerror(errno));
            close(fd);
            return;
        }
    }

    close(fd);  // 关闭连接
}
