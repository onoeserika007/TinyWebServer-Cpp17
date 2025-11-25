//
// Created by inory on 10/28/25.
//

#include <arpa/inet.h>
#include <signal.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>


#include "fiber.h"
#include "io_fiber.h"
#include "scheduler.h"
#include "serika/basic/config_manager.h"
#include "serika/basic/logger.h"
#include "user_service.h"
#include "webserver/controller/static_file_controller.h"
#include "webserver/http_request.h"
#include "webserver/http_response.h"
#include "webserver/http_router.h"
#include "webserver/sync/http_conn_sync.h"
#include "webserver/webserver.h"

void EpollServer::initLogger() { LOG_INFO("[EpollServer] - Log Init {:d}", 114514); }

void EpollServer::initUserService() {
    // 初始化用户服务
    if (!UserService::Instance().init()) {
        LOG_ERROR("[EpollServer] Failed to initialize user service");
        throw std::runtime_error("Failed to initialize user service");
    }
    LOG_INFO("[EpollServer] User service initialized");
}

int EpollServer::createListenSocket(const std::string& host, uint16_t port) {

    // 创建 server socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        throw std::runtime_error("Failed to create server socket");
    }

    // 设置端口复用
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throw std::runtime_error("setsockopt SO_REUSEADDR failed");
    }

    // 设置 socket 地址信息
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(host.c_str());

    // 绑定服务器地址
    if (bind(sock, (sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        throw std::runtime_error(std::string {"Failed to bind server socket, "} + strerror(errno));
    }

    // 开始监听
    if (listen(sock, 10) == -1) {
        throw std::runtime_error("Failed to listen on server socket");
    }

    return sock;
}

void EpollServer::initRouter() {
    auto &router = HttpRouter::instance();
    router.RegisterRoutes();
}

void EpollServer::initHttpPreHandlers() {
    HttpConnectionSync::add_pre_handler([](const HttpRequest &req, HttpResponse &resp) {
        bool keep_alive =
                (req.version() == "HTTP/1.1" && req.keep_alive()) || (req.version() == "HTTP/1.0" && req.keep_alive());

        resp.set_keep_alive(keep_alive);
    });

    HttpConnectionSync::add_pre_handler([](const HttpRequest &req, HttpResponse &resp) {
        if (req.host().empty()) {
            resp.set_status(HttpStatus::BAD_REQUEST);
            resp.set_body("Host header is required");
        }
    });
}

void EpollServer::initHttpPostHandlers() {
    HttpConnectionSync::add_post_handler([](const HttpRequest &req, HttpResponse &resp) {
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

EpollServer::EpollServer(const std::string &host, int port, int sub_reactor_count) : host_(host), port_(port) {

    auto &config_manager = ConfigManager::Instance();
    // Init config
    HttpConnectionSync::set_use_sendfile(config_manager.get<bool>("server.use_sendfile", true));

    // connections
    connections_.resize(MAX_FD);

    signal(SIGPIPE, SIG_IGN);

    initHttpPreHandlers();
    initHttpPostHandlers();
    initRouter();

    initLogger();
    initUserService(); // 初始化用户服务
}

EpollServer::~EpollServer() {
    // 停止所有 SubReactors

    close(listen_fd_);
    close(epoll_fd_);
    LOG_INFO("[EpollServer] Shutdown complete");
}

void EpollServer::start() {
    // 创建 server socket
    listen_fd_ = createListenSocket(host_, port_);
    running_ = true;

    LOG_INFO("[EpollServer:start] Server start, running");

    // fiber::Fiber::go([server = shared_from_this()]() { server->acceptLoop(); });
}

void EpollServer::shutdown() {
    running_ = false;
}

// public
void EpollServer::acceptLoop() {

    LOG_INFO("[EpollServer:acceptLoop] start loop");
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        auto result = fiber::IO::accept(listen_fd_, (sockaddr*)&client_addr, &addr_len);

        if (!result) {
            // accept失败，可能是因为shutdown关闭了socket
            if (!running_) {
                LOG_INFO("EpollServer: accept loop terminated");
                break;
            }
            LOG_ERROR("EpollServer: accept failed");
            continue;
        }

        int client_fd = result.value();
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }

            if (errno == EMFILE) {
                LOG_ERROR("[EpollServer] No more fd available: errno={}, error:{}", errno, strerror(errno));
                continue;
            }

            LOG_ERROR("[EpollServer] accept failed: errno={}, error:{}", errno, strerror(errno));
            continue;
        }

        // 记录客户端 IP（调试用）
        {
            auto client_ip = std::string(inet_ntoa(client_addr.sin_addr));
            if (!client_ips_.contains(client_ip)) {
                client_ips_.insert(client_ip);
                LOG_INFO("[EpollServer] New client IP: {}", client_ip);
            }
        }

        // 创建 HttpConnectionSync
        if (!connections_[client_fd]) {
            connections_[client_fd] = std::make_unique<HttpConnectionSync>();
        }

        connections_[client_fd]->Init(client_fd, client_addr);

        // 定时器资源只在一个连接上下文中进行管理
        fiber::Fiber::go([server = shared_from_this(), fd = client_fd]() {
            auto& timer_mgr = fiber::Scheduler::getThreadLocalTimerManager();
            auto timer = timer_mgr.addTimer(15000, [server, fd]() {
                LOG_DEBUG("[EpollServer] Timer timeout fd:{}", fd);
                if (server->connections_[fd]) {
                    server->connections_[fd]->Stop();
                }
            });

            server->connections_[fd]->ReceiveLoop([&](const bool should_close) {

                if (should_close) {
                    server->connections_[fd]->Stop();
                    if (timer) {
                        timer_mgr.cancel(timer);
                    }
                } else {
                    // 仅仅刷新资源
                    server->connections_[fd]->Init();
                    if (timer) {
                        timer = timer_mgr.refresh(timer);
                    }
                }
            });

            // ReceiveLoop exit by destroy internal
            server->connections_[fd]->Destroy();
            // LOG_INFO("Connection on fd:{} destroyed.", fd);
        });
    }

    LOG_INFO("[EpollServer] Event loop stopped");
}

// handleRead 和 handleWrite 已移至 SubReactor，MainReactor 不再需要
