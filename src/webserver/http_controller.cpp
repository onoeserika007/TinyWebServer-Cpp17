//
// Created by inory on 10/29/25.
//

#include "http_controller.h"
#include "http_request.h"
#include "http_response.h"
#include "logger.h"
#include "user_service.h"
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>

#ifndef PROJECT_ROOT_DIR
#define PROJECT_ROOT_DIR "."
#endif

namespace {
    const char* const kDocRoot = PROJECT_ROOT_DIR "/root";
}

namespace HttpController {
    void echo(const HttpRequest& req, HttpResponse& resp) {
        resp.set_body("<h1>Echo</h1><pre>" + req.body() + "</pre>");
    }

    void hello(const HttpRequest& req, HttpResponse& resp) {
        resp.set_status(HttpStatus::OK);
        resp.set_body("Hello, World!");
    }

    void serveStaticFile(const HttpRequest& req, HttpResponse& resp) {
        std::string filepath = std::string(kDocRoot) + req.uri();
        LOG_INFO("[HttpController] Serving static file: {}", filepath);
        resp.set_file(filepath);
    }

    // 处理注册请求
    void handleRegister(const HttpRequest& req, HttpResponse& resp) {
        // 对于GET请求，显示注册页面
        if (req.method() == HttpRequest::Method::GET) {
            LOG_INFO("[HttpController] Showing Register page.");
            resp.set_file(kDocRoot + std::string("/register.html"));
            return;
        }

        auto username = req.get_form_field("user");
        auto password = req.get_form_field("password");

        LOG_INFO("[HttpController] Handle register request, username: {}, password: {}", username, password);

        if (username.empty() || password.empty()) {
            resp.set_status(HttpStatus::BAD_REQUEST);
            resp.set_body("Username and password are required");
            return;
        }

        if (UserService::Instance().userExists(username)) {
            resp.set_file(kDocRoot + std::string("/registerError.html"));
            return;
        }

        if (UserService::Instance().registerUser(username, password)) {
            // 注册成功后重定向到登录页面
            resp.set_status(HttpStatus::FOUND);
            resp.add_header("Location", "/login");
        } else {
            resp.set_file(kDocRoot + std::string("/registerError.html"));
        }

    }

    // 处理登录请求
    void handleLogin(const HttpRequest& req, HttpResponse& resp) {
        // 对于GET请求，显示登录页面
        // noinspection
        if (req.method() == HttpRequest::Method::GET) {
            LOG_INFO("[HttpController] Showing login page.");
            resp.set_file(kDocRoot + std::string("/log.html"));
            return;
        }

        auto username = req.get_form_field("user");
        auto password = req.get_form_field("password");

        LOG_INFO("[HttpController] Handle login request, username: {}, password: {}", username, password);

        if (username.empty() || password.empty()) {
            resp.set_status(HttpStatus::BAD_REQUEST);
            resp.set_body("Username and password are required");
            return;
        }

        if (UserService::Instance().verifyUser(username, password)) {
            // 登录成功后重定向到欢迎页面
            resp.set_status(HttpStatus::FOUND);
            resp.add_header("Location", "/welcome");
        } else {
            resp.set_file(kDocRoot + std::string("/logError.html"));
        }
    }

    // 主页（判断页面）
    void showJudgePage(const HttpRequest& req, HttpResponse& resp) {
        LOG_INFO("[HttpController] Showing judge page.");
        resp.set_file(kDocRoot + std::string("/judge.html"));
    }
    
    // 首页欢迎页面
    void showWelcomePage(const HttpRequest& req, HttpResponse& resp) {
        LOG_INFO("[HttpController] Showing welcome page.");
        resp.set_file(kDocRoot + std::string("/welcome.html"));
    }
    
    // 展示图片页面
    void showPicturePage(const HttpRequest& req, HttpResponse& resp) {
        LOG_INFO("[HttpController] Showing picture page.");
        resp.set_file(kDocRoot + std::string("/picture.html"));
    }

    // 展示视频页面
    void showVideoPage(const HttpRequest& req, HttpResponse& resp) {
        LOG_INFO("[HttpController] Showing video page.");
        resp.set_file(kDocRoot + std::string("/video.html"));
    }

}