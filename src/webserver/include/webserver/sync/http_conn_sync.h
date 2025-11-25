//
// Created by inory on 11/21/25.
//

#ifndef HTTP_CONN_SYNC_H
#define HTTP_CONN_SYNC_H

#include <functional>
#include <netinet/in.h>
#include <string>
#include <vector>

#include "webserver/http_parser.h"
#include "webserver/http_response.h"
#include "input_buffer_sync.h"
#include "output_buffer_sync.h"

class HttpRequest;
class HttpResponse;

constexpr const int FILENAME_LEN = 200;
constexpr const int READ_BUFFER_SIZE = 2048;
constexpr const int WRITE_BUFFER_SIZE = 1024;

class HttpConnectionSync {
public:
    using Middleware = std::function<void(const HttpRequest &, HttpResponse &)>;
    using MessageCallback = std::function<void(const bool should_close)>;

    HttpConnectionSync() {}

    static void add_pre_handler(const Middleware &handler) { pre_handlers_.push_back(handler); }

    static void add_post_handler(const Middleware &handler) { pre_handlers_.push_back(handler); }

    void Init(int fd, sockaddr_in client_addr);
    void Init();
    auto GetClientAddress() -> sockaddr_in & { return client_addr_; }
    void Destroy();
    void Stop();
    bool ProcessHttp();
    void ReceiveLoop(MessageCallback callback);

private:
    static bool PreHandlersCheck(const HttpRequest &request, HttpResponse &response);
    static bool PostHandlersCheck(const HttpRequest &request, HttpResponse &response);

    void BeginGracefulClose(); // 开始优雅关闭
    void make_response_mmap(); // 使用 mmap + writev
    void make_response_sendfile(); // 使用 sendfile

    int conn_fd_{-1};
    sockaddr_in client_addr_;

    // io
    InputBufferSync read_buffer_;
    OutputBufferSync write_buffer_;
    std::string ret_content_;

    // http
    HttpRequestParser parser_;
    HttpResponse response_;
    static inline std::vector<Middleware> pre_handlers_;
    static inline std::vector<Middleware> post_handlers_;

    // options
    bool use_edge_trig_{};
    bool closing_{false}; // 是否正在优雅关闭
    bool running_ {false}; // is running, for accept loop exiting
    bool static use_sendfile_; // true=sendfile, false=mmap+writev

    // Debug
    static std::atomic<uint64_t> read_times;
    static std::atomic<uint64_t> failed_reads_;
    static std::atomic<uint64_t> write_times;

public:
    void static set_use_sendfile(bool enable) { use_sendfile_ = enable; }
    bool static use_sendfile() { return use_sendfile_; }
};

#endif //HTTP_CONN_SYNC_H
