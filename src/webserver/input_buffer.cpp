//
// Created by inory on 10/29/25.
//

#include "input_buffer.h"
#include "logger.h"

bool InputBuffer::read_lt(int fd) {
    ssize_t n = recv(fd, write_ptr(), writable_bytes(), 0);
    if (n > 0) {
        has_written(n);
        return true;
    }
    return n == 0 ? false : (errno == EAGAIN || errno == EWOULDBLOCK);
}

bool InputBuffer::read_et(int fd) {
    bool success = true;
    while (true) {
        ssize_t n = recv(fd, write_ptr(), writable_bytes(), 0);
        if (n > 0) {
            has_written(n);
            continue; // 继续读直到无数据
        }
        if (n == 0) {
            success = false; // 对端关闭
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break; // 正常退出
        } else {
            success = false;
            if (errno == ECONNRESET) {
                break;
            }
            LOG_ERROR("[InputBuffer] Read error: {:s}", strerror(errno));
            break;
        }
    }
    return success;
}

bool InputBuffer::read_from(int fd, bool use_edge_trigger) {
    if (readable_bytes() == buffer_.size()) {
        LOG_ERROR("[InputBuffer] Buffer full, cannot read more.");
        return false;
    }

    return use_edge_trigger ? read_et(fd) : read_lt(fd);
}