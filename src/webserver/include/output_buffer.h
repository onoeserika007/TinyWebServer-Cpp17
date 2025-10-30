//
// Created by inory on 10/29/25.
//

#ifndef OUTPUT_BUFFER_H
#define OUTPUT_BUFFER_H

#include <fcntl.h>


enum class WriteResult {
    SUCCESS,      // 数据已全部写完
    CONTINUE,     // 写未完成，需继续监听 EPOLLOUT
    ERROR         // 发生不可恢复错误（如 EPIPE），应关闭连接
};

class OutputBuffer {

public:
    OutputBuffer() = default;
    ~OutputBuffer() { unmap_if_needed(); }

    // 禁止拷贝，允许移动
    OutputBuffer(const OutputBuffer&) = delete;
    OutputBuffer& operator=(const OutputBuffer&) = delete;
    OutputBuffer(OutputBuffer&& other) noexcept;
    OutputBuffer& operator=(OutputBuffer&& other) noexcept;

    void set_close_on_done(bool close = true) { close_connection_ = close; }
    bool should_close() const { return close_connection_; }

    void reset();

    // 设置要发送的内容（由 HttpResponse 调用）
    void set_response(const char* response_data, size_t response_len,
                      const char* file_addr, size_t file_size);

    void set_simple_response(const char* data, size_t len);

    // 尝试写一次（只调用一次 writev）
    // 返回值：-1=错误，0=需要继续写，1=已全部发送完成
    WriteResult write_to(int fd);

    // 是否还有数据未发送？
    bool pending() const { return bytes_to_send_ > 0; }

    // 清理 mmap 资源
    void unmap_if_needed();

private:
    struct iovec iov_[2];
    int iov_count_ = 0;

    size_t bytes_have_sent_ = 0;     // 已发送总字节数
    size_t bytes_to_send_ = 0;       // 待发送总字节数
    size_t response_bound_ = 0;           // header 长度（即 iov[0].iov_len 初始值）

    void* file_address_ = nullptr;
    bool should_unmap_ = false;
    bool close_connection_ = false;
};


#endif //OUTPUT_BUFFER_H
