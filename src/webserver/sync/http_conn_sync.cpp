//
// Created by inory on 11/21/25.
//

#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstring>
#include <string>


#include "epoll_util.h"
#include "io_fiber.h"
#include "serika/basic/logger.h"
#include "webserver/http_parser.h"
#include "webserver/http_request.h"
#include "webserver/http_response.h"
#include "webserver/http_router.h"
#include "webserver/sync/http_conn_sync.h"

bool HttpConnectionSync::use_sendfile_ = false;

void HttpConnectionSync::Init(int fd, sockaddr_in client_addr) {

    conn_fd_ = fd;
    client_addr_ = client_addr;
    use_edge_trig_ = true;

    Init();
}

void HttpConnectionSync::Init() {
    read_buffer_.reset();
    write_buffer_.reset();
    parser_.reset();
    response_.reset();
    closing_ = false;
    running_ = true;
}

void HttpConnectionSync::Destroy() {
    if (conn_fd_ == -1) {
        return;
    }

    int fd = conn_fd_;
    conn_fd_ = -1;

    // 先Init，规避后续复用的Init和Destroy Init的数据竞态
    Init();

    // 直接关闭（让内核处理 FIN）
    // 如果还有未发送的数据，内核会先发送完再发送 FIN
    // 最后Close
    if (fiber::IO::close(fd) < 0) {
        LOG_ERROR("Closing error: {}, fd:{}", strerror(errno), fd);
    } else {
        LOG_DEBUG("Close success, fd:{}", fd);
    }
}

void HttpConnectionSync::Stop() {
    running_ = false;
}

HttpStatus to_http_status(ParseResult result) {
    switch (result) {
        case ParseResult::OK:
            return HttpStatus::OK;
        case ParseResult::INCOMPLETE:
            return HttpStatus::OK; // 不应走到这里
        case ParseResult::BAD_REQUEST:
            return HttpStatus::BAD_REQUEST;
        case ParseResult::FORBIDDEN:
            return HttpStatus::FORBIDDEN;
        case ParseResult::NOT_FOUND:
            return HttpStatus::NOT_FOUND;
        case ParseResult::INTERNAL_ERROR:
            return HttpStatus::INTERNAL_ERROR;
    }
    return HttpStatus::INTERNAL_ERROR;
}

void HttpConnectionSync::BeginGracefulClose() {
    // 关闭写端，发送 FIN 给客户端
    fiber::IO::shutdown(conn_fd_, SHUT_WR);

    // 标记为正在关闭
    closing_ = true;
}

bool HttpConnectionSync::PreHandlersCheck(const HttpRequest &request, HttpResponse &response_) {
    // --- Pre-processing ---
    for (const auto &handler: pre_handlers_) {
        handler(request, response_);
        if (response_.is_error()) {
            return false;
        }
    }
    return true;
}

bool HttpConnectionSync::PostHandlersCheck(const HttpRequest &request, HttpResponse &response_) {
    // --- Post-processing ---
    for (const auto &handler: post_handlers_) {
        handler(request, response_);
    }
    return true;
}

void HttpConnectionSync::ReceiveLoop(MessageCallback callback) {
    while (running_) {
        // read message
        bool read_success = read_buffer_.read_from(conn_fd_, use_edge_trig_, closing_);

        if (!read_success) {
            // should close
            // LOG_INFO("Received fin from client, gracefully closing done.");
            callback(true);
            continue;
        }

        if (!ProcessHttp()) {
            continue;
        }

        // send message
        try {
            write_buffer_.write_to(conn_fd_);
            if (!write_buffer_.should_close()) {
                callback(false);
            } else {
                // LOG_INFO("Start gracefully closing, sending FIN");
                BeginGracefulClose();
                // 试试主动关闭呢
                // callback(true);
            }

        } catch (const std::exception &e) {
            LOG_ERROR("[HttpConnectionSync::ReceiveLoop] {}", e.what());
            callback(true);
        }
    }
}

bool HttpConnectionSync::ProcessHttp() {

    HttpRequest request;
    ParseResult parse_result = parser_.parse({read_buffer_.data(), read_buffer_.readable_bytes()}, request);
    LOG_DEBUG("[HttpConnectionSync] Processing request uri:{}, fd:{}, parse_res: {}", request.uri(), conn_fd_,
          static_cast<int>(parse_result));

    if (parse_result == ParseResult::INCOMPLETE) {
        // 继续等待
        LOG_DEBUG("Conn fd:{} Incomplete, keep waiting.", conn_fd_);
    }

    switch (parse_result) {
        case ParseResult::OK:
            read_buffer_.retrieve(parser_.consumed_bytes()); // 移动读指针
            response_.reset();

            if (!PreHandlersCheck(request, response_)) {
                LOG_DEBUG("Conn fd:{} Pre-handle Check not Passed.", conn_fd_);
                break;
            }

            // handle
            if (!response_.is_handled()) { // 没被拦截
                HttpRouter::instance().match(request, response_); // 处理业务逻辑
                LOG_DEBUG("Conn req fd:{} handled.", conn_fd_);
            } else {
                LOG_DEBUG("Conn fd:{} Intercepted.", conn_fd_);
            }

            PostHandlersCheck(request, response_);
            break;
        case ParseResult::INCOMPLETE:
            // 继续等待
            LOG_DEBUG("Conn fd:{} Incomplete, keep waiting.", conn_fd_);
            return false;
        default:
            // 统一错误处理
            response_.reset();
            response_.set_error_page(to_http_status(parse_result));
            LOG_DEBUG("Conn fd:{} Set Error Page.", conn_fd_);
            break;
    }

    response_.finalize(); // 准备 header + file

    // 根据 use_sendfile_ 选择发送方式
    if (use_sendfile_ && response_.has_file()) {
        make_response_sendfile();
    } else {
        make_response_mmap();
    }

    if (!response_.keep_alive()) {
        write_buffer_.set_close_on_done(true);
    }

    // parse success
    return true;
}

void HttpConnectionSync::make_response_mmap() {
    write_buffer_.set_response_with_mmap(response_.response_data(), response_.response_length(), response_.file_path(),
                                         response_.file_start(), response_.file_size());
}

void HttpConnectionSync::make_response_sendfile() {
    write_buffer_.set_response_with_sendfile(response_.response_data(), response_.response_length(),
                                             response_.file_path(), response_.file_start(), response_.file_size());
}
