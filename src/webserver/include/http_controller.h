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
    void serveStaticFile(const HttpRequest& req, HttpResponse& resp);

    // 处理注册请求（GET显示页面，POST处理表单）
    void handleRegister(const HttpRequest& req, HttpResponse& resp);
    
    // 处理登录请求（GET显示页面，POST处理表单）
    void handleLogin(const HttpRequest& req, HttpResponse& resp);
    
    // 主页（判断页面）
    void showJudgePage(const HttpRequest& req, HttpResponse& resp);
    
    // 首页欢迎页面
    void showWelcomePage(const HttpRequest& req, HttpResponse& resp);
    
    // 展示图片页面
    void showPicturePage(const HttpRequest& req, HttpResponse& resp);

    // 展示视频页面
    void showVideoPage(const HttpRequest& req, HttpResponse& resp);

    // 展示粉丝页面
    void showFansPage(const HttpRequest& req, HttpResponse& resp);
}

#endif //HTTP_CONTROLLER_H