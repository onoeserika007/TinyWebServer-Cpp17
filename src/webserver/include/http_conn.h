//
// Created by inory on 10/29/25.
//

#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <netinet/in.h>
#include <functional>
#include <string>
#include <sys/uio.h>
#include <vector>

#include "http_parser.h"
#include "input_buffer.h"
#include "output_buffer.h"
#include "http_response.h"

class HttpRequest;
class HttpResponse;

constexpr const int FILENAME_LEN = 200;
constexpr const int READ_BUFFER_SIZE = 2048;
constexpr const int WRITE_BUFFER_SIZE = 1024;

class HttpConnection {
public:
    using Middleware = std::function<void(const HttpRequest&, HttpResponse&)>;

    HttpConnection() {}

    static void add_pre_handler(const Middleware& handler) {
        pre_handlers_.push_back(handler);
    }

    static void add_post_handler(const Middleware& handler) {
        pre_handlers_.push_back(handler);
    }

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
    static bool PreHandlersCheck(const HttpRequest& request, HttpResponse& response);
    static bool PostHandlersCheck(const HttpRequest& request, HttpResponse& response);
    int epoll_fd_ {-1};
    int conn_fd_ {-1};
    sockaddr_in client_addr_;

    // io
    InputBuffer read_buffer_;
    OutputBuffer write_buffer_;
    std::string ret_content_;

    // http
    HttpRequestParser parser_;
    HttpResponse response_;
    static inline std::vector<Middleware> pre_handlers_;
    static inline std::vector<Middleware> post_handlers_;

    // options
    bool use_edge_trig_{};
};

#endif // HTTP_CONN_H
