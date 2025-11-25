//
// Created by inory on 11/25/25.
//

#include "main_reactor.h"
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>

#include "serika/basic/config_manager.h"
#include "serika/basic/logger.h"
#include "epoll_util.h"
#include "webserver.h"
#include "sub_reactor.h"

MainReactor::MainReactor(EpollServer* server, const std::string& host, int port, int index)
    : server_(server), host_(host), port_(port), index_(index),
      running_(false), stopped_(false) {
    initEpoll();
}

MainReactor::~MainReactor() {
    if (running_) {
        stop();
    }
    close(server_fd_);
    close(epoll_fd_);
}

void MainReactor::start() {
    running_ = true;
    stopped_ = false;
    thread_ = std::thread(&MainReactor::run, this);
}

void MainReactor::stop() {
    if (!running_) return;

    running_ = false;
    stopped_ = true;

    // 唤醒 epoll_wait
    int dummy_fd = eventfd(0, EFD_NONBLOCK);
    if (dummy_fd >= 0) {
        uint64_t val = 1;
        write(dummy_fd, &val, sizeof(val));
        close(dummy_fd);
    }

    if (thread_.joinable()) {
        thread_.join();
    }
}

void MainReactor::run() {
    std::vector<epoll_event> events(64);
    LOG_INFO("[MainReactor-{}] Event loop started", index_);

    while (running_) {
        int num_events = epoll_wait(epoll_fd_, events.data(), events.size(), 100); // 100ms timeout
        if (num_events < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("[MainReactor-{}] epoll_wait failed: {}", index_, strerror(errno));
            break;
        }

        for (int i = 0; i < num_events; ++i) {
            int fd = events[i].data.fd;
            if (fd == server_fd_) {
                acceptConnections();
            }
        }
    }

    LOG_INFO("[MainReactor-{}] Event loop stopped", index_);
    stopped_ = true;
}

void MainReactor::initEpoll() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        throw std::runtime_error("Failed to create server socket");
    }

    // 关键修改：启用 SO_REUSEPORT
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throw std::runtime_error("setsockopt SO_REUSEADDR failed");
    }
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        throw std::runtime_error("setsockopt SO_REUSEPORT failed");
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = inet_addr(host_.c_str());

    if (bind(server_fd_, (sockaddr*) &server_addr, sizeof(server_addr)) == -1) {
        throw std::runtime_error("Failed to bind server socket");
    }

    if (listen(server_fd_, 10) == -1) {
        throw std::runtime_error("Failed to listen on server socket");
    }

    EpollUtil::setNonBlocking(server_fd_);

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        throw std::runtime_error("Failed to create epoll instance");
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &ev) == -1) {
        throw std::runtime_error("Failed to add server socket to epoll");
    }
}

void MainReactor::acceptConnections() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (sockaddr*) &client_addr, &client_len);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            if (errno == EMFILE) {
                LOG_ERROR("[MainReactor-{}] No more fd available: errno={}, error:{}",
                          index_, errno, strerror(errno));
                break;
            }

            LOG_ERROR("[MainReactor-{}] accept failed: errno={}, error:{}",
                      index_, errno, strerror(errno));
            return;
        }

        // 选择一个 SubReactor 并分发连接
        SubReactor* reactor = server_->selectSubReactor(client_fd);
        reactor->addConnection(client_fd, client_addr);

        LOG_DEBUG("[MainReactor-{}] Accepted fd:{}, dispatched to SubReactor",
                  index_, client_fd);
    }
}