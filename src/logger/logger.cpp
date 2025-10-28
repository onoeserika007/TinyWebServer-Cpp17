//
// Created by inory on 10/28/25.
//


#include "logger.h"

#include <ctime>
#include <iostream>
#include <sys/time.h>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <fstream>       // For modern file stream operations
#include <memory>        // For std::unique_ptr (RAII resource management)
#include <ostream>       // For std::ostream interface

Logger& Logger::Instance() {
    static Logger inst;
    return inst;
}

Logger::Logger()
    : m_buf_size_(8192),
      m_rotate_bytes_(50 * 1024 * 1024),
      m_close_log_(0),
      m_file_stream_(nullptr),  // Replaces FILE* with RAII-managed ofstream
      m_written_bytes_(0),
      m_today_(0),
      m_max_queue_size_(0),
      m_running_(false),
      m_is_async_(true),
      m_output_stream_(nullptr) // Unified interface for file/stderr output
{}

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
    // stop worker, flush remaining logs
    if (m_is_async_) {
        m_running_.store(false);
        m_cv_not_empty_.notify_all();
        if (m_worker_.joinable()) m_worker_.join();
    }
    // flush and close file (RAII-managed stream replaces manual fclose)
    std::lock_guard<std::mutex> lk(m_file_mutex_);
    if (m_file_stream_ && m_file_stream_->is_open()) {
        m_file_stream_->flush();
        m_file_stream_->close();
        m_file_stream_.reset();  // Automatically releases resources
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
    // called under file mutex by writer
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    time_t tt = tv.tv_sec;
    struct tm tm_now;
    localtime_r(&tt, &tm_now);  // Replaced localtime with thread-safe localtime_r (POSIX)

    bool need_rotate = false;
    if (tm_now.tm_mday != m_today_) {
        need_rotate = true;
        m_today_ = tm_now.tm_mday;
    }
    if (m_rotate_bytes_ > 0 && m_written_bytes_ >= m_rotate_bytes_) {
        need_rotate = true;
    }
    if (!need_rotate) return;

    // close current, open new with new name
    if (m_file_stream_ && m_file_stream_->is_open()) {
        m_file_stream_->flush();
        m_file_stream_->close();
    }

    std::string fname = GenerateLogFileName();
    // Reinitialize file stream for new log file
    m_file_stream_ = std::make_unique<std::ofstream>(fname, std::ios::app | std::ios::binary);
    if (!m_file_stream_->is_open()) {
        // redirect to stderr
        m_output_stream_ = &std::cerr;
    } else {
        m_output_stream_ = m_file_stream_.get();
    }
    m_current_name_ = fname;
    m_written_bytes_ = 0;
}

void Logger::WriteToFile(const std::string& msg) {
    std::lock_guard<std::mutex> lk(m_file_mutex_);
    RotateIfNeeded();
    if (!m_output_stream_) return;
    size_t to_write = msg.size();
    // Write to stream (replaces fwrite with C++ stream operation)
    m_output_stream_->write(msg.data(), to_write);
    if (m_output_stream_->good()) {  // Check stream state instead of return value
        m_written_bytes_ += to_write;
    }
    // flush periodically: here do not flush every write for performance; user can call flush in Shutdown.
    // but flush if stderr
    if (m_output_stream_ == &std::cerr) m_output_stream_->flush();  // Replaces fflush
}

void Logger::WorkerThread() {
    // background consumer: pop from queue and write
    while (m_running_.load() || !m_queue_.empty()) {
        std::unique_lock<std::mutex> lk(m_queue_mutex_);
        m_cv_not_empty_.wait(lk, [this]() { return !m_running_.load() || !m_queue_.empty(); });
        while (!m_queue_.empty()) {
            std::string msg = std::move(m_queue_.front());
            m_queue_.pop_front();
            lk.unlock();
            // write outside queue lock
            WriteToFile(msg);
            lk.lock();
            // notify producers waiting for space
            if (m_max_queue_size_ > 0) m_cv_not_full_.notify_one();
        }
    }
    // ensure flush at end
    std::lock_guard<std::mutex> lk_file(m_file_mutex_);
    if (m_file_stream_ && m_file_stream_->is_open()) {
        m_file_stream_->flush();  // Replaces fflush
    }
}

void Logger::Log(LogLevel level, const char* fmt, ...) {
    if (m_close_log_) return;

    // format message using vsnprintf into local buffer
    va_list args;
    va_start(args, fmt);

    std::string formatted;
    formatted.resize(m_buf_size_);
    int n = vsnprintf(&formatted[0], (int)formatted.size(), fmt, args);
    va_end(args);

    if (n < 0) {
        return;
    }
    if ((size_t)n >= formatted.size()) {
        // need bigger buffer
        formatted.resize(n + 1);
        va_list args2;
        va_start(args2, fmt);
        vsnprintf(&formatted[0], formatted.size(), fmt, args2);
        va_end(args2);
    }
    formatted.resize(n);

    // compose final message with timestamp and newline
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    std::string prefix = MakeTimePrefix(tv, level);
    std::string final_msg;
    final_msg.reserve(prefix.size() + formatted.size() + 2);
    final_msg += prefix;
    final_msg += formatted;
    final_msg += '\n';

    if (m_is_async_) {
        // push to queue (blocking if full)
        std::unique_lock<std::mutex> lk(m_queue_mutex_);
        if (m_max_queue_size_ > 0) {
            m_cv_not_full_.wait(lk, [this](){ return m_queue_.size() < m_max_queue_size_ || !m_running_.load(); });
        }
        m_queue_.emplace_back(std::move(final_msg));
        lk.unlock();
        m_cv_not_empty_.notify_one();
    } else {
        // synchronous path - directly write
        WriteToFile(final_msg);
    }
}