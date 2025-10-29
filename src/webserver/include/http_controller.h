//
// Created by inory on 10/29/25.
//

#ifndef HTTP_CONTROLLER_H
#define HTTP_CONTROLLER_H

class HttpRequest;
class HttpResponse;

namespace HttpController {
    void echo(const HttpRequest& req, HttpResponse& resp);
    void hello(const HttpRequest& req, HttpResponse& resp);
}

#endif //HTTP_CONTROLLER_H
