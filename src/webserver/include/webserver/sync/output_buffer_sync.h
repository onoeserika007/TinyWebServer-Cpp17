//
// Created by inory on 11/21/25.
//

#ifndef OUTPUT_BUFFER_SYNC_H
#define OUTPUT_BUFFER_SYNC_H

#include <fcntl.h>
#include <string>

class OutputBufferSync {

public:
    OutputBufferSync() = default;
    ~OutputBufferSync() {
        unmap_if_needed();
        close_file_if_needed();
    }

    // 禁止拷贝，允许移动
    OutputBufferSync(const OutputBufferSync &) = delete;
    OutputBufferSync &operator=(const OutputBufferSync &) = delete;
    OutputBufferSync(OutputBufferSync &&other) noexcept;
    OutputBufferSync &operator=(OutputBufferSync &&other) noexcept;

    void set_close_on_done(bool close = true) { close_connection_ = close; }
    bool should_close() const { return close_connection_; }

    void reset();

    // 统一的设置接口（应用层只传参数，I/O 层决定如何处理）
    void set_response_with_mmap(const char *response_data, size_t response_len, const std::string &file_path,
                                size_t file_offset, size_t file_size);

    void set_response_with_sendfile(const char *response_data, size_t response_len, const std::string &file_path,
                                    size_t file_offset, size_t file_size);

    // 尝试写一次
    void write_to(int fd);

    // 是否还有数据未发送？
    bool pending() const { return bytes_to_send_ > 0; }

    // 清理 mmap 资源
    void unmap_if_needed();

    // 清理sendfile文件描述符
    void close_file_if_needed();

private:
    struct iovec iov_[2];
    int iov_count_ = 0;

    size_t bytes_have_sent_ = 0; // 已发送总字节数
    size_t bytes_to_send_ = 0; // 待发送总字节数
    size_t response_bound_ = 0; // header 长度（即 iov[0].iov_len 初始值）

    // mmap 模式
    void *file_address_ = nullptr;
    size_t mmap_size_ = 0; // mmap 的原始大小，用于 munmap
    bool should_unmap_ = false;

    // sendfile 模式
    int file_fd_ = -1;
    off_t file_offset_ = 0;
    size_t file_remain_ = 0;

    bool use_sendfile_ = false; // true=sendfile, false=mmap
    bool close_connection_ = false;

    static std::atomic<uint64_t> send_times_;
};

#endif //OUTPUT_BUFFER_SYNC_H
