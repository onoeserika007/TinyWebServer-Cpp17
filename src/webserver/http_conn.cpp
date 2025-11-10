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
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <netinet/in.h>

#include <cstring>
#include <string>

#include "http_response.h"
#include "http_router.h"
#include "logger.h"

bool HttpConnection::use_sendfile_ = false;

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
    write_buffer_.reset();
    parser_.reset();
    response_.reset(); // 确保HttpResponse也被正确初始化
    closing_ = false;
}

void HttpConnection::Destroy() {
    if (conn_fd_ == -1) {
        return;
    }
    
    // 从 epoll 移除
    EpollUtil::removeFd(epoll_fd_, conn_fd_);
    
    // 直接关闭（让内核处理 FIN）
    // 如果还有未发送的数据，内核会先发送完再发送 FIN
    if (close(conn_fd_) < 0) {
        LOG_ERROR("Closing error: {}, fd:{}", strerror(errno), conn_fd_);
    } else {
        LOG_DEBUG("Close success, fd:{}", conn_fd_);
    }
    conn_fd_ = -1;

    // reset buffer resources
    // eg: mmap failed: Cannot allocate memory
    Init();
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
    // if is gracefully closing
    // if (closing_) {
    //     char buf[1];
    //     ssize_t n = recv(conn_fd_, buf, sizeof(buf), MSG_PEEK);
    //     if (n == 0) {
    //         LOG_INFO("Gracefully closing");
    //         return false;
    //     } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    //         // no fin yet
    //         return true;
    //     } else {
    //         // error
    //         return false;
    //     }
    // }
    
    // normal
    if (!read_buffer_.read_from(conn_fd_, use_edge_trig_)) {
        // read buffer overflow
        return false;
    }
    return true;
}

bool HttpConnection::WriteOnce() {
    WriteResult result = write_buffer_.write_to(conn_fd_);

    switch (result) {
        case WriteResult::SUCCESS:
            write_buffer_.reset();
            // according to request parsing result
            if (!write_buffer_.should_close()) {
                Init();
                EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLIN, use_edge_trig_);
                return true; // 不销毁连接
            } else {
                // // 需要关闭连接：开始优雅关闭流程
                // BeginGracefulClose();
                // return true; // 暂时不销毁，等待对端关闭
                if (fcntl(conn_fd_, F_GETFD) < 0) {
                    LOG_ERROR("Fd:{} has been closed before you do!, err:{}", conn_fd_, strerror(errno));
                }
                LOG_DEBUG("Close for demand, fd:{}", conn_fd_);
                return false;
            }

        case WriteResult::CONTINUE:
            // 需要继续写，重新注册 EPOLLOUT（尤其 ONESHOT 必须这么做）
            EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLOUT, use_edge_trig_);
            return true; // 不销毁连接

        case WriteResult::ERROR:
            write_buffer_.reset();
            LOG_ERROR("Close for write error, fd:{}", conn_fd_);
            return false; // 销毁连接
    }

    // dummy
    LOG_INFO("Close for write res no match, fd:{}", conn_fd_);
    return false;
}

void HttpConnection::BeginGracefulClose() {
    // 关闭写端，发送 FIN 给客户端
    shutdown(conn_fd_, SHUT_WR);
    
    // 标记为正在关闭
    closing_ = true;
    
    // 注册 EPOLLIN，等待客户端的 FIN
    EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLIN, use_edge_trig_);
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
    LOG_DEBUG("[HttpConnection] Processing request uri:{}, fd:{}, parse_res: {}", request.uri(), conn_fd_, static_cast<int>(parse_result));
    response_.reset();

    switch (parse_result) {
        case ParseResult::OK:
            read_buffer_.retrieve(parser_.consumed_bytes()); // 移动读指针

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
            EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLIN, use_edge_trig_);
            LOG_DEBUG("Conn fd:{} Incomplete, keep waiting.", conn_fd_);
            return;
        default:
            // 统一错误处理
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

    EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLOUT, use_edge_trig_);
}

void HttpConnection::make_response_mmap() {
    write_buffer_.set_response_with_mmap(
        response_.response_data(), 
        response_.response_length(), 
        response_.file_path(),
        response_.file_start(),
        response_.file_size()
    );
}

void HttpConnection::make_response_sendfile() {
    write_buffer_.set_response_with_sendfile(
        response_.response_data(),
        response_.response_length(),
        response_.file_path(),
        response_.file_start(),
        response_.file_size()
    );
}
