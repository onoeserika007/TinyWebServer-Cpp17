#include "static_file_controller.h"
#include <filesystem>
#include <regex>
#include <fstream>

void StaticFileController::serveStaticFile(const HttpRequest& req, HttpResponse& resp) {
    LOG_INFO("[StaticFileController] Handling url {}", req.uri());
    std::string filepath = std::string(kDocRoot) + req.uri();
    
    // 规范化路径，防止目录遍历攻击
    std::error_code ec;
    std::filesystem::path normalizedPath = std::filesystem::canonical(filepath, ec);
    if (ec || !normalizedPath.string().starts_with(kDocRoot)) {
        resp.set_status(HttpStatus::FORBIDDEN);
        resp.set_body("Access denied");
        LOG_INFO("[StaticFileController] Access deny, url {} ", req.uri());
        return;
    }

    // 检查文件是否存在且可访问
    if (!std::filesystem::exists(filepath)) {
        resp.set_status(HttpStatus::NOT_FOUND);
        resp.set_body("File not found");
        LOG_INFO("[StaticFileController] File not found, url {} ", req.uri());
        return;
    }

    // 获取文件信息
    auto fileSize = std::filesystem::file_size(filepath, ec);
    if (ec) {
        resp.set_status(HttpStatus::INTERNAL_ERROR);
        resp.set_body("Failed to get file info");
        LOG_INFO("[StaticFileController] Failed to get file info, url {} ", req.uri());
        return;
    }

    // 处理部分内容请求（Range）
    if (auto it = req.headers().find("Range"); it != req.headers().end()) {
        handleRangeRequest(it->second, fileSize, filepath, req, resp);
        return;
    }

    // 设置 Content-Type
    auto mimeType = MimeTypes::getMimeType(filepath);
    resp.add_header("Content-Type", std::string(mimeType));

    // 设置文件
    resp.set_file(filepath);
    
    // 只有在需要时才添加缓存相关头
    // 处理条件请求（If-Modified-Since，If-None-Match）
    auto lastModified = std::filesystem::last_write_time(filepath, ec);
    if (!ec) {
        auto lastModifiedTime = std::chrono::system_clock::to_time_t(
            std::chrono::clock_cast<std::chrono::system_clock>(lastModified)
        );
        char timeBuf[100];
        std::strftime(timeBuf, sizeof(timeBuf), "%a, %d %b %Y %H:%M:%S GMT", 
                     std::gmtime(&lastModifiedTime));
        resp.add_header("Last-Modified", timeBuf);

        // 处理 If-Modified-Since
        if (auto it = req.headers().find("If-Modified-Since"); it != req.headers().end()) {
            std::time_t ifModifiedSince = parseHttpDate(it->second);
            if (ifModifiedSince >= lastModifiedTime) {
                resp.set_status(HttpStatus::NOT_MODIFIED);
                return;
            }
        }
        
        // 设置 ETag（简单实现，实际应该基于文件内容）
        std::string etag = generateETag(lastModified, fileSize);
        resp.add_header("ETag", etag);

        // 处理 If-None-Match
        if (auto it = req.headers().find("If-None-Match"); it != req.headers().end()) {
            if (it->second == etag) {
                resp.set_status(HttpStatus::NOT_MODIFIED);
                return;
            }
        }
        
        // 启用缓存控制
        resp.add_header("Cache-Control", "public, max-age=3600");
    }
    
    // 添加 Accept-Ranges 头表明服务器支持范围请求
    resp.add_header("Accept-Ranges", "bytes");
}

void StaticFileController::serveFile(const std::string& filepath, HttpResponse& resp) {
    // 检查文件是否存在且可访问
    std::error_code ec;
    if (!std::filesystem::exists(filepath, ec)) {
        resp.set_status(HttpStatus::NOT_FOUND);
        resp.set_body("File not found");
        return;
    }

    // 获取文件信息
    auto fileSize = std::filesystem::file_size(filepath, ec);
    if (ec) {
        resp.set_status(HttpStatus::INTERNAL_ERROR);
        resp.set_body("Failed to get file info");
        return;
    }

    // 设置 Content-Type
    auto mimeType = MimeTypes::getMimeType(filepath);
    resp.add_header("Content-Type", std::string(mimeType));
    
    // 设置文件
    resp.set_file(filepath);
}

void StaticFileController::handleRangeRequest(const std::string& rangeHeader, 
                                             std::uintmax_t fileSize,
                                             const std::string& filepath,
                                             const HttpRequest& req,
                                             HttpResponse& resp) {
    // 解析范围请求头
    std::smatch matches;
    std::regex rangeRegex(R"(bytes=(\d*)-(\d*))");
    if (!std::regex_match(rangeHeader, matches, rangeRegex)) {
        resp.set_status(HttpStatus::BAD_REQUEST);
        resp.set_body("Invalid range format");
        return;
    }

    // 解析范围值
    std::string startStr = matches[1].str();
    std::string endStr = matches[2].str();
    
    size_t start = startStr.empty() ? 0 : std::stoull(startStr);
    size_t end = endStr.empty() ? fileSize - 1 : std::stoull(endStr);

    // 验证范围
    if (start >= fileSize || end >= fileSize || start > end) {
        resp.set_status(HttpStatus::REQUESTED_RANGE_NOT_SATISFIABLE);
        resp.add_header("Content-Range", "bytes */" + std::to_string(fileSize));
        return;
    }

    // 设置部分内容响应
    resp.set_status(HttpStatus::PARTIAL_CONTENT);
    resp.add_header("Content-Range", 
        "bytes " + std::to_string(start) + "-" + 
        std::to_string(end) + "/" + std::to_string(fileSize));
    
    // 设置 Content-Type
    auto mimeType = MimeTypes::getMimeType(filepath);
    resp.add_header("Content-Type", std::string(mimeType));
    
    // 对于范围请求，我们需要提供特定范围的数据
    // 通过设置 Accept-Ranges 头来表明服务器支持范围请求
    resp.add_header("Accept-Ranges", "bytes");
    
    // 设置 Content-Length 为范围大小
    size_t rangeSize = end - start + 1;
    resp.add_header("Content-Length", std::to_string(rangeSize));
    
    // 使用文件映射方式处理范围请求，避免在内存中加载整个文件
    resp.set_file_with_range(filepath, start, rangeSize);
}