#ifndef WEBSERVER_STATIC_FILE_CONTROLLER_H
#define WEBSERVER_STATIC_FILE_CONTROLLER_H

#include "http_request.h"
#include "http_response.h"
#include "mime_types.h"
#include "logger.h"
#include <string>
#include <filesystem>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <regex>

class HttpRequest;
class HttpResponse;

class StaticFileController {
public:
    // 处理静态文件请求（供路由系统调用的接口）
    static void serveStaticFile(const HttpRequest& req, HttpResponse& resp);
    
    // 处理静态文件请求（内部实现）
    static void serveFile(const std::string& filepath, HttpResponse& resp);

private:
    static constexpr const char* kDocRoot = PROJECT_ROOT_DIR "/root";

    // 解析 HTTP 日期格式
    static std::time_t parseHttpDate(const std::string& date) {
        std::tm tm = {};
        std::istringstream ss(date);
        ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
        return std::mktime(&tm);
    }

    // 生成 ETag
    static std::string generateETag(const std::filesystem::file_time_type& mtime, 
                                  std::uintmax_t size) {
        auto timePoint = std::chrono::clock_cast<std::chrono::system_clock>(mtime);
        auto timestamp = std::chrono::system_clock::to_time_t(timePoint);
        return "\"" + std::to_string(timestamp) + "-" + std::to_string(size) + "\"";
    }

    // 处理范围请求
    static void handleRangeRequest(const std::string& rangeHeader, 
                                 std::uintmax_t fileSize,
                                 const std::string& filepath,
                                 const HttpRequest& req,
                                 HttpResponse& resp);
};

#endif // WEBSERVER_STATIC_FILE_CONTROLLER_H