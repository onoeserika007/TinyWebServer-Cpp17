//
// Created by inory on 10/28/25.
//


#include "logger.h"

#include <ctime>
#include <format>
#include <iostream>
#include <sys/time.h>
#include <vector>
#include <iomanip>
#include <cstring>
#include <fstream>
#include <memory>
#include <ostream>

Logger& Logger::Instance() {
    static Logger inst;
    return inst;
}

Logger::Logger():
    m_buf_size_(8192),
    m_rotate_bytes_(50 * 1024 * 1024),
    m_close_log_(0),
    m_file_stream_(nullptr),  // Replaces FILE* with RAII-managed ofstream
    m_written_bytes_(0),
    m_today_(0),
    m_max_queue_size_(0),
    m_running_(false),
    m_is_async_(true),
    m_output_stream_(nullptr), // Unified interface for file/stderr output
    m_batch_flush_threshold_(4096) // 4 KB
{

}

Logger::~Logger() {
    Shutdown();
}

bool Logger::Init(std::string file_name,
                  bool async,
                  size_t max_queue_size,
                  size_t buf_size,
                  size_t rotate_bytes,
                  int close_log) {
    m_base_name_ = std::move(file_name);
    m_is_async_ = async;
    m_max_queue_size_ = max_queue_size;
    m_buf_size_ = buf_size;
    m_rotate_bytes_ = rotate_bytes;
    m_close_log_ = close_log;

    // open file
    std::lock_guard<std::mutex> lk(m_file_mutex_);
    // compute initial name
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t tt = tv.tv_sec;
    struct tm tm_now;
    localtime_r(&tt, &tm_now);  // Replaced localtime with thread-safe localtime_r (POSIX)
    m_today_ = tm_now.tm_mday;

    std::string fname = GenerateLogFileName();
    // Initialize file stream with append mode (replaces fopen("a"))
    m_file_stream_ = std::make_unique<std::ofstream>(fname, std::ios::app | std::ios::binary);
    if (!m_file_stream_->is_open()) {
        // fallback to stderr
        fprintf(stderr, "Logger: failed to open log file %s, fallback to stderr\n", fname.c_str());
        m_output_stream_ = &std::cerr;
    } else {
        // Get current file size (replaces fseek + ftell)
        m_file_stream_->seekp(0, std::ios::end);
        m_written_bytes_ = static_cast<size_t>(m_file_stream_->tellp());
        if (m_written_bytes_ == static_cast<size_t>(-1)) m_written_bytes_ = 0;
        m_output_stream_ = m_file_stream_.get();
    }

    m_current_name_ = fname;

    if (m_is_async_ && m_max_queue_size_ > 0) {
        m_running_.store(true);
        m_worker_ = std::thread(&Logger::WorkerThread, this);
    } else {
        m_is_async_ = false;
    }
    return true;
}

void Logger::Shutdown() {
    // 停止消费者线程
    if (m_is_async_) {
        m_running_.store(false);
        if (m_worker_.joinable()) {
            m_worker_.join();
        }
    }

    // 刷空批量缓冲区
    std::lock_guard<std::mutex> lk(m_file_mutex_);
    if (!m_batch_buf_.empty() && m_output_stream_) {
        m_output_stream_->write(m_batch_buf_.data(), m_batch_buf_.size());
        m_written_bytes_ += m_batch_buf_.size();
        m_batch_buf_.clear();
    }

    // 关闭文件流
    if (m_file_stream_ && m_file_stream_->is_open()) {
        m_file_stream_->flush();
        m_file_stream_->close();
        m_file_stream_.reset();
        m_output_stream_ = nullptr;
    }
}

std::string Logger::MakeTimePrefix(struct timeval tv, LogLevel level) {
    time_t t = tv.tv_sec;
    struct tm tm_now;
    localtime_r(&t, &tm_now);  // Replaced localtime with thread-safe localtime_r (POSIX)
    char buf[64];
    const char* lvl = "[INFO]:";
    switch (level) {
        case LogLevel::DEBUG: lvl = "[DEBUG]:"; break;
        case LogLevel::INFO: lvl = "[INFO]:"; break;
        case LogLevel::WARN: lvl = "[WARN]:"; break;
        case LogLevel::ERROR: lvl = "[ERROR]:"; break;
    }
    int len = snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                       tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                       tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, tv.tv_usec, lvl);
    return std::string(buf, (len>0?len:0));
}

std::string Logger::GenerateLogFileName() {
    // Generate log file name in format: basename_YYYY_MM_DD_HH_MM_SS.log (precision to second)
    time_t current_time = time(nullptr);
    // Thread-safety note: Using localtime_r instead of localtime for thread safety (POSIX)
    // For Windows, replace with localtime_s to maintain thread safety
    struct tm current_tm;
    localtime_r(&current_time, &current_tm);
    char filename_buf[256];

    // Format specifiers:
    snprintf(filename_buf, sizeof(filename_buf), "%s_%04d_%02d_%02d_%02d_%02d_%02d.log",
             m_base_name_.c_str(),
             current_tm.tm_year + 1900,
             current_tm.tm_mon + 1,
             current_tm.tm_mday,
             current_tm.tm_hour,
             current_tm.tm_min,
             current_tm.tm_sec);

    return {filename_buf};
}

void Logger::RotateIfNeeded() {
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    time_t tt = tv.tv_sec;
    struct tm tm_now;
    localtime_r(&tt, &tm_now);

    bool need_rotate = false;
    if (tm_now.tm_mday != m_today_) {
        need_rotate = true;
        m_today_ = tm_now.tm_mday;
    }
    if (m_rotate_bytes_ > 0 && m_written_bytes_ >= m_rotate_bytes_) {
        need_rotate = true;
    }
    if (!need_rotate) return;

    // 轮转前刷空批量缓冲区
    if (!m_batch_buf_.empty() && m_output_stream_) {
        m_output_stream_->write(m_batch_buf_.data(), m_batch_buf_.size());
        m_written_bytes_ += m_batch_buf_.size();
        m_batch_buf_.clear();
    }

    // 关闭旧文件，打开新文件
    if (m_file_stream_ && m_file_stream_->is_open()) {
        m_file_stream_->flush();
        m_file_stream_->close();
    }

    std::string fname = GenerateLogFileName();
    m_file_stream_ = std::make_unique<std::ofstream>(fname, std::ios::app | std::ios::binary);
    if (!m_file_stream_->is_open()) {
        m_output_stream_ = &std::cerr;
    } else {
        m_output_stream_ = m_file_stream_.get();
    }
    m_current_name_ = fname;
    m_written_bytes_ = 0;
}

void Logger::WriteToFile(const std::string& msg) {
    std::lock_guard<std::mutex> lk(m_file_mutex_);
    if (!m_output_stream_) return;

    // 日志追加到批量缓冲区
    m_batch_buf_ += msg;

    // 满足阈值或输出到stderr时刷盘（stderr需实时输出）
    if (m_batch_buf_.size() >= m_batch_flush_threshold_ || m_output_stream_ == &std::cerr) {
        RotateIfNeeded(); // 刷盘前检查是否需要轮转
        m_output_stream_->write(m_batch_buf_.data(), m_batch_buf_.size());
        if (m_output_stream_->good()) {
            m_written_bytes_ += m_batch_buf_.size();
        }
        m_batch_buf_.clear(); // 清空缓冲区
    }
}

void Logger::WorkerThread() {
    std::vector<std::string> batch_msgs;
    batch_msgs.reserve(32); // 预分配32条日志的空间，减少扩容开销

    while (m_running_.load() || !m_queue_.empty()) {
        batch_msgs.clear();

        // 批量出队（自旋锁保护，减少锁竞争次数）
        {
            std::lock_guard<SpinLock> lk(m_queue_lock_); // 自旋锁加锁
            while (!m_queue_.empty() && batch_msgs.size() < 32) {
                batch_msgs.emplace_back(std::move(m_queue_.front()));
                m_queue_.pop_front();
            }
        } // 自动解锁

        // 批量写入文件（复用批量刷盘逻辑）
        if (!batch_msgs.empty()) {
            for (const auto& msg : batch_msgs) {
                WriteToFile(msg);
            }
        } else {
            // 无日志时yield CPU，减少空转消耗
            std::this_thread::yield();
        }
    }

    // 线程退出前刷空队列剩余日志
    std::string remaining_msg;
    {
        std::lock_guard<SpinLock> lk(m_queue_lock_);
        while (!m_queue_.empty()) {
            remaining_msg = std::move(m_queue_.front());
            m_queue_.pop_front();
            WriteToFile(remaining_msg);
        }
    }
}