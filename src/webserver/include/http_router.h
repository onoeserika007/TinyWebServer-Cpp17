//
// Created by inory on 10/29/25.
//

#ifndef HTTP_ROUTER_H
#define HTTP_ROUTER_H

#include <unordered_map>
#include <string>
#include <functional>

#include <functional>
#include <unordered_map>
#include <string>

class HttpRequest;
class HttpResponse;

using HttpHandlerFunc = std::function<void(const HttpRequest&, HttpResponse&)>;

class HttpRouter {
public:
    static HttpRouter& instance() {
        static HttpRouter r;
        return r;
    }

    void get(std::string path, HttpHandlerFunc handler);
    void post(std::string path, HttpHandlerFunc handler);

    bool match(const HttpRequest& req, HttpResponse& resp) const;

    HttpRouter(const HttpRouter&) = delete;
    HttpRouter& operator=(const HttpRouter&) = delete;

    void RegisterRoutes();

private:
    HttpRouter() = default;

    struct Route {
        HttpHandlerFunc get_handler;
        HttpHandlerFunc post_handler;
    };

    std::unordered_map<std::string, Route> routes_ {};
};


#endif //HTTP_ROUTER_H