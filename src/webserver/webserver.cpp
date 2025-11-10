//
// Created by inory on 10/28/25.
//

#include "webserver.h"

#include <assert.h>

#include "logger.h"
#include "config_manager.h"
#include "user_service.h"

#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <memory>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "epoll_util.h"
#include "http_router.h"
#include "http_controller.h"
#include "http_request.h"
#include "http_response.h"
#include "static_file_controller.h"

void EpollServer::initLogger() {
    Logger::Instance().Init("webserver", true, 10000, 8192, 10 * 1024 * 1024, 0);
    LOG_INFO("[EpollServer] - Log Init {:d}", 114514);
}

void EpollServer::initUserService() {
    // 初始化用户服务
    if (!UserService::Instance().init()) {
        LOG_ERROR("[EpollServer] Failed to initialize user service");
        throw std::runtime_error("Failed to initialize user service");
    }
    LOG_INFO("[EpollServer] User service initialized");
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
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = inet_addr(host_.c_str());

    // 绑定服务器地址
    if (bind(server_fd_, (sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        throw std::runtime_error("Failed to bind server socket");
    }

    // 开始监听
    if (listen(server_fd_, 10) == -1) {
        throw std::runtime_error("Failed to listen on server socket");
    }

    // 设置 server socket 为非阻塞
    EpollUtil::setNonBlocking(server_fd_);

    // 创建 epoll 实例
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        throw std::runtime_error("Failed to create epoll instance");
    }

    // 将 server socket 注册到 epoll 中
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &ev) == -1) {
        throw std::runtime_error("Failed to add server socket to epoll");
    }
}

void EpollServer::initRouter() {
    auto& router = HttpRouter::instance();
    router.RegisterRoutes();
}

void EpollServer::initHttpPreHandlers() {
    HttpConnection::add_pre_handler([](const HttpRequest& req, HttpResponse& resp) {
        bool keep_alive = (req.version() == "HTTP/1.1" && req.keep_alive()) ||
                          (req.version() == "HTTP/1.0" && req.keep_alive());

        resp.set_keep_alive(keep_alive);
    });

    HttpConnection::add_pre_handler([](const HttpRequest& req, HttpResponse& resp) {
        if (req.host().empty()) {
            resp.set_status(HttpStatus::BAD_REQUEST);
            resp.set_body("Host header is required");
        }
    });


}

void EpollServer::initHttpPostHandlers() {
    HttpConnection::add_post_handler([](const HttpRequest& req, HttpResponse& resp) {
        // mmp默认不设置body，这里html会被设置成plain，url上也看不出html特征
        // if (resp.body().starts_with("<!DOCTYPE html") ||
        //     req.uri().ends_with(".html")) {
        //     resp.add_header("Content-Type", "text/html");
        // } else if (req.uri().ends_with(".js")) {
        //     resp.add_header("Content-Type", "application/javascript");
        // } else if (req.uri().ends_with(".css")) {
        //     resp.add_header("Content-Type", "text/css");
        // } else {
        //     resp.add_header("Content-Type", "text/plain");
        // }
    });
}

EpollServer::EpollServer(const std::string &host, int port) : host_(host), port_(port) {

    auto& config_manager = ConfigManager::Instance();
    // Init config
    HttpConnection::set_use_sendfile(config_manager.get<bool>("server.use_sendfile", true));

    // http conns
    connections_.resize(MAX_FD);
    timer_handles_.reserve(10000);  // 预分配 map 空间，减少 rehash
    initHttpPreHandlers();
    initHttpPostHandlers();
    initRouter();

    initLogger();
    initUserService();  // 初始化用户服务
    initEpoll();
}

EpollServer::~EpollServer() {
    close(server_fd_);
    close(epoll_fd_);
}

// public
void EpollServer::eventloop() {
    std::vector<epoll_event> events(1024);  // 增大到 1024，减少系统调用次数
    auto& time_mgr = TimerWheel::getInst();

    while (true) {
        // dynamically update tick_interval
        auto tick_interval = time_mgr.nextTimeoutMs();
        int num_events = epoll_wait(epoll_fd_, events.data(), events.size(), tick_interval);
        if (num_events < 0) {
            if (errno != EINTR) {
                LOG_ERROR("epoll_wait failed: {}", strerror(errno));
            }
            break;
        }

        // handle epoll
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

        // real tick check inside
        time_mgr.tick();
    }
}

void EpollServer::acceptConnections() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (sockaddr *) &client_addr, &client_len);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            if (errno == EMFILE) {
                LOG_ERROR("Failed to accept connection, no more fd is available: errno={:d}, error:{:s}", errno, strerror(errno));
                break;
            }

            LOG_ERROR("Failed to accept connection: errno={:d}, error:{:s}", errno, strerror(errno));
            return;
        }

        // TODO make a connection
        assert(client_fd >= 0 && client_fd < MAX_FD);
        if (!connections_[client_fd]) {
            connections_[client_fd] = std::make_unique<HttpConnection>();
        }

        {
            auto client_ip = std::string(inet_ntoa(client_addr.sin_addr));
            if (!client_ips_.contains(client_ip)) {
                client_ips_.insert(client_ip);
                LOG_INFO("Got new client ip: {}", client_ip);
            }
        }
        LOG_DEBUG("Init new connection fd:{}", client_fd);
        connections_[client_fd]->Init(client_fd, epoll_fd_, client_addr);

        if (timer_handles_.count(client_fd)) {
            timer_manager_.cancel(timer_handles_[client_fd]);
        }

        // destroy connection when time out
        timer_handles_[client_fd] = timer_manager_.addTimer(15, [this, fd = client_fd]() {
            LOG_INFO("Timer cleared fd:{}", fd);
            connections_[fd]->Destroy();
            timer_handles_.erase(fd);
        });
    }
}

void EpollServer::handleRead(int fd) {
    if (!connections_[fd]) {
        return;
    }

    auto timer = timer_handles_[fd];

    auto& http_conn = connections_[fd];

    // if read success
    LOG_DEBUG("[EpollServer] Http conn coming from client: {}", inet_ntoa(http_conn->GetClientAddress().sin_addr));
    if (http_conn->ReadOnce()) {
        // 不要用引用捕获局部变量，比如fd
        thread_pool_.pushTask([this, fd = fd]() {
            connections_[fd]->ProcessHttp();
        });

        if (timer) {
            timer_manager_.refresh(timer);
        }
    } else {
        // TODO callback
        if (timer) {
            timer_manager_.cancel(timer);
            timer_handles_.erase(fd);
        }
        connections_[fd]->Destroy();
    }
}

void EpollServer::handleWrite(int fd) {
    if (!connections_[fd]) {
        return;
    }

    auto& http_conn = connections_[fd];

    auto timer = timer_handles_[fd];

    LOG_DEBUG("[EpollServer] Resp to client: {}", inet_ntoa(http_conn->GetClientAddress().sin_addr));
    // if keep connection
    if (http_conn->WriteOnce()) {
        // Actually no write/read work need to be submitted to thread pool, it's for pure computation
        // TODO timer
        // Keep Alive
        if (timer) {
            timer_manager_.refresh(timer);
        }
    } else {
        // TODO callback, now close connection
        if (timer) {
            timer_manager_.cancel(timer);
            timer_handles_.erase(fd);
        }
        connections_[fd]->Destroy();
    }
}