//
// Created by inory on 10/29/25.
//

// http_request_parser.cpp
#include "include/webserver/http_parser.h"
#include <iomanip>
#include <sstream>
#include "include/webserver/http_request.h"

// Modified: added headers for optimized parsing and conversions
#include <c++/9/cstdint>
#include <cctype>
#include <charconv>
#include <cstring>
#include <vector>

bool HttpRequestParser::iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char l, char r) { return std::tolower(l) == std::tolower(r); });
}

auto HttpRequestParser::trim(std::string_view sv) -> std::string_view {
    auto start = sv.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos)
        return "";
    auto end = sv.find_last_not_of(" \t\r\n");
    return sv.substr(start, end - start + 1);
}

auto HttpRequestParser::split_by_first(std::string_view str,
                                       char delim) -> std::pair<std::string_view, std::string_view> {
    auto pos = str.find(delim);
    if (pos == std::string_view::npos) {
        return {str, {}};
    }
    return {trim(str.substr(0, pos)), trim(str.substr(pos + 1))};
}

// Modified: optimized parse_form_data to avoid istringstream and repeated allocations.
// Keep original function signature but implement efficient string_view-based parsing.
void HttpRequestParser::parse_form_data(const std::string &body, HttpRequest &req) {
    // NEW OPTIMIZATION: operate on string_view to avoid copies/substr as much as possible
    std::string_view sv(body);

    // Pre-allocate a buffer for decoded values to avoid repeated reallocations.
    // Conservative reserve: body length (worst-case).
    std::string decoded;
    decoded.reserve(body.size());

    // LUT for hex decoding: 0xFF == invalid
    static uint8_t hex_lut[256];
    static bool hex_lut_init = false;
    if (!hex_lut_init) {
        std::memset(hex_lut, 0xFF, sizeof(hex_lut));
        for (char c = '0'; c <= '9'; ++c) hex_lut[static_cast<unsigned char>(c)] = c - '0';
        for (char c = 'a'; c <= 'f'; ++c) hex_lut[static_cast<unsigned char>(c)] = 10 + (c - 'a');
        for (char c = 'A'; c <= 'F'; ++c) hex_lut[static_cast<unsigned char>(c)] = 10 + (c - 'A');
        hex_lut_init = true;
    }

    auto url_decode_to = [&](std::string_view in, std::string &out) {
        out.clear();
        out.reserve(in.size());
        const char *p = in.data();
        const char *end = p + in.size();
        while (p < end) {
            if (*p == '%' && p + 2 < end) {
                uint8_t hi = hex_lut[static_cast<unsigned char>(p[1])];
                uint8_t lo = hex_lut[static_cast<unsigned char>(p[2])];
                if (hi != 0xFF && lo != 0xFF) {
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    p += 3;
                    continue;
                }
            }
            if (*p == '+') {
                out.push_back(' ');
            } else {
                out.push_back(*p);
            }
            ++p;
        }
    };

    size_t pos = 0;
    while (pos < sv.size()) {
        auto amp = sv.find('&', pos);
        std::string_view pair = (amp == std::string_view::npos) ? sv.substr(pos) : sv.substr(pos, amp - pos);

        auto eq = pair.find('=');
        if (eq != std::string_view::npos) {
            std::string_view key_view = pair.substr(0, eq);
            std::string_view val_view = pair.substr(eq + 1);

            url_decode_to(key_view, decoded);
            std::string key_decoded = decoded; // one allocation copy to match HttpRequest API
            url_decode_to(val_view, decoded);
            std::string val_decoded = decoded;

            req.add_form_field(std::move(key_decoded), std::move(val_decoded));
        }

        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
}

void HttpRequestParser::reset() {
    state_ = State::RequestLine;
    consumed_bytes_ = 0;
    body_bytes_received_ = 0;
    content_length_ = 0;
}

auto HttpRequestParser::parse(std::string_view input, HttpRequest &request) -> ParseResult {
    consumed_bytes_ = 0;
    body_bytes_received_ = 0;

    while (!input.empty()) {
        switch (state_) {
            case State::RequestLine: {
                // Modified: avoid find of "\r\n" twice. Use find once and operate with string_view.
                auto crlf = input.find("\r\n");
                if (crlf == std::string_view::npos) {
                    return ParseResult::INCOMPLETE; // 等待完整请求行
                }

                std::string_view line = input.substr(0, crlf);
                auto result = parse_request_line(line, request);
                if (result != ParseResult::OK)
                    return result;

                input.remove_prefix(crlf + 2);
                consumed_bytes_ += crlf + 2;
                state_ = State::Headers;
                break;
            }

            case State::Headers: {
                // Modified: find header-section end once
                auto headers_end = input.find("\r\n\r\n");
                if (headers_end == std::string_view::npos) {
                    return ParseResult::INCOMPLETE; // 头部未结束
                }

                std::string_view headers = input.substr(0, headers_end);
                auto result = parse_headers(headers, request);
                if (result != ParseResult::OK)
                    return result;

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
                size_t remaining_needed = content_length_ - body_bytes_received_;
                if (input.size() < remaining_needed) {
                    // 数据不够，等待更多
                    body_bytes_received_ += input.size();
                    consumed_bytes_ += input.size();
                    return ParseResult::INCOMPLETE;
                }

                // 收到足够 body
                std::string_view body = input.substr(0, remaining_needed);
                auto result = parse_body(body, request);
                if (result != ParseResult::OK)
                    return result;

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

// Modified: reimplemented parse_request_line using a single-pass scan (no repeated find/starts_with calls)
ParseResult HttpRequestParser::parse_request_line(std::string_view line, HttpRequest &req) {
    // Single pass: find method, uri, version by scanning for spaces
    size_t len = line.size();
    size_t i = 0;

    // find first space
    size_t space1 = std::string_view::npos;
    for (; i < len; ++i) {
        if (line[i] == ' ') { space1 = i; ++i; break; }
    }
    if (space1 == std::string_view::npos) return ParseResult::BAD_REQUEST;

    // find second space
    size_t space2 = std::string_view::npos;
    for (; i < len; ++i) {
        if (line[i] == ' ') { space2 = i; ++i; break; }
    }
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

    // URI 处理 - 分离路径和查询参数
    std::string_view path = uri;
    std::string_view query;

    // Modified: more efficient handling for absolute URLs & path, single-pass style.
    // We look for "://" to detect scheme and then the first '/' after host, or treat as absolute path.
    size_t scheme_pos = path.find("://");
    if (scheme_pos != std::string_view::npos) {
        // absolute URL: find first '/' after scheme_pos+3
        size_t host_start = scheme_pos + 3;
        size_t slash = path.find('/', host_start);
        if (slash != std::string_view::npos) {
            path = path.substr(slash);
        } else {
            path = "/";
        }
    } else {
        // path likely begins with '/'
        if (!path.empty() && path[0] == '/') {
            // keep as-is
        } else {
            // if no leading '/', treat as bad request (original logic required leading '/')
            return ParseResult::BAD_REQUEST;
        }
    }

    // 分离查询参数
    size_t query_pos = path.find('?');
    if (query_pos != std::string_view::npos) {
        query = path.substr(query_pos + 1);
        path = path.substr(0, query_pos);
    }

    if (path.empty()) {
        path = "/";
    } else if (!path.empty() && path[0] != '/') {
        return ParseResult::BAD_REQUEST;
    }

    // 设置处理后的URI（不包含查询参数）
    req.set_uri(std::string(path));

    // 如果有查询参数，可以将其添加到请求中（可选）
    if (!query.empty()) {
        req.add_header("Query-String", std::string(query));
        // 对于GET请求，也可以解析查询参数为表单字段
        if (req.method() == HttpRequest::Method::GET) {
            // parse_form_data expects std::string; pass converted string to reuse optimized code.
            parse_form_data(std::string(query), req);
        }
    }

    // 版本检查
    if (!iequals(version, "HTTP/1.1") && !iequals(version, "HTTP/1.0")) {
        return ParseResult::BAD_REQUEST;
    }

    req.set_version(std::string(version));

    return ParseResult::OK;
}

// Modified: optimized parse_headers to single-pass parse lines and avoid repeated find/trim allocations.
ParseResult HttpRequestParser::parse_headers(std::string_view headers, HttpRequest &req) {
    content_length_ = 0;

    const char *data = headers.data();
    size_t total = headers.size();
    size_t pos = 0;

    // We'll parse line by line by scanning for "\r\n"
    while (pos < total) {
        // find end of line
        size_t line_end = std::string_view::npos;
        for (size_t k = pos; k + 1 < total; ++k) {
            if (data[k] == '\r' && data[k + 1] == '\n') { line_end = k; break; }
        }
        if (line_end == std::string_view::npos) {
            // last line without CRLF - treat whole remainder as one line
            line_end = total;
        }

        std::string_view line(data + pos, line_end - pos);
        if (line.empty()) {
            break; // 到达头部结束或多余空行
        }

        // split by first ':'
        auto colon_pos = line.find(':');
        if (colon_pos != std::string_view::npos) {
            // trim key and value manually to avoid extra substr calls
            std::string_view raw_key = line.substr(0, colon_pos);
            std::string_view raw_val = line.substr(colon_pos + 1);

            // trim key
            size_t kstart = 0;
            while (kstart < raw_key.size() && (raw_key[kstart] == ' ' || raw_key[kstart] == '\t' || raw_key[kstart] == '\r' || raw_key[kstart] == '\n')) ++kstart;
            size_t kend = raw_key.size();
            while (kend > kstart && (raw_key[kend - 1] == ' ' || raw_key[kend - 1] == '\t' || raw_key[kend - 1] == '\r' || raw_key[kend - 1] == '\n')) --kend;
            std::string_view key = raw_key.substr(kstart, kend - kstart);

            // trim val
            size_t vstart = 0;
            while (vstart < raw_val.size() && (raw_val[vstart] == ' ' || raw_val[vstart] == '\t' || raw_val[vstart] == '\r' || raw_val[vstart] == '\n')) ++vstart;
            size_t vend = raw_val.size();
            while (vend > vstart && (raw_val[vend - 1] == ' ' || raw_val[vend - 1] == '\t' || raw_val[vend - 1] == '\r' || raw_val[vend - 1] == '\n')) --vend;
            std::string_view value = raw_val.substr(vstart, vend - vstart);

            // Store header by converting to std::string only once (match original API)
            req.add_header(std::string(key), std::string(value));

            // Handle well-known headers efficiently (case-insensitive)
            if (iequals(key, "Connection")) {
                if (iequals(value, "keep-alive")) {
                    req.set_keep_alive(true);
                } else if (iequals(value, "close")) {
                    req.set_keep_alive(false);
                }
            } else if (iequals(key, "Content-Length")) {
                // Modified: use fast integer parse instead of std::stoull (no exceptions).
                uint64_t parsed = 0;
                const char *p = value.data();
                const char *endp = p + value.size();
                if (p == endp) return ParseResult::BAD_REQUEST;
                for (; p < endp; ++p) {
                    char c = *p;
                    if (c < '0' || c > '9') return ParseResult::BAD_REQUEST;
                    parsed = parsed * 10 + (c - '0');
                }
                content_length_ = parsed;
                req.set_content_length(content_length_);
            } else if (iequals(key, "Host")) {
                req.set_host(std::string(value));
            } else if (iequals(key, "Content-Type")) {
                req.add_header("Content-Type", std::string(value));
            }
        } else {
            // malformed header line - skip or consider bad request? Keep original behavior: continue
        }

        // advance pos beyond "\r\n" if present
        if (line_end + 2 <= total && data[line_end] == '\r' && data[line_end + 1] == '\n') {
            pos = line_end + 2;
        } else {
            pos = line_end;
        }
    }

    return ParseResult::OK;
}

// Modified: parse_body avoids extra checks and reuses content_length_ precisely.
// Keeps same observable behavior: checks body.size() matches content_length_
ParseResult HttpRequestParser::parse_body(std::string_view body, HttpRequest &req) {
    if (body.size() != content_length_) {
        return ParseResult::BAD_REQUEST;
    }

    req.set_body(std::string(body)); // POST 表单数据等

    // 解析表单数据（如果Content-Type是application/x-www-form-urlencoded）
    std::string content_type = req.get_header("Content-Type");
    if (content_type.find("application/x-www-form-urlencoded") != std::string::npos) {
        // Reuse optimized parse_form_data by passing std::string
        parse_form_data(std::string(body), req);
    }

    return ParseResult::OK;
}
