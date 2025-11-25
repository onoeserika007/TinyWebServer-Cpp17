//
// Created by inory on 10/28/25.
//

#include "webserver.h"

#include <cassert>

#include "serika/basic/logger.h"
#include "serika/basic/config_manager.h"
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
#include "main_reactor.h"
#include "sub_reactor.h"

void EpollServer::initUserService() {
    // 初始化用户服务
    if (!UserService::Instance().init()) {
        LOG_ERROR("[EpollServer] Failed to initialize user service");
        throw std::runtime_error("Failed to initialize user service");
    }
    LOG_INFO("[EpollServer] User service initialized");
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

EpollServer::EpollServer(const std::string &host, int port, int main_reactor_count, int sub_reactor_count)
    : host_(host), port_(port) {

    auto& config_manager = ConfigManager::Instance();
    // Init config
    HttpConnection::set_use_sendfile(config_manager.get<bool>("server.use_sendfile", true));

    initHttpPreHandlers();
    initHttpPostHandlers();
    initRouter();

    initUserService();  // 初始化用户服务

    // 创建 SubReactors
    LOG_INFO("[EpollServer] Creating {} SubReactors", sub_reactor_count);
    for (int i = 0; i < sub_reactor_count; ++i) {
        sub_reactors_.push_back(std::make_unique<SubReactor>(i));
        sub_reactors_.back()->start();
    }
    LOG_INFO("[EpollServer] All SubReactors started");

    // 创建 MainReactors
    LOG_INFO("[EpollServer] Creating {} MainReactors", main_reactor_count);
    for (int i = 0; i < main_reactor_count; ++i) {
        main_reactors_.push_back(std::make_unique<MainReactor>(this, host, port, i));
    }

    // 启动额外的 MainReactors (除了主线程要运行的那个)
    if (main_reactor_count > 1) {
        for (int i = 1; i < main_reactor_count; ++i) {
            main_reactors_[i]->start();
        }
        LOG_INFO("[EpollServer] Started {} additional MainReactors", main_reactor_count - 1);
    }

    // 主线程将运行第一个 MainReactor (index=0)
    LOG_INFO("[EpollServer] Main thread will run MainReactor-0");
}

EpollServer::~EpollServer() {
    // 停止所有 MainReactors
    LOG_INFO("[EpollServer] Stopping all MainReactors");
    for (auto &reactor: main_reactors_) {
        reactor->stop();
    }
    main_reactors_.clear();

    // 停止所有 SubReactors
    LOG_INFO("[EpollServer] Stopping all SubReactors");
    for (auto &reactor: sub_reactors_) {
        reactor->stop();
    }
    sub_reactors_.clear();

    LOG_INFO("[EpollServer] Shutdown complete");
}

// 负载均衡：选择连接数最少的 SubReactor
SubReactor* EpollServer::selectSubReactor(int fd) {
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

    // Hash by Fd
    // size_t index = fd % sub_reactors_.size();
    // return sub_reactors_[index].get();
}

void EpollServer::run() {
    if (main_reactors_.empty()) {
        LOG_ERROR("[EpollServer] No MainReactor to run");
        return;
    }

    LOG_INFO("[EpollServer] Main thread starting MainReactor-0 event loop");
    main_reactors_[0]->running_ = true;
    main_reactors_[0]->stopped_ = false;
    main_reactors_[0]->run();

    // 当主线程的 MainReactor 退出后，停止所有其他 Reactors
    LOG_INFO("[EpollServer] MainReactor-0 stopped, initiating shutdown");

    // 停止其他 MainReactors
    for (size_t i = 1; i < main_reactors_.size(); ++i) {
        if (!main_reactors_[i]->stopped_) {
            main_reactors_[i]->stop();
        }
    }

    // 停止所有 SubReactors
    for (auto& reactor : sub_reactors_) {
        if (!reactor->stopped()) {
            reactor->stop();
        }
    }
}
