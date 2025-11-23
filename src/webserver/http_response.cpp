//
// Created by inory on 10/29/25.
//

#include "include/webserver/http_response.h"
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/mman.h> // mmap, munmap, PROT_READ, MAP_PRIVATE
#include <sys/stat.h>
#include <unistd.h>
#include <charconv>    // Modified: for to_chars fast integer -> string conversion
#include <algorithm>   // Modified: for std::max

// MIME type 推断辅助函数
static std::string guess_content_type(const std::string &path) {
    if (path.ends_with(".html"))
        return "text/html";
    if (path.ends_with(".css"))
        return "text/css";
    if (path.ends_with(".js"))
        return "application/javascript";
    if (path.ends_with(".png"))
        return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg"))
        return "image/jpeg";
    return "application/octet-stream";
}

void HttpResponse::set_status(HttpStatus code, std::string reason) {
    status_code_ = code;
    if (!reason.empty()) {
        reason_phrase_ = std::move(reason);
        return;
    }

    switch (code) {
        case HttpStatus::OK:
            reason_phrase_ = "OK";
            break;
        case HttpStatus::FOUND:
            reason_phrase_ = "Found";
            break;
        case HttpStatus::PARTIAL_CONTENT:
            reason_phrase_ = "Partial Content";
            break;
        case HttpStatus::NOT_MODIFIED:
            reason_phrase_ = "Not Modified";
            break;
        case HttpStatus::BAD_REQUEST:
            reason_phrase_ = "Bad Request";
            break;
        case HttpStatus::METHOD_NOT_ALLOWED:
            reason_phrase_ = "Method Not Allowed";
            break;
        case HttpStatus::FORBIDDEN:
            reason_phrase_ = "Forbidden";
            break;
        case HttpStatus::NOT_FOUND:
            reason_phrase_ = "Not Found";
            break;
        case HttpStatus::REQUESTED_RANGE_NOT_SATISFIABLE:
            reason_phrase_ = "Requested Range Not Satisfiable";
            break;
        case HttpStatus::INTERNAL_ERROR:
            reason_phrase_ = "Internal Server Error";
            break;
        default:
            reason_phrase_ = "Unknown";
    }
}

// Modified: keep the same API; optimize header insertion cost (reuse unordered_map interface unchanged)
void HttpResponse::add_header(std::string key, std::string value) {
    // 优化：vector 替代 map → 手动查找是否已存在 header
    for (auto& kv : headers_) {
        if (kv.first == key) {
            kv.second = std::move(value);  // 覆盖旧值
            return;
        }
    }

    // 不存在则新增
    headers_.emplace_back(std::move(key), std::move(value));
}

// Modified: optimize set_content_length to avoid temporaries when possible
void HttpResponse::set_content_length(size_t len) {
    // NEW OPTIMIZATION: use to_chars into a small local buffer to avoid intermediate std::string temporary in some cases
    char numbuf[32];
    auto [ptr, ec] = std::to_chars(numbuf, numbuf + sizeof(numbuf), len);
    if (ec == std::errc()) {
        add_header("Content-Length", std::string(numbuf, ptr - numbuf));
    } else {
        // fallback (very unlikely)
        add_header("Content-Length", std::to_string(len));
    }
}

void HttpResponse::set_body(std::string body) {
    body_ = std::move(body);
    set_content_length(body_.size());
}

// Modified: do a stat once and cache the result in the response object to avoid repeated syscalls later
void HttpResponse::set_file(std::string filepath) {
    file_path_ = std::move(filepath);
    file_start_ = 0;

    // NEW OPTIMIZATION: cache stat result to avoid calling stat again in finalize()
    struct stat st{};
    if (stat(file_path_.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        file_size_ = static_cast<size_t>(st.st_size);
        file_stat_valid_ = true;
        cached_st_ = st; // cache for later use
    } else {
        file_size_ = 0;
        file_stat_valid_ = false;
    }
}

// Modified: set_file_with_range should not trigger extra stat — assume caller calculated correct range
void HttpResponse::set_file_with_range(std::string filepath, size_t start, size_t length) {
    file_path_ = std::move(filepath);
    file_start_ = start;
    file_size_ = length;

    // Try to stat once if not done yet (best-effort; failure not fatal)
    if (!file_stat_valid_) {
        struct stat st{};
        if (stat(file_path_.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            file_stat_valid_ = true;
            cached_st_ = st;
        }
    }
}

void HttpResponse::set_keep_alive(bool enable) { close_connection_ = !enable; }

bool HttpResponse::keep_alive() const { return !close_connection_; }

const std::string &HttpResponse::body() { return body_; }

void HttpResponse::set_handled() { handled_ = true; }

bool HttpResponse::is_error() const {
    int code = static_cast<int>(status_code_);
    return code >= 400;
}

bool HttpResponse::is_success() const {
    int code = static_cast<int>(status_code_);
    return code >= 200 && code < 300;
}

bool HttpResponse::is_handled() const { return handled_ || !body_.empty() || !file_path_.empty() || is_error(); }

bool HttpResponse::has_header(const std::string &key) const {
    // 优化：vector 查找
    for (const auto& kv : headers_) {
        if (kv.first == key)
            return true;
    }
    return false;
}

auto HttpResponse::get_header(const std::string &key) const -> std::string {
    // 优化：vector 线性查找（cache 更友好）
    for (const auto& kv : headers_) {
        if (kv.first == key)
            return kv.second;
    }
    return "";
}

// Modified: highly optimized build_response to minimize string/STL operations
void HttpResponse::build_response() {
    // helpers that avoid creating std::string temporaries
    auto append_raw = [&](const char* p, size_t n) {
        if (n == 0) return;
        resp_buf_.insert(resp_buf_.end(), p, p + n);
    };
    auto append_sv = [&](const std::string_view &sv) {
        append_raw(sv.data(), sv.size());
    };
    auto append_cstr = [&](const char* s) {
        append_raw(s, std::strlen(s));
    };

    // 1) Prepare status line WITHOUT std::string concatenation
    //    Format: "HTTP/1.1 <code> <reason>\r\n"
    char status_buf[64];
    size_t status_len = 0;
    // copy "HTTP/1.1 "
    const char prefix[] = "HTTP/1.1 ";
    std::memcpy(status_buf + status_len, prefix, sizeof(prefix) - 1);
    status_len += sizeof(prefix) - 1;

    // convert status code (int) using to_chars
    int code = static_cast<int>(status_code_);
    auto [ptr, ec] = std::to_chars(status_buf + status_len, status_buf + sizeof(status_buf), code);
    if (ec != std::errc()) {
        // fallback (rare)
        std::string tmp = std::to_string(code);
        std::memcpy(status_buf + status_len, tmp.data(), tmp.size());
        status_len += tmp.size();
    } else {
        status_len = ptr - status_buf;
    }

    // append space + reason_phrase_ + CRLF
    status_buf[status_len++] = ' ';
    // We'll append reason_phrase_ directly into resp_buf_ rather than copying into status_buf_
    // So status_buf currently contains "HTTP/1.1 <code> " (no CRLF yet)

    // 2) Scan headers_ ONCE to determine what we must add and estimate size.
    bool has_content_length = false;
    bool has_connection = false;
    bool has_server = false;
    bool has_content_type = false;

    size_t headers_total_size = 0; // sum of key + ": " + value + "\r\n"
    for (const auto &kv : headers_) {
        const std::string &k = kv.first;
        const std::string &v = kv.second;
        if (k == "Content-Length") has_content_length = true;
        else if (k == "Connection") has_connection = true;
        else if (k == "Server") has_server = true;
        else if (k == "Content-Type") has_content_type = true;
        headers_total_size += k.size() + 2 + v.size() + 2; // ": " and "\r\n"
    }

    // If we will add any default headers, include their sizes
    // Determine content length that will be used (do not modify headers_ yet)
    size_t effective_body_len = has_file() ? file_size_ : body_.size();
    size_t content_length_additional = 0;
    if (!has_content_length) {
        // compute digit length of effective_body_len quickly
        char numbuf[32];
        auto [nptr, nec] = std::to_chars(numbuf, numbuf + sizeof(numbuf), effective_body_len);
        size_t numlen = (nec == std::errc()) ? (nptr - numbuf) : std::string(numbuf).size();
        content_length_additional = std::strlen("Content-Length: ") + numlen + 2; // "\r\n"
        headers_total_size += content_length_additional;
    }
    if (!has_connection) {
        headers_total_size += std::strlen("Connection: ") + std::strlen(close_connection_ ? "close" : "keep-alive") + 2;
    }
    if (!has_server) {
        headers_total_size += std::strlen("Server: ") + std::strlen("MyWebServer/1.0") + 2;
    }
    if (!has_content_type) {
        std::string guessed;
        if (has_file()) guessed = guess_content_type(file_path_);
        else {
            if (!body_.empty() && (body_.find("<!DOCTYPE html") != std::string::npos || body_.find("<html") != std::string::npos))
                guessed = "text/html";
            else guessed = "text/plain";
        }
        headers_total_size += std::strlen("Content-Type: ") + guessed.size() + 2;
        // Note: we won't insert into headers_ here; we'll append the computed value to resp_buf_ directly.
    }

    // final blank line = 2
    headers_total_size += 2;

    // body length only if not file
    size_t body_size_to_add = (!has_file() ? body_.size() : 0);

    // total estimate: status line + reason + headers + body
    size_t estimated_total = status_len + reason_phrase_.size() + 2 + headers_total_size + body_size_to_add;

    // Reserve once
    resp_buf_.clear();
    resp_buf_.reserve(std::max(estimated_total, static_cast<size_t>(1024)));

    // 3) Append status_buf, reason_phrase_, CRLF
    append_raw(status_buf, status_len);
    append_sv(std::string_view(reason_phrase_));
    append_cstr("\r\n");

    // 4) Append existing headers_ entries (use direct data pointers, no temporaries)
    for (const auto &kv : headers_) {
        append_sv(std::string_view(kv.first));
        append_cstr(": ");
        append_sv(std::string_view(kv.second));
        append_cstr("\r\n");
    }

    // 5) Append any default headers that were missing (append directly, don't modify headers_ to avoid allocations here)
    if (!has_content_length) {
        // append "Content-Length: <num>\r\n"
        append_cstr("Content-Length: ");
        char numbuf[32];
        auto [nptr2, nec2] = std::to_chars(numbuf, numbuf + sizeof(numbuf), effective_body_len);
        if (nec2 == std::errc()) {
            append_raw(numbuf, nptr2 - numbuf);
        } else {
            std::string tmp = std::to_string(effective_body_len);
            append_sv(std::string_view(tmp));
        }
        append_cstr("\r\n");
    }

    if (!has_connection) {
        append_cstr("Connection: ");
        append_cstr(close_connection_ ? "close" : "keep-alive");
        append_cstr("\r\n");
    }

    if (!has_server) {
        append_cstr("Server: ");
        append_cstr("MyWebServer/1.0");
        append_cstr("\r\n");
    }

    if (!has_content_type) {
        append_cstr("Content-Type: ");
        if (has_file()) {
            append_sv(std::string_view(guess_content_type(file_path_)));
        } else {
            if (!body_.empty() && (body_.find("<!DOCTYPE html") != std::string::npos || body_.find("<html") != std::string::npos))
                append_cstr("text/html");
            else
                append_cstr("text/plain");
        }
        append_cstr("\r\n");
    }

    // 6) final blank line
    append_cstr("\r\n");

    // 7) Append body only if not file (we assume file content will be sent by sendfile/writev)
    if (!has_file() && !body_.empty()) {
        append_sv(std::string_view(body_));
    }
}

// Modified: finalize no longer always calls stat(); it uses cached stat when available (set in set_file())
// If file_stat is invalid, it will attempt one stat as fallback, but typical successful path avoids repeated syscalls.
void HttpResponse::finalize() {
    // 验证文件是否存在（只验证，不打开） - use cached stat if available
    if (!file_path_.empty()) {
        bool ok = file_stat_valid_;
        if (!ok) {
            struct stat st{};
            if (stat(file_path_.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                cached_st_ = st;
                file_stat_valid_ = true;
                ok = true;
                file_size_ = static_cast<size_t>(st.st_size); // update size if we didn't already
            }
        }

        if (!ok) {
            // 文件不存在或不可访问 → 返回错误页
            set_status(HttpStatus::NOT_FOUND);
            set_body("<h1>404 Not Found</h1>");
            add_header("Content-Type", "text/html");
            file_path_.clear();
            file_size_ = 0;
            file_stat_valid_ = false;
        }
    }

    build_response();
}

// Modified: reset keeps capacity to allow reuse (do not shrink_to_fit)
void HttpResponse::reset() {
    status_code_ = HttpStatus::OK;
    reason_phrase_.clear();
    body_.clear();
    file_path_.clear();
    file_size_ = 0;
    file_start_ = 0;
    // keep resp_buf_ capacity; only clear content
    resp_buf_.clear();
    // keep headers_ bucket allocation; clear entries
    headers_.clear();

    handled_ = false;
    close_connection_ = false;

    // preserve file_stat cache boolean but clear flag so future set_file() will repopulate
    file_stat_valid_ = false;
    std::memset(&cached_st_, 0, sizeof(cached_st_));
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
