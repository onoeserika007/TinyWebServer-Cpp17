//
// Created by inory on 10/29/25.
//

#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>

class HttpRequest {
public:
    enum class Method { GET, POST };

    void set_method(Method m) { method_ = m; }
    Method method() const { return method_; }

    void set_uri(std::string uri) { uri_ = std::move(uri); }
    const std::string& uri() const { return uri_; }

    void set_body(std::string body) { body_ = std::move(body); }
    const std::string& body() const { return body_; }

    void set_keep_alive(bool on) { keep_alive_ = on; }
    bool keep_alive() const { return keep_alive_; }

    void set_content_length(size_t n) { content_length_ = n; }
    size_t content_length() const { return content_length_; }

    void set_host(std::string host) { host_ = std::move(host); }
    const std::string& host() const { return host_; }

    void set_cgi(bool flag) { cgi_ = flag; }
    bool cgi() const { return cgi_; }

    void set_version(std::string version) { version_ = std::move(version); }
    const std::string& version() const { return version_; }

private:
    Method method_ {};
    std::string uri_;
    std::string body_;
    std::string host_;
    std::string version_;
    bool keep_alive_ = false;
    size_t content_length_ = 0;
    bool cgi_ = false;
};

#endif //HTTP_REQUEST_H
