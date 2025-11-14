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

EpollServer::EpollServer(const std::string &host, int port, int sub_reactor_count) 
    : host_(host), port_(port) {

    auto& config_manager = ConfigManager::Instance();
    // Init config
    HttpConnection::set_use_sendfile(config_manager.get<bool>("server.use_sendfile", true));

    initHttpPreHandlers();
    initHttpPostHandlers();
    initRouter();

    initLogger();
    initUserService();  // 初始化用户服务
    initEpoll();
    
    // 创建 SubReactors
    LOG_INFO("[MainReactor] Creating {} SubReactors", sub_reactor_count);
    for (int i = 0; i < sub_reactor_count; ++i) {
        sub_reactors_.push_back(std::make_unique<SubReactor>(i));
        sub_reactors_.back()->start();
    }
    LOG_INFO("[MainReactor] All SubReactors started");
}

EpollServer::~EpollServer() {
    // 停止所有 SubReactors
    LOG_INFO("[MainReactor] Stopping all SubReactors");
    for (auto& sub_reactor : sub_reactors_) {
        sub_reactor->stop();
    }
    sub_reactors_.clear();
    
    close(server_fd_);
    close(epoll_fd_);
    LOG_INFO("[MainReactor] Shutdown complete");
}

// 负载均衡：选择连接数最少的 SubReactor
SubReactor* EpollServer::selectSubReactor() {
    // Round-Robin（轮询）策略
    size_t index = next_sub_reactor_.fetch_add(1) % sub_reactors_.size();
    return sub_reactors_[index].get();
    
    // 或者使用最少连接数策略（需要更多同步开销）
    // SubReactor* selected = sub_reactors_[0].get();
    // size_t min_conn = selected->getConnectionCount();
    // for (auto& reactor : sub_reactors_) {
    //     size_t conn = reactor->getConnectionCount();
    //     if (conn < min_conn) {
    //         min_conn = conn;
    //         selected = reactor.get();
    //     }
    // }
    // return selected;
}

// public
void EpollServer::eventloop() {
    std::vector<epoll_event> events(64);  // MainReactor 只监听 server_fd，不需要太多
    
    LOG_INFO("[MainReactor] Event loop started");

    while (true) {
        int timeout = timer_wheel_.nextTimeoutMs();
        int num_events = epoll_wait(epoll_fd_, events.data(), events.size(), timeout);
        if (num_events < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR("[MainReactor] epoll_wait failed: {}", strerror(errno));
            break;
        }

        // MainReactor 只处理 accept
        for (int i = 0; i < num_events; ++i) {
            int fd = events[i].data.fd;

            if (fd == server_fd_) {
                acceptConnections();
            }
        }
        
        // MainReactor 的独立定时器 tick（当前无定时任务，但保持架构一致）
        timer_wheel_.tick();
    }
    
    LOG_INFO("[MainReactor] Event loop stopped");
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
                LOG_ERROR("[MainReactor] No more fd available: errno={}, error:{}", errno, strerror(errno));
                break;
            }

            LOG_ERROR("[MainReactor] accept failed: errno={}, error:{}", errno, strerror(errno));
            return;
        }

        // 记录客户端 IP（调试用）
        {
            auto client_ip = std::string(inet_ntoa(client_addr.sin_addr));
            if (!client_ips_.contains(client_ip)) {
                client_ips_.insert(client_ip);
                LOG_INFO("[MainReactor] New client IP: {}", client_ip);
            }
        }
        
        // 选择一个 SubReactor 并分发连接
        SubReactor* reactor = selectSubReactor();
        reactor->addConnection(client_fd, client_addr);
        
        LOG_DEBUG("[MainReactor] Accepted fd:{}, dispatched to SubReactor", client_fd);
    }
}

// handleRead 和 handleWrite 已移至 SubReactor，MainReactor 不再需要