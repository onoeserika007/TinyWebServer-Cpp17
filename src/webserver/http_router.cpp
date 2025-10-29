//
// Created by inory on 10/29/25.
//

#include "http_router.h"
#include "http_request.h"
#include "http_response.h"

void HttpRouter::get(std::string path, HttpHandlerFunc handler) {
    routes_[std::move(path)] = Route{std::move(handler), false};
}

void HttpRouter::post(std::string path, HttpHandlerFunc handler) {
    routes_[std::move(path)] = Route{std::move(handler), true};
}

bool HttpRouter::match(const HttpRequest& req, HttpResponse& resp) const {
    auto it = routes_.find(req.uri());
    if (it != routes_.end()) {
        bool is_post = req.method() == HttpRequest::Method::POST;
        if (is_post != it->second.is_post) {
            resp.set_status(HttpStatus::BAD_REQUEST);
            resp.set_body("Method not allowed");
            return true;
        }
        it->second.handler(req, resp);
        return true;
    }
    return false;
}