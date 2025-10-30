//
// Created by inory on 10/29/25.
//

#include "http_response.h"
#include <fstream>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/mman.h>   // mmap, munmap, PROT_READ, MAP_PRIVATE

// MIME type 推断辅助函数
static std::string guess_content_type(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".css"))  return "text/css";
    if (path.ends_with(".js"))   return "application/javascript";
    if (path.ends_with(".png"))  return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    return "application/octet-stream";
}

void HttpResponse::set_status(HttpStatus code, std::string reason) {
    status_code_ = code;
    if (!reason.empty()) {
        reason_phrase_ = std::move(reason);
    } else {
        switch (code) {
            case HttpStatus::OK:             reason_phrase_ = "OK"; break;
            case HttpStatus::FOUND:          reason_phrase_ = "Found"; break;
            case HttpStatus::NOT_FOUND:      reason_phrase_ = "Not Found"; break;
            case HttpStatus::BAD_REQUEST:    reason_phrase_ = "Bad Request"; break;
            case HttpStatus::METHOD_NOT_ALLOWED: reason_phrase_ = "Method Not Allowed"; break;
            case HttpStatus::FORBIDDEN:      reason_phrase_ = "Forbidden"; break;
            case HttpStatus::INTERNAL_ERROR: reason_phrase_ = "Internal Server Error"; break;
            default:                         reason_phrase_ = "Unknown";
        }
    }
}

void HttpResponse::add_header(std::string key, std::string value) {
    headers_[std::move(key)] = std::move(value);
}

void HttpResponse::set_body(std::string body) {
    body_ = std::move(body);
    set_content_length(body_.size());
}

void HttpResponse::set_content_length(size_t len) {
    headers_["Content-Length"] = std::to_string(len);
}

void HttpResponse::set_file(std::string filepath) {
    file_path_ = std::move(filepath);
}

void HttpResponse::set_close() { close_connection_ = true; }

void HttpResponse::set_keep_alive() { close_connection_ = false; }

bool HttpResponse::keep_alive() const { return !close_connection_; }

const std::string &HttpResponse::body() { return body_; }

void HttpResponse::set_handled() {
    handled_ = true;
}

bool HttpResponse::is_error() const {
    int code = static_cast<int>(status_code_);
    return code >= 400;
}

bool HttpResponse::is_success() const {
    int code = static_cast<int>(status_code_);
    return code >= 200 && code < 300;
}

bool HttpResponse::is_handled() const {
    return handled_ || !body_.empty() || !file_path_.empty() || is_error();
}

bool HttpResponse::has_header(const std::string& key) const {
    return headers_.find(key) != headers_.end();
}

bool HttpResponse::map_file() const {
    if (mapped_) return file_addr_ != nullptr;

    struct stat st{};
    if (stat(file_path_.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }

    int fd = open(file_path_.c_str(), O_RDONLY);
    if (fd < 0) return false;

    void* addr = mmap(nullptr,           // 地址由系统决定
                      st.st_size,        // 映射大小
                      PROT_READ,         // 只读权限
                      MAP_PRIVATE,       // 私有映射
                      fd,                // 文件描述符
                      0);                // 偏移量
    close(fd); // mmap 后可关闭 fd

    if (addr == MAP_FAILED) return false;

    file_addr_ = static_cast<char*>(addr);
    file_size_ = st.st_size;
    mapped_ = true;
    return true;
}

void HttpResponse::unmap_if_needed() const {
    if (mapped_ && file_addr_) {
        munmap(file_addr_, file_size_);
        file_addr_ = nullptr;
        mapped_ = false;
    }
}

void HttpResponse::build_response() {
    std::string status_line = "HTTP/1.1 " + std::to_string(static_cast<int>(status_code_)) +
                              " " + reason_phrase_ + "\r\n";

    resp_buf_.clear();
    resp_buf_.reserve(512);
    resp_buf_.assign(status_line.begin(), status_line.end());

    // 默认 Content-Length
    if (headers_.find("Content-Length") == headers_.end()) {
        size_t len = has_file() ? file_size_ : body_.size();
        headers_["Content-Length"] = std::to_string(len);
    }

    // 默认 Connection
    if (headers_.find("Connection") == headers_.end()) {
        headers_["Connection"] = close_connection_ ? "close" : "keep-alive";
    }

    // 默认 Server
    if (headers_.find("Server") == headers_.end()) {
        headers_["Server"] = "MyWebServer/1.0";
    }

    // 只有在没有设置 Content-Type 时才设置默认值
    if (headers_.find("Content-Type") == headers_.end()) {
        if (has_file()) {
            headers_["Content-Type"] = guess_content_type(file_path_);
        } else {
            // 检查body是否包含HTML内容
            if (!body_.empty() && (body_.find("<!DOCTYPE html") != std::string::npos ||
                                   body_.find("<html") != std::string::npos)) {
                headers_["Content-Type"] = "text/html";
           } else {
               headers_["Content-Type"] = "text/plain";
           }
        }
    }

    for (const auto& [k, v] : headers_) {
        std::string line = k + ": " + v + "\r\n";
        resp_buf_.insert(resp_buf_.end(), line.begin(), line.end());
    }

    // 分隔空行
    resp_buf_.insert(resp_buf_.end(), {'\r', '\n'}); // \r\n\r\n

    // 如果没有文件，则直接拼上 body
    if (!has_file() && !body_.empty()) {
        resp_buf_.insert(resp_buf_.end(), body_.begin(), body_.end());
    }
}

void HttpResponse::finalize() {
    unmap_if_needed(); // 清理上次可能残留的 mmap

    // 先考虑file，最后build response
    if (!file_path_.empty()) {
        if (!map_file()) {
            // 文件打开失败 → 返回错误页
            unmap_if_needed();
            set_status(HttpStatus::NOT_FOUND);
            set_body("<h1>404 Not Found</h1>");
            add_header("Content-Type", "text/html");
            file_path_.clear(); // 清除 file 模式
            file_size_ = 0;
        }
    }

    build_response();

    // 注意：不要重复设置Content-Length，因为build_response已经设置了
    // set_content_length(body_.size() + file_size());
}

// 缓存构造好的 header
std::vector<char> resp_buf_;

void HttpResponse::reset() {
    unmap_if_needed();

    status_code_ = HttpStatus::OK;
    reason_phrase_.clear();
    body_.clear();
    file_path_.clear();
    file_addr_ = nullptr;
    file_size_ = 0;
    resp_buf_.clear();

    headers_.clear();

    handled_ = false;
}

void HttpResponse::set_error_page(HttpStatus code) {
    set_status(code);

    std::string body;
    switch (code) {
        case HttpStatus::NOT_FOUND:
            body = R"(
                <html><body>
                <h1>404 Not Found</h1>
                <p>The requested resource was not found.</p>
                </body></html>
            )";
            break;
        case HttpStatus::FORBIDDEN:
            body = R"(
                <html><body>
                <h1>403 Forbidden</h1>
                <p>You don't have permission to access this resource.</p>
                </body></html>
            )";
            break;
        case HttpStatus::BAD_REQUEST:
            body = R"(
                <html><body>
                <h1>400 Bad Request</h1>
                <p>Your request syntax is invalid.</p>
                </body></html>
            )";
            break;
        case HttpStatus::INTERNAL_ERROR:
            body = R"(
                <html><body>
                <h1>500 Internal Error</h1>
                <p>An unexpected error occurred on the server.</p>
                </body></html>
            )";
            break;
        default:
            body = "<html><body><h1>Error</h1></body></html>";
    }

    set_body(std::move(body));
    add_header("Content-Type", "text/html");
}