//
// Created by inory on 10/29/25.
//

#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

// http_response.h
#pragma once
#include <string>
#include <vector>

#include <string>
#include <map>
#include <memory>

enum class HttpStatus {
    OK = 200,
    FOUND = 302,
    PARTIAL_CONTENT = 206,
    NOT_MODIFIED = 304,
    BAD_REQUEST = 400,
    METHOD_NOT_ALLOWED = 405,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    REQUESTED_RANGE_NOT_SATISFIABLE = 416,
    INTERNAL_ERROR = 500
};

class HttpResponse {
public:
    void set_status(HttpStatus code, std::string reason = "");
    void add_header(std::string key, std::string value);
    void set_body(std::string body);
    void set_content_length(size_t len);
    void set_file(std::string filepath); // 触发 mmap + writev
    void set_file_with_range(std::string filepath, size_t start, size_t length); // 支持范围请求
    void set_error_page(HttpStatus code);
    void set_keep_alive(bool enable);
    void set_handled();

    bool is_error() const;        // 是否是错误响应（4xx/5xx）
    bool is_success() const;      // 是否成功（2xx）
    bool is_handled() const;      // 是否已被拦截处理（如鉴权失败）
    bool has_header(const std::string& key) const;
    bool keep_alive() const;
    const std::string& body();

    // 构建最终要发送的内容（供 OutputBuffer 使用）
    void finalize();

    // 提供给 OutputBuffer 的接口（应用层只提供参数）
    const char* response_data() const { return resp_buf_.data(); }
    size_t response_length() const { return resp_buf_.size(); }
    std::string response() const { return std::string {resp_buf_.begin(), resp_buf_.end()}; }
    
    const std::string& file_path() const { return file_path_; }
    size_t file_start() const { return file_start_; }
    size_t file_size() const { return file_size_; }

    bool has_file() const { return !file_path_.empty(); }
    bool will_close() const { return close_connection_; }

    void reset();

private:
    HttpStatus status_code_ = HttpStatus::OK;
    std::string reason_phrase_;
    std::map<std::string, std::string> headers_;
    std::string body_;
    bool handled_ {};

    // 文件相关（只存储参数，不做 I/O）
    std::string file_path_;
    size_t file_size_ = 0;
    size_t file_start_ = 0;  // 文件范围起始位置

    // 缓存构造好的 header
    std::vector<char> resp_buf_;

    bool close_connection_ = false;

    void build_response();

    friend class HttpResponseBuilder; // 可选：builder 模式
};


#endif //HTTP_RESPONSE_H