//
// Created by inory on 10/29/25.
//

#ifndef INPUT_BUFFER_H
#define INPUT_BUFFER_H

#include <unistd.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <vector>
#include "logger.h"

class InputBuffer {
private:
    static constexpr size_t BUFFER_SIZE = 4096;
    std::vector<char> buffer_;
    size_t read_end_; // 已经接收到的数据末尾位置

public:
    InputBuffer() : buffer_(BUFFER_SIZE), read_end_(0) {}

    // 获取可写指针（供 recv 使用）
    char* write_ptr() { return buffer_.data() + read_end_; }

    // 剩余空间
    size_t writable_bytes() const { return buffer_.size() - read_end_; }

    // 当前已接收数据大小
    size_t readable_bytes() const { return read_end_; }

    // 底层数据指针
    const char* data() const { return buffer_.data(); }

    // 扩展已写入范围
    void has_written(size_t bytes) {
        if (bytes > writable_bytes()) {
            LOG_ERROR("[InputBuffer] Overflow in has_written: {:zu}", bytes);
            return;
        }
        read_end_ += bytes;
    }

    // 标记消费了 n 字节（如解析完一个请求后）
    void retrieve(size_t len) {
        if (len >= read_end_) {
            clear();
        } else {
            std::memmove(buffer_.data(), buffer_.data() + len, read_end_ - len);
            read_end_ -= len;
        }
    }

    void clear() { read_end_ = 0; }

    // 真正执行读操作（LT/ET 模式在此区分）
    bool read_from(int fd, bool use_edge_trigger = false);

private:
    bool read_lt(int fd);
    bool read_et(int fd);
};

#endif //INPUT_BUFFER_H
