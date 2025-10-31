//
// Created by inory on 10/29/25.
//

#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <string>
#include <string_view>
#include <optional>
#include <algorithm>
#include <cctype>

// 核心请求对象前向声明
class HttpRequest;

enum class ParseResult {
    OK,
    INCOMPLETE,
    BAD_REQUEST,
    FORBIDDEN,
    NOT_FOUND,
    INTERNAL_ERROR
};

/**
 * @brief 高性能、零拷贝 HTTP 请求解析器
 *        支持 GET/POST, keep-alive, Content-Length, Host 等基础字段
 */
class HttpRequestParser {
public:
    auto parse(std::string_view input, HttpRequest& request) -> ParseResult;
    void reset();
    auto consumed_bytes() -> size_t { return consumed_bytes_; }

private:
    enum class State {
        RequestLine,
        Headers,
        Body,
        Done,
        Invalid
    };

    State state_ = State::RequestLine;
    size_t consumed_bytes_ = 0;      // 已处理字节数
    size_t body_bytes_received_ = 0; // 已接收的 body 字节数
    size_t content_length_ = 0;

    // helper funcs
    // 不区分大小写的字符串比较
    static auto iequals(std::string_view a, std::string_view b) -> bool;

    // 按第一个分隔符分割（如 "Host: localhost" → ["Host", "localhost"]）
    static auto split_by_first(std::string_view str, char delim) -> std::pair<std::string_view, std::string_view>;

    // 删除前后空白字符
    static auto trim(std::string_view sv) -> std::string_view;

    // 解析表单数据
    static void parse_form_data(const std::string& body, HttpRequest& req);

    auto parse_request_line(std::string_view line, HttpRequest& req) -> ParseResult;
    auto parse_headers(std::string_view headers, HttpRequest& req) -> ParseResult;
    auto parse_body(std::string_view body, HttpRequest& req) -> ParseResult;
};

#endif //HTTP_PARSER_H