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
    NOT_FOUND = 404,
    BAD_REQUEST = 400,
    FORBIDDEN = 403,
    INTERNAL_ERROR = 500
};

class HttpResponse {
public:
    void set_status(HttpStatus code, std::string reason = "");
    void add_header(std::string key, std::string value);
    void set_body(std::string body);
    void set_content_length(size_t len);
    void set_file(std::string filepath); // 触发 mmap + writev
    void set_error_page(HttpStatus code);
    void set_close();                    // 不启用 keep-alive
    void set_keep_alive();
    void set_handled();

    bool is_error() const;        // 是否是错误响应（4xx/5xx）
    bool is_success() const;      // 是否成功（2xx）
    bool is_handled() const;      // 是否已被拦截处理（如鉴权失败）
    bool has_header(const std::string& key) const;
    bool keep_alive() const;
    const std::string& body();

    // 构建最终要发送的内容（供 OutputBuffer 使用）
    void finalize();

    // 提供给 OutputBuffer 的接口
    const char* response_data() const { return header_buf_.data(); }
    size_t response_length() const { return header_len_; }
    const char* file_address() const { return file_addr_; }
    size_t file_size() const { return file_size_; }

    bool has_file() const { return file_size_ > 0; }
    bool will_close() const { return close_connection_; }

private:
    HttpStatus status_code_ = HttpStatus::OK;
    std::string reason_phrase_;
    std::map<std::string, std::string> headers_;
    std::string body_;
    bool handled_ {};

    std::string file_path_;
    mutable char* file_addr_ = nullptr;
    mutable size_t file_size_ = 0;
    mutable bool mapped_ = false;

    // 缓存构造好的 header
    std::vector<char> header_buf_;
    size_t header_len_ = 0;

    bool close_connection_ = false;

    void build_response();
    bool map_file() const; // mmap 文件
    void unmap_if_needed() const;

    friend class HttpResponseBuilder; // 可选：builder 模式
};


#endif //HTTP_RESPONSE_H
