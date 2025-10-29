//
// Created by inory on 10/29/25.
//

// http_request_parser.cpp
#include "http_parser.h"
#include "http_request.h" // 假设已有 HttpRequest 定义


bool HttpRequestParser::iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char l, char r) {
               return std::tolower(l) == std::tolower(r);
           });
}

auto HttpRequestParser::trim(std::string_view sv) -> std::string_view  {
    auto start = sv.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = sv.find_last_not_of(" \t\r\n");
    return sv.substr(start, end - start + 1);
}

auto HttpRequestParser::split_by_first(std::string_view str, char delim) -> std::pair<std::string_view, std::string_view> {
    auto pos = str.find(delim);
    if (pos == std::string_view::npos) {
        return {str, {}};
    }
    return {trim(str.substr(0, pos)), trim(str.substr(pos + 1))};
}

void HttpRequestParser::reset() {
    state_ = State::RequestLine;
    consumed_bytes_ = 0;
    body_bytes_received_ = 0;
    content_length_ = 0;
}

auto HttpRequestParser::parse(std::string_view input, HttpRequest& request) -> ParseResult {
    consumed_bytes_ = 0;
    body_bytes_received_ = 0;

    while (!input.empty()) {
        switch (state_) {
            case State::RequestLine: {
                auto crlf = input.find("\r\n");
                if (crlf == std::string_view::npos) {
                    return ParseResult::INCOMPLETE; // 等待完整请求行
                }

                std::string_view line = input.substr(0, crlf);
                auto result = parse_request_line(line, request);
                if (result != ParseResult::OK) return result;

                input.remove_prefix(crlf + 2);
                consumed_bytes_ += crlf + 2;
                state_ = State::Headers;
                break;
            }

            case State::Headers: {
                auto headers_end = input.find("\r\n\r\n");
                if (headers_end == std::string_view::npos) {
                    return ParseResult::INCOMPLETE; // 头部未结束
                }

                std::string_view headers = input.substr(0, headers_end);
                auto result = parse_headers(headers, request);
                if (result != ParseResult::OK) return result;

                input.remove_prefix(headers_end + 4);
                consumed_bytes_ += headers_end + 4;

                if (content_length_ == 0) {
                    state_ = State::Done;
                } else {
                    state_ = State::Body;
                }
                break;
            }

            case State::Body: {
                if (input.size() < content_length_ - body_bytes_received_) {
                    // 数据不够，等待更多
                    body_bytes_received_ += input.size();
                    consumed_bytes_ += input.size();
                    return ParseResult::INCOMPLETE;
                }

                // 收到足够 body
                std::string_view body = input.substr(0, content_length_ - body_bytes_received_);
                auto result = parse_body(body, request);
                if (result != ParseResult::OK) return result;

                consumed_bytes_ += body.size();
                state_ = State::Done;
                return ParseResult::OK;
            }

            case State::Done:
                return ParseResult::OK;

            case State::Invalid:
                return ParseResult::BAD_REQUEST;
        }
    }

    return state_ == State::Done ? ParseResult::OK : ParseResult::INCOMPLETE;
}

ParseResult HttpRequestParser::parse_request_line(std::string_view line, HttpRequest& req) {
    auto space1 = line.find(' ');
    if (space1 == std::string_view::npos) return ParseResult::BAD_REQUEST;

    auto space2 = line.find(' ', space1 + 1);
    if (space2 == std::string_view::npos) return ParseResult::BAD_REQUEST;

    std::string_view method = line.substr(0, space1);
    std::string_view uri = line.substr(space1 + 1, space2 - space1 - 1);
    std::string_view version = line.substr(space2 + 1);

    // 方法识别
    if (iequals(method, "GET")) {
        req.set_method(HttpRequest::Method::GET);
    } else if (iequals(method, "POST")) {
        req.set_method(HttpRequest::Method::POST);
        req.set_cgi(true); // 或者通过配置决定
    } else {
        return ParseResult::BAD_REQUEST;
    }

    // URI 处理
    if (uri.starts_with("http://")) {
        auto slash = uri.find('/', 7);
        uri = slash != std::string_view::npos ? uri.substr(slash) : "/";
    } else if (uri.starts_with("https://")) {
        auto slash = uri.find('/', 8);
        uri = slash != std::string_view::npos ? uri.substr(slash) : "/";
    }

    if (uri.empty() || !uri.starts_with('/')) {
        return ParseResult::BAD_REQUEST;
    }

    // 默认页面
    // if (uri == "/") {
    //     // uri = "/judge.html";
    // }

    req.set_uri(std::string(uri)); // 拷贝一份（也可设计为零拷贝生命周期管理）

    // 版本检查
    if (!iequals(version, "HTTP/1.1")) {
        return ParseResult::BAD_REQUEST;
    }

    return ParseResult::OK;
}

ParseResult HttpRequestParser::parse_headers(std::string_view headers, HttpRequest& req) {
    content_length_ = 0;

    size_t start = 0;
    while (start < headers.size()) {
        auto next = headers.find("\r\n", start);
        std::string_view line = (next == std::string_view::npos)
                                    ? headers.substr(start)
                                    : headers.substr(start, next - start);

        if (line.empty()) break; // 到达头部结束

        auto [key, value] = split_by_first(line, ':');
        if (key.empty()) continue;

        if (iequals(key, "Connection")) {
            if (iequals(value, "keep-alive")) {
                req.set_keep_alive(true);
            }
        } else if (iequals(key, "Content-Length")) {
            try {
                content_length_ = std::stoull(std::string(value));
                req.set_content_length(content_length_);
            } catch (...) {
                return ParseResult::BAD_REQUEST;
            }
        } else if (iequals(key, "Host")) {
            req.set_host(std::string(value));
        } else if (iequals(value, "close")) {
            req.set_keep_alive(false); // 显式关闭
        }
        // 忽略其他头（可选记录日志）

        start = (next == std::string_view::npos) ? headers.size() : next + 2;
    }

    return ParseResult::OK;
}

ParseResult HttpRequestParser::parse_body(std::string_view body, HttpRequest& req) {
    if (body.size() != content_length_) {
        return ParseResult::BAD_REQUEST;
    }

    req.set_body(std::string(body)); // POST 表单数据等
    return ParseResult::OK;
}