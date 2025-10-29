//
// Created by inory on 10/29/25.
//

#include "http_controller.h"
#include "http_request.h"
#include "http_response.h"

namespace HttpController {
    void echo(const HttpRequest& req, HttpResponse& resp) {
        resp.set_body("<h1>Echo</h1><pre>" + req.body() + "</pre>");
    }

    void hello(const HttpRequest& req, HttpResponse& resp) {
        resp.set_status(HttpStatus::OK);
        resp.set_body("Hello, World!");
    }
}