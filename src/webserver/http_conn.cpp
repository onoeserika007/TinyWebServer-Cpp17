//
// Created by inory on 10/29/25.
//

#include "http_conn.h"
#include "epoll_util.h"
#include "http_parser.h"
#include "http_request.h"
#include "http_controller.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>
#include <iostream>
#include <netinet/in.h>

#include <cstring>
#include <string>

#include "http_response.h"
#include "http_router.h"
#include "logger.h"

void HttpConnection::Init(int fd, int epoll_fd, sockaddr_in client_addr) {

    conn_fd_ = fd;
    epoll_fd_ = epoll_fd;
    client_addr_ = client_addr;

    use_edge_trig_ = true;

    // 设置客户端 socket 为非阻塞
    EpollUtil::setNonBlocking(conn_fd_);

    // 将新的客户端连接注册到 epoll 中
    EpollUtil::addFdRead(epoll_fd_, conn_fd_, true, use_edge_trig_);

    Init();
}

void HttpConnection::Init() {
    read_buffer_.clear();
    parser_.reset();
    response_.reset(); // 确保HttpResponse也被正确初始化
}

void HttpConnection::Destroy() {
    EpollUtil::removeFd(epoll_fd_, conn_fd_);
    conn_fd_ = -1;
}

HttpStatus to_http_status(ParseResult result) {
    switch (result) {
        case ParseResult::OK:             return HttpStatus::OK;
        case ParseResult::INCOMPLETE:     return HttpStatus::OK; // 不应走到这里
        case ParseResult::BAD_REQUEST:    return HttpStatus::BAD_REQUEST;
        case ParseResult::FORBIDDEN:      return HttpStatus::FORBIDDEN;
        case ParseResult::NOT_FOUND:      return HttpStatus::NOT_FOUND;
        case ParseResult::INTERNAL_ERROR: return HttpStatus::INTERNAL_ERROR;
    }
    return HttpStatus::INTERNAL_ERROR;
}

bool HttpConnection::ReadOnce() {
    // if need more read
    if (!read_buffer_.read_from(conn_fd_, use_edge_trig_)) {
        // read buffer overflow
        Destroy();
        return false;
    }
    return true;
}

bool HttpConnection::WriteAll() {
    WriteResult result = write_buffer_.write_to(conn_fd_);

    switch (result) {
        case WriteResult::SUCCESS:
            write_buffer_.unmap_if_needed();

            // according to request parsing result
            if (!write_buffer_.should_close()) {
                write_buffer_.reset();
                Init();
                EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLIN, use_edge_trig_);
                return true; // 不销毁连接
            } else {
                return false; // 销毁连接
            }

        case WriteResult::CONTINUE:
            // 需要继续写，重新注册 EPOLLOUT（尤其 ONESHOT 必须这么做）
            EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLOUT, use_edge_trig_);
            return true; // 不销毁连接

        case WriteResult::ERROR:
            write_buffer_.unmap_if_needed();
            return false; // 销毁连接
    }

    // dummy
    return false;
}

bool HttpConnection::PreHandlersCheck(const HttpRequest& request, HttpResponse& response_) {
    // --- Pre-processing ---
    for (const auto& handler : pre_handlers_) {
        handler(request, response_);
        if (response_.is_error()) {
            return false;
        }
    }
    return true;
}

bool HttpConnection::PostHandlersCheck(const HttpRequest& request, HttpResponse& response_) {
    // --- Post-processing ---
    for (const auto& handler : post_handlers_) {
        handler(request, response_);
    }
    return true;
}

void HttpConnection::ProcessHttp() {

    HttpRequest request;
    ParseResult parse_result = parser_.parse({read_buffer_.data(), read_buffer_.readable_bytes()}, request);
    LOG_INFO("[HttpConnection] Processing request uri:{}", request.uri());
    response_.reset();

    switch (parse_result) {
        case ParseResult::OK:
            read_buffer_.retrieve(parser_.consumed_bytes()); // 移动读指针

            if (!PreHandlersCheck(request, response_)) {
                break;
            }

            // handle
            if (!response_.is_handled()) { // 没被拦截
                HttpRouter::instance().match(request, response_); // 处理业务逻辑
            }

            PostHandlersCheck(request, response_);
            break;
        case ParseResult::INCOMPLETE:
            // 继续等待
            EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLIN, use_edge_trig_);
            return;
        default:
            // 统一错误处理
            response_.set_error_page(to_http_status(parse_result));
            break;
    }

    response_.finalize(); // 准备 header + file

    write_buffer_.set_response(response_.response_data(), response_.response_length(), response_.file_address(),
                               response_.file_size());

    if (!response_.keep_alive()) {
        write_buffer_.set_close_on_done(true);
    }

    EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLOUT, use_edge_trig_);
}
