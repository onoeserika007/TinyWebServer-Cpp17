//
// Created by inory on 11/20/25.
//

#ifndef INPUT_BUFFER_SYNC_H
#define INPUT_BUFFER_SYNC_H

#include <vector>

class InputBufferSync {
private:
    static constexpr size_t BUFFER_SIZE = 4096;
    std::vector<char> buffer_;
    size_t read_end_; // 已经接收到的数据末尾位置

public:
    InputBufferSync() : buffer_(BUFFER_SIZE), read_end_(0) {}

    InputBufferSync(InputBufferSync &) = delete;
    InputBufferSync &operator=(const InputBufferSync &) = delete;
    InputBufferSync(InputBufferSync &&other) noexcept = default;
    InputBufferSync &operator=(InputBufferSync &&other) noexcept = default;

    // 获取可写指针（供 recv 使用）
    char *write_ptr() { return buffer_.data() + read_end_; }

    // 剩余空间
    size_t writable_bytes() const { return buffer_.size() - read_end_; }

    // 当前已接收数据大小
    size_t readable_bytes() const { return read_end_; }

    // 底层数据指针
    const char *data() const { return buffer_.data(); }

    // 扩展已写入范围
    void has_written(size_t bytes);

    // 标记消费了 n 字节（如解析完一个请求后）
    void retrieve(size_t len);

    void reset() { read_end_ = 0; }

    bool read_from(int fd, bool use_edge_trigger = false, bool graceful_closing = false);

private:
    static bool check_peer_fin(int fd); // 优雅关闭时检查对端 FIN
};

#endif //INPUT_BUFFER_SYNC_H
