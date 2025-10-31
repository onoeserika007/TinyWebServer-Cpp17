//
// Created by inory on 10/29/25.
//

#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>
#include <unordered_map>

class HttpRequest {
public:
    enum class Method { GET, POST };

    void set_method(Method m) { method_ = m; }
    Method method() const { return method_; }

    void set_uri(std::string uri) { uri_ = std::move(uri); }
    const std::string &uri() const { return uri_; }

    void set_body(std::string body) { body_ = std::move(body); }
    const std::string &body() const { return body_; }

    void set_keep_alive(bool on) { keep_alive_ = on; }
    bool keep_alive() const { return keep_alive_; }

    void set_content_length(size_t n) { content_length_ = n; }
    size_t content_length() const { return content_length_; }

    void set_host(std::string host) { host_ = std::move(host); }
    const std::string &host() const { return host_; }

    void set_cgi(bool flag) { cgi_ = flag; }
    bool cgi() const { return cgi_; }

    void set_version(std::string version) { version_ = std::move(version); }
    const std::string &version() const { return version_; }

    // 获取所有请求头
    const std::unordered_map<std::string, std::string> &headers() const { return headers_; }
    // 设置请求头
    void add_header(std::string key, std::string value) { headers_[std::move(key)] = std::move(value); }
    // 获取特定请求头，如果不存在返回空字符串
    std::string get_header(const std::string &key) const {
        auto it = headers_.find(key);
        return it != headers_.end() ? it->second : "";
    }

    // 表单数据相关方法
    // 添加表单字段
    void add_form_field(std::string key, std::string value) { form_fields_[std::move(key)] = std::move(value); }

    // 获取表单字段
    std::string get_form_field(const std::string &key) const {
        auto it = form_fields_.find(key);
        return it != form_fields_.end() ? it->second : "";
    }

    // 获取所有表单字段
    const std::unordered_map<std::string, std::string> &form_fields() const { return form_fields_; }

private:
    Method method_;
    std::string uri_;
    std::string body_;
    std::string host_;
    std::string version_;
    bool keep_alive_ = false;
    size_t content_length_ = 0;
    bool cgi_ = false;
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> form_fields_; // 存储解析后的表单字段
};


#endif //HTTP_REQUEST_H