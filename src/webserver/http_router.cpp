//
// Created by inory on 10/29/25.
//

#include "http_router.h"
#include "http_request.h"
#include "http_response.h"
#include "http_controller.h"
#include "static_file_controller.h"
#include <fnmatch.h>

void HttpRouter::get(std::string path, HttpHandlerFunc handler) {
    // 查找现有路由或创建新路由
    auto it = routes_.find(path);
    if (it != routes_.end()) {
        // 更新GET处理函数
        it->second.get_handler = std::move(handler);
    } else {
        // 创建新路由
        Route route{};
        route.get_handler = std::move(handler);
        routes_[std::move(path)] = std::move(route);
    }
}

void HttpRouter::post(std::string path, HttpHandlerFunc handler) {
    // 查找现有路由或创建新路由
    auto it = routes_.find(path);
    if (it != routes_.end()) {
        // 更新POST处理函数
        it->second.post_handler = std::move(handler);
    } else {
        // 创建新路由
        Route route{};
        route.post_handler = std::move(handler);
        routes_[std::move(path)] = std::move(route);
    }
}

bool HttpRouter::match(const HttpRequest &req, HttpResponse &resp) const {
    const std::string &uri = req.uri();

    // 首先尝试精确匹配
    auto it = routes_.find(uri);
    if (it != routes_.end()) {
        bool is_post = req.method() == HttpRequest::Method::POST;
        if (is_post && it->second.post_handler) {
            it->second.post_handler(req, resp);
            return true;
        } else if (!is_post && it->second.get_handler) {
            it->second.get_handler(req, resp);
            return true;
        } else {
            resp.set_status(HttpStatus::METHOD_NOT_ALLOWED);
            resp.set_body("Method not allowed");
            return true;
        }
    }

    // 尝试通配符匹配
    for (const auto &route: routes_) {
        // 检查是否是通配符路由
        if (route.first.ends_with("*")) {
            std::string prefix = route.first.substr(0, route.first.length() - 1);
            if (uri.starts_with(prefix)) {
                bool is_post = req.method() == HttpRequest::Method::POST;
                if (is_post && route.second.post_handler) {
                    route.second.post_handler(req, resp);
                    return true;
                } else if (!is_post && route.second.get_handler) {
                    route.second.get_handler(req, resp);
                    return true;
                } else {
                    resp.set_status(HttpStatus::METHOD_NOT_ALLOWED);
                    resp.set_body("Method not allowed");
                    return true;
                }
            }
        }
        // 检查是否是文件扩展名匹配（如 *.jpg）
        else if (route.first.starts_with("*.")) {
            std::string extension = route.first.substr(1); // 包含点号
            if (uri.ends_with(extension)) {
                bool is_post = req.method() == HttpRequest::Method::POST;
                if (is_post && route.second.post_handler) {
                    route.second.post_handler(req, resp);
                    return true;
                } else if (!is_post && route.second.get_handler) {
                    route.second.get_handler(req, resp);
                    return true;
                } else {
                    resp.set_status(HttpStatus::METHOD_NOT_ALLOWED);
                    resp.set_body("Method not allowed");
                    return true;
                }
            }
        }
    }

    return false;
}

void HttpRouter::RegisterRoutes() {
    auto& router = HttpRouter::instance();

    // 首页
    router.get("/", HttpController::showJudgePage);
    router.get("/index", HttpController::showJudgePage);
    router.get("/index.html", HttpController::showJudgePage);

    // 静态文件服务
    router.get("/static/*", HttpController::serveStaticFile);

    // 注册相关 - 同一路径处理GET和POST
    router.get("/register", HttpController::handleRegister);  // 显示注册页面或处理注册请求
    router.post("/register", HttpController::handleRegister);   // 处理注册请求

    // 登录相关 - 同一路径处理GET和POST
    router.get("/login", HttpController::handleLogin);    // 显示登录页面或处理登录请求
    router.post("/login", HttpController::handleLogin);     // 处理登录请求

    // 登录后的欢迎页面
    router.get("/welcome", HttpController::showWelcomePage);  // 欢迎页面
    router.get("/welcome.html", HttpController::showWelcomePage);  // 欢迎页面

    // 其他页面 - 业务逻辑路由
    router.get("/picture", HttpController::showPicturePage);  // 图片页
    router.get("/video", HttpController::showVideoPage);    // 视频页

    // 对应的页面处理 - 直接文件访问路由
    router.get("/picture.html", StaticFileController::serveStaticFile);  // 图片页
    router.get("/video.html", StaticFileController::serveStaticFile);      // 视频页
    router.get("/judge.html", StaticFileController::serveStaticFile);      // 判断页
    router.get("/log.html", StaticFileController::serveStaticFile);        // 登录页
    router.get("/register.html", StaticFileController::serveStaticFile); // 注册页
    router.get("/logError.html", StaticFileController::serveStaticFile);   // 登录错误页
    router.get("/registerError.html", StaticFileController::serveStaticFile); // 注册错误页

    // 静态资源文件
    router.get("*.jpg", StaticFileController::serveStaticFile);
    router.get("*.jpeg", StaticFileController::serveStaticFile);
    router.get("*.png", StaticFileController::serveStaticFile);
    router.get("*.gif", StaticFileController::serveStaticFile);
    router.get("*.mp4", StaticFileController::serveStaticFile);
    router.get("*.avi", StaticFileController::serveStaticFile);
    router.get("*.ico", StaticFileController::serveStaticFile);
    router.get("*.zip", StaticFileController::serveStaticFile);
}