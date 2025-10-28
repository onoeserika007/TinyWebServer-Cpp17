//
// Created by inory on 10/28/25.
//

#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <fstream>
#include <memory>
#include <cstdio>
#include <cstdarg>
#include <chrono>

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

class Logger {
public:
    // 获取单例
    static Logger& Instance();

    // 初始化：file_name (base name), async: 是否异步, max_queue_size: 异步队列长度(0 表示同步),
    // buf_size: 单条消息最大缓冲, rotate_bytes: 文件大小上限（0表示不按大小分割），close_log: 是否关闭日志输出（0: 启用）
    bool Init(std::string file_name,
              bool async = true,
              size_t max_queue_size = 10000,
              size_t buf_size = 8192,
              size_t rotate_bytes = 50 * 1024 * 1024,
              int close_log = 0);

    // 写日志（与经典API兼容，format采用printf样式）
    void Log(LogLevel level, const char* fmt, ...);

    // 手动flush并关闭（会等待后台写线程完成）
    void Shutdown();

    // 是否关闭日志（外部可读）
    bool IsClosed() const { return m_close_log_; }

    std::string GetCurrentLogName() const { return m_current_name_; }

    // 禁止拷贝
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();

    // 后台线程主函数
    void WorkerThread();

    // 将格式化的日志写入文件（只由后台线程或同步路径调用，外部应加锁或保证单线程）
    void WriteToFile(const std::string& msg);

    // 检查是否需要切分文件（按天或按大小）
    void RotateIfNeeded();

    // 格式化时间前缀
    std::string MakeTimePrefix(struct timeval tv, LogLevel level);

    // 生成新的日志文件名
    std::string GenerateLogFileName();

private:
    // 配置
    std::string m_base_name_;
    size_t m_buf_size_;
    size_t m_rotate_bytes_;
    int m_close_log_;

    // runtime
    std::string m_current_name_;

    // 文件句柄与相关计数
    std::unique_ptr<std::ofstream> m_file_stream_;
    std::ostream* m_output_stream_;
    size_t m_written_bytes_; // 当前文件已写字节数
    int m_today_;            // 当前日期（tm_mday）

    // 异步队列与线程
    std::deque<std::string> m_queue_;
    size_t m_max_queue_size_;
    std::mutex m_queue_mutex_;
    std::condition_variable m_cv_not_empty_;
    std::condition_variable m_cv_not_full_;
    std::thread m_worker_;
    std::atomic<bool> m_running_;
    bool m_is_async_;

    // 锁用于文件/旋转/flush等
    std::mutex m_file_mutex_;
};

//
// 方便宏（内部会检查是否关闭日志）
//
// 不用花括号是因为防止;打断if else逻辑
#define LOG_DEBUG(format, ...) \
    do { if (!Logger::Instance().IsClosed()) Logger::Instance().Log(LogLevel::DEBUG, format, ##__VA_ARGS__); } while(0)
#define LOG_INFO(format, ...) \
    do { if (!Logger::Instance().IsClosed()) Logger::Instance().Log(LogLevel::INFO, format, ##__VA_ARGS__); } while(0)
#define LOG_WARN(format, ...) \
    do { if (!Logger::Instance().IsClosed()) Logger::Instance().Log(LogLevel::WARN, format, ##__VA_ARGS__); } while(0)
#define LOG_ERROR(format, ...) \
    do { if (!Logger::Instance().IsClosed()) Logger::Instance().Log(LogLevel::ERROR, format, ##__VA_ARGS__); } while(0)

#endif // LOGGER_H

