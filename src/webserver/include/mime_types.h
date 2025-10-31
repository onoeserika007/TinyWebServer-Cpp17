#ifndef WEBSERVER_MIME_TYPES_H
#define WEBSERVER_MIME_TYPES_H

#include <string>
#include <string_view>
#include <unordered_map>
#include <filesystem>

namespace MimeTypes {
    // 获取文件扩展名对应的 MIME 类型
    inline std::string_view getMimeType(std::string_view path) {
        static const std::unordered_map<std::string_view, std::string_view> MIME_TYPES = {
            {".html", "text/html"},
            {".htm", "text/html"},
            {".css", "text/css"},
            {".js", "application/javascript"},
            {".json", "application/json"},
            {".png", "image/png"},
            {".jpg", "image/jpeg"},
            {".jpeg", "image/jpeg"},
            {".gif", "image/gif"},
            {".svg", "image/svg+xml"},
            {".ico", "image/x-icon"},
            {".mp4", "video/mp4"},
            {".webm", "video/webm"},
            {".mp3", "audio/mpeg"},
            {".wav", "audio/wav"},
            {".ogg", "audio/ogg"},
            {".pdf", "application/pdf"},
            {".xml", "application/xml"},
            {".zip", "application/zip"},
            {".ttf", "font/ttf"},
            {".woff", "font/woff"},
            {".woff2", "font/woff2"}
        };

        // 从路径中获取扩展名
        auto pos = path.rfind('.');
        if (pos != std::string_view::npos) {
            std::string_view ext = path.substr(pos);
            // 正常使用 C++17 的 if-init 语句，我们的编译器支持
            if (auto it = MIME_TYPES.find(ext); it != MIME_TYPES.end()) {
                return it->second;
            }
        }

        // 默认返回 octet-stream
        return std::string_view("application/octet-stream");
    }

    // 检查文件类型是否支持
    inline bool isSupportedType(std::string_view path) {
        return getMimeType(path) != "application/octet-stream";
    }

    // 检查是否是文本类型
    inline bool isTextType(std::string_view mimeType) {
        return mimeType.starts_with("text/") || 
               mimeType == "application/javascript" ||
               mimeType == "application/json" ||
               mimeType == "application/xml";
    }
}

#endif // WEBSERVER_MIME_TYPES_H