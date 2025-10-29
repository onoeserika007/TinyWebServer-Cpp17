//
// Created by inory on 10/29/25.
//

#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <string>
#include <vector>
#include <sys/uio.h>
#include <netinet/in.h>

constexpr const int FILENAME_LEN = 200;
constexpr const int READ_BUFFER_SIZE = 2048;
constexpr const int WRITE_BUFFER_SIZE = 1024;

class HttpConnection {
public:
    HttpConnection() {}

    void Init(int fd, int epoll_fd, sockaddr_in client_addr);
    void Init();
    auto GetClientAddress() -> sockaddr_in& {
        return client_addr_;
    }
    void Destroy();
    bool ReadOnce();
    bool WriteAll();
    void ProcessHttp();

private:
    int epoll_fd_ {-1};
    int conn_fd_ {-1};
    sockaddr_in client_addr_;
    char read_buf_[READ_BUFFER_SIZE];
    char write_buf_[WRITE_BUFFER_SIZE];
    char *m_file_address;
    char real_file_[FILENAME_LEN];
    ssize_t read_idx_;
    ssize_t write_idx_;
    ssize_t bytes_to_send_;
    ssize_t bytes_have_sent_;
    struct iovec m_iv_[2]; // send http and file
    int m_iv_count_;

    // options
    bool use_edge_trig_{};
    bool use_keep_alive_;
};

#endif // HTTP_CONN_H
