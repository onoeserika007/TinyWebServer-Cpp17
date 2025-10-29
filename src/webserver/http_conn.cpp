//
// Created by inory on 10/29/25.
//

#include "http_conn.h"
#include "epoll_util.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>
#include <iostream>
#include <netinet/in.h>

#include <cstring>
#include <string>

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
}

void HttpConnection::Init() {
    memset(read_buf_, '\0', READ_BUFFER_SIZE);
    memset(write_buf_, '\0', WRITE_BUFFER_SIZE);
    memset(real_file_, '\0', FILENAME_LEN);
}

void HttpConnection::Destroy() {
    EpollUtil::removeFd(epoll_fd_, conn_fd_);
    conn_fd_ = -1;
}

bool HttpConnection::ReadOnce() {
    if (read_idx_ >= READ_BUFFER_SIZE)
    {
        LOG_ERROR("[HttpConnection] Read buffer overflow.");
        return false;
    }


    ssize_t bytes_read = 0;
    //LT读取数据
    if (!use_edge_trig_)
    {
        bytes_read = recv(conn_fd_, read_buf_ + read_idx_, READ_BUFFER_SIZE - read_idx_, 0);
        read_idx_ += bytes_read;

        if (bytes_read <= 0)
        {
            return false;
        }

        return true;
    }
    //ET读数据
    else
    {
        while (true)
        {
            bytes_read = recv(conn_fd_, read_buf_ + read_idx_, READ_BUFFER_SIZE - read_idx_, 0);
            if (bytes_read == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }

                LOG_ERROR("[HttpConnection] Read error: errno=%d, error: %s", errno, strerror(errno));
                return false;
            }

            if (bytes_read == 0)
            {
                LOG_INFO("[HttpConnection] Client disconnected.");
                return false;
            }

            read_idx_ += bytes_read;
        }
        return true;
    }
}

bool HttpConnection::WriteAll() {
    int temp = 0;

    if (bytes_to_send_ == 0)
    {
        EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLIN, use_edge_trig_);
        Init();
        return true;
    }

    while (true)
    {
        // 分散写入，避免拷贝开销
        const auto write_bytes = writev(conn_fd_, m_iv_, m_iv_count_);

        if (write_bytes < 0)
        {
            if (errno == EAGAIN)
            {
                EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLOUT, use_edge_trig_);
                return true;
            }
            // unmap();
            return false;
        }

        bytes_have_sent_ += write_bytes;
        bytes_to_send_ -= write_bytes;
        if (bytes_have_sent_ >= m_iv_[0].iov_len)
        {
            // 响应头已全部发送完毕
            m_iv_[0].iov_len = 0;  // 头部不再发送

            // 更新文件部分偏移：从文件中未发送的位置开始
            m_iv_[1].iov_base = m_file_address + (bytes_have_sent_ - write_idx_);
            m_iv_[1].iov_len = bytes_to_send_; // 剩余待发文件长度
        }
        else
        {
            // 响应头还没发完
            m_iv_[0].iov_base = write_buf_ + bytes_have_sent_;
            m_iv_[0].iov_len = m_iv_[0].iov_len - bytes_have_sent_;
        }

        if (bytes_to_send_ <= 0)
        {
            // unmap();
            EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLIN, use_edge_trig_);

            if (use_keep_alive_)
            {
                Init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

void HttpConnection::ProcessHttp() {
    std::string ret_content = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
    sprintf(write_buf_, ret_content.data());
    bytes_to_send_ = ret_content.size();
    m_iv_[0].iov_base = write_buf_;
    m_iv_[0].iov_len = bytes_to_send_;
    m_iv_count_ = 1;

    EpollUtil::modFd(epoll_fd_, conn_fd_, EPOLLOUT, use_edge_trig_);
}

