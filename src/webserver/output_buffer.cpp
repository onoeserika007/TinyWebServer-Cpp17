//
// Created by inory on 10/29/25.
//

#include "output_buffer.h"
#include <cerrno>
#include <string>
#include <cstring>
#include "logger.h"

#include <sys/uio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/mman.h>

void OutputBuffer::reset() {
    bytes_have_sent_ = 0;
    bytes_to_send_ = 0;
    response_bound_ = 0;
    iov_count_ = 0;
    file_address_ = nullptr;
    should_unmap_ = false;
    std::memset(iov_, 0, sizeof(iov_));
}

void OutputBuffer::set_response(const char* response_data, size_t response_len,
                                const char* file_addr, size_t file_size) {
    unmap_if_needed();

    iov_[0].iov_base = const_cast<char*>(response_data);
    iov_[0].iov_len = response_len;
    response_bound_ = response_len;

    if (file_size > 0 && file_addr != nullptr) {
        iov_[1].iov_base = const_cast<char*>(file_addr);
        iov_[1].iov_len = file_size;
        iov_count_ = 2;
    } else {
        iov_count_ = 1;
    }

    bytes_to_send_ = response_len + file_size;
    bytes_have_sent_ = 0;
    file_address_ = const_cast<char*>(file_addr);
    should_unmap_ = (file_addr != nullptr);
}

void OutputBuffer::set_simple_response(const char* data, size_t len) {
    set_response(data, len, nullptr, 0);
}

WriteResult OutputBuffer::write_to(int fd) {
    // 让epoll去调度，只写一次很重要，否则原来那样缓冲区满了就干等
    // 这里 QPS 从2500提升到了15000
    if (bytes_to_send_ == 0) {
        return WriteResult::SUCCESS;
    }

    ssize_t n = writev(fd, iov_, iov_count_);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return WriteResult::CONTINUE; // 非致命，稍后重试
        }
        return WriteResult::ERROR; // 真正的 I/O 错误
    }

    bytes_have_sent_ += n;
    bytes_to_send_ -= n;

    // --- 核心逻辑：保持原始判断 ---
    if (bytes_have_sent_ >= response_bound_) {
        // 头部不再发送
        iov_[0].iov_len = 0;

        size_t file_bytes_sent = bytes_have_sent_ - response_bound_;
        iov_[1].iov_base = file_address_ + file_bytes_sent;
        iov_[1].iov_len = bytes_to_send_;
    } else {
        char* base = static_cast<char*>(iov_[0].iov_base);
        iov_[0].iov_base = base + bytes_have_sent_;
        iov_[0].iov_len = response_bound_ - bytes_have_sent_;
    }

    return bytes_to_send_ > 0 ? WriteResult::CONTINUE : WriteResult::SUCCESS;
}

void OutputBuffer::unmap_if_needed() {
    if (should_unmap_ && file_address_) {
        munmap(file_address_, static_cast<size_t>(iov_[1].iov_len));
        file_address_ = nullptr;
        should_unmap_ = false;
    }
}

// 移动语义支持
OutputBuffer::OutputBuffer(OutputBuffer&& other) noexcept
    : iov_{other.iov_[0], other.iov_[1]}
    , iov_count_(other.iov_count_)
    , bytes_have_sent_(other.bytes_have_sent_)
    , bytes_to_send_(other.bytes_to_send_)
    , response_bound_(other.response_bound_)
    , file_address_(other.file_address_)
    , should_unmap_(other.should_unmap_) {
    other.reset();
    other.file_address_ = nullptr;
    other.should_unmap_ = false;
}

OutputBuffer& OutputBuffer::operator=(OutputBuffer&& other) noexcept {
    if (this != &other) {
        unmap_if_needed();
        iov_[0] = other.iov_[0];
        iov_[1] = other.iov_[1];
        iov_count_ = other.iov_count_;
        bytes_have_sent_ = other.bytes_have_sent_;
        bytes_to_send_ = other.bytes_to_send_;
        response_bound_ = other.response_bound_;
        file_address_ = other.file_address_;
        should_unmap_ = other.should_unmap_;

        other.reset();
        other.file_address_ = nullptr;
        other.should_unmap_ = false;
    }
    return *this;
}
