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
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <fcntl.h>

void OutputBuffer::reset() {
    unmap_if_needed();
    close_file_if_needed();
    
    bytes_have_sent_ = 0;
    bytes_to_send_ = 0;
    response_bound_ = 0;
    iov_count_ = 0;
    file_address_ = nullptr;
    mmap_size_ = 0;
    should_unmap_ = false;
    file_offset_ = 0;
    file_remain_ = 0;
    use_sendfile_ = false;
    std::memset(iov_, 0, sizeof(iov_));
}

void OutputBuffer::set_response_with_mmap(const char* response_data, size_t response_len,
                                          const std::string& file_path, size_t file_offset, size_t file_size) {
    unmap_if_needed();
    close_file_if_needed();
    use_sendfile_ = false;

    iov_[0].iov_base = const_cast<char*>(response_data);
    iov_[0].iov_len = response_len;
    response_bound_ = response_len;
    bytes_to_send_ = response_len;
    bytes_have_sent_ = 0;

    // 如果有文件，进行 mmap
    if (!file_path.empty() && file_size > 0) {
        int fd = open(file_path.c_str(), O_RDONLY);
        if (fd < 0) {
            LOG_ERROR("Failed to open file for mmap: {}", file_path);
            iov_count_ = 1;
            return;
        }

        // mmap 的 offset 必须页对齐（通常 4KB）
        static const size_t PAGE_SIZE = sysconf(_SC_PAGE_SIZE);  // 通常是 4096
        size_t aligned_offset = (file_offset / PAGE_SIZE) * PAGE_SIZE;  // 向下对齐到页边界
        size_t offset_in_page = file_offset - aligned_offset;  // 页内偏移
        size_t map_size = offset_in_page + file_size;  // 需要 map 的总大小

        void* addr = mmap(
            nullptr, // 建议映射起始位置
            map_size, // 映射字节数
            PROT_READ, // PROT_WRITE | PROT_EXEC（加载可执行代码（如 .so 库）） | PROT_NONE
            MAP_PRIVATE, // MAP_PRIVATE 私有写时复制，修改不影响原文件 | MAP_SHARED 共享映射，修改会同步到文件，用于进程间通信、修改文件 | MAP_ANONYMOUS
            fd,
            aligned_offset // 文件内偏移量
            );
        close(fd);  // mmap 后可关闭 fd

        if (addr == MAP_FAILED) {
            LOG_ERROR("mmap failed: {} (offset={}, aligned={}, map_size={})", 
                      strerror(errno), file_offset, aligned_offset, map_size);
            iov_count_ = 1;
            return;
        }

        file_address_ = static_cast<char*>(addr);
        mmap_size_ = map_size;  // 保存对齐后的大小用于 munmap
        should_unmap_ = true;

        // iov[1] 指向实际需要发送的数据（跳过页内偏移）
        iov_[1].iov_base = static_cast<char*>(addr) + offset_in_page;
        iov_[1].iov_len = file_size;
        iov_count_ = 2;
        bytes_to_send_ += file_size;
    } else {
        iov_count_ = 1;
    }
}

void OutputBuffer::set_response_with_sendfile(const char* response_data, size_t response_len,
                                              const std::string& file_path, size_t file_offset, size_t file_size) {
    unmap_if_needed();
    close_file_if_needed();
    use_sendfile_ = true;

    iov_[0].iov_base = const_cast<char*>(response_data);
    iov_[0].iov_len = response_len;
    response_bound_ = response_len;
    bytes_to_send_ = response_len;
    bytes_have_sent_ = 0;
    iov_count_ = 1;

    // 如果有文件，打开 fd 准备 sendfile
    if (!file_path.empty() && file_size > 0) {
        file_fd_ = open(file_path.c_str(), O_RDONLY);
        if (file_fd_ < 0) {
            LOG_ERROR("Failed to open file for sendfile: {}", file_path);
            return;
        }

        file_offset_ = static_cast<off_t>(file_offset);
        file_remain_ = file_size;
        bytes_to_send_ += file_size;
    }
}

WriteResult OutputBuffer::write_to(int fd) {
    if (bytes_to_send_ == 0) {
        return WriteResult::SUCCESS;
    }

    // 根据模式选择不同的发送方式
    if (use_sendfile_) {
        LOG_DEBUG("[OutputBuffer] Sending using sendfile.");
        // sendfile 模式
        // 1. 先发送 header
        if (bytes_have_sent_ < response_bound_) {
            char* base = static_cast<char*>(iov_[0].iov_base);
            size_t header_remain = response_bound_ - bytes_have_sent_;
            ssize_t n = write(fd, base + bytes_have_sent_, header_remain);
            
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return WriteResult::CONTINUE;
                }
                // 客户端主动断开
                if (errno == EPIPE || errno == ECONNRESET) {
                    LOG_DEBUG("[OutputBuffer] Client disconnected during header write");
                    return WriteResult::ERROR;
                }
                LOG_ERROR("[OutputBuffer] write error: {}", strerror(errno));
                return WriteResult::ERROR;
            }
            
            bytes_have_sent_ += n;
            bytes_to_send_ -= n;
            
            if (bytes_have_sent_ < response_bound_) {
                return WriteResult::CONTINUE;
            }
        }

        // 2. header 已发完，发送文件
        if (file_fd_ >= 0 && file_remain_ > 0) {
            ssize_t n = sendfile(fd, file_fd_, &file_offset_, file_remain_);
            
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return WriteResult::CONTINUE;
                }
                // 客户端主动断开（正常情况，如视频播放器获取足够数据后断开）
                if (errno == EPIPE || errno == ECONNRESET) {
                    LOG_DEBUG("[OutputBuffer] Client disconnected during sendfile (EPIPE/ECONNRESET), errno={}", errno);
                    return WriteResult::ERROR;
                }
                LOG_ERROR("[OutputBuffer] sendfile error: {}", strerror(errno));
                return WriteResult::ERROR;
            }
            
            file_remain_ -= n;
            bytes_to_send_ -= n;
            bytes_have_sent_ += n;
        }
    } else {
        LOG_DEBUG("[OutputBuffer] Sending using mmap.");
        // mmap + writev 模式
        ssize_t n = writev(fd, iov_, iov_count_);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return WriteResult::CONTINUE;
            }
            // 客户端主动断开（正常情况，如视频播放器获取足够数据后断开）
            if (errno == EPIPE || errno == ECONNRESET) {
                LOG_DEBUG("[OutputBuffer] Client disconnected (EPIPE/ECONNRESET), errno={}", errno);
                return WriteResult::ERROR;
            }
            LOG_ERROR("[OutputBuffer] writev error: {}", strerror(errno));
            return WriteResult::ERROR;
        }

        bytes_have_sent_ += n;
        bytes_to_send_ -= n;

        if (bytes_have_sent_ >= response_bound_) {
            iov_[0].iov_len = 0;
            size_t file_bytes_sent = bytes_have_sent_ - response_bound_;
            iov_[1].iov_base = static_cast<char*>(file_address_) + file_bytes_sent;
            iov_[1].iov_len = bytes_to_send_;
        } else {
            char* base = static_cast<char*>(iov_[0].iov_base);
            iov_[0].iov_base = base + bytes_have_sent_;
            iov_[0].iov_len = response_bound_ - bytes_have_sent_;
        }
    }

    return bytes_to_send_ > 0 ? WriteResult::CONTINUE : WriteResult::SUCCESS;
}

void OutputBuffer::unmap_if_needed() {
    if (should_unmap_ && file_address_) {
        munmap(file_address_, mmap_size_);  // 使用原始大小，而不是被修改的 iov_[1].iov_len，否则有可能造成内存泄露
        file_address_ = nullptr;
        mmap_size_ = 0;
        should_unmap_ = false;
    }
}

void OutputBuffer::close_file_if_needed() {
    if (file_fd_ >= 0) {
        close(file_fd_);
        file_fd_ = -1;
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
    , mmap_size_(other.mmap_size_)
    , should_unmap_(other.should_unmap_) {
    other.reset();
    other.file_address_ = nullptr;
    other.mmap_size_ = 0;
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
        mmap_size_ = other.mmap_size_;
        should_unmap_ = other.should_unmap_;

        other.reset();
        other.file_address_ = nullptr;
        other.mmap_size_ = 0;
        other.should_unmap_ = false;
    }
    return *this;
}
