//
// Created by inory on 11/20/25.
//

#include "webserver/sync/input_buffer_sync.h"
#include "io_fiber.h"
#include "serika/basic/logger.h"


bool InputBufferSync::check_peer_fin(int fd) {
    char buf[1];
    ssize_t n = fiber::IO::recv(fd, buf, sizeof(buf), MSG_PEEK).value_or(-1);

    if (n == 0) {
        // 收到 FIN，正常关闭
        LOG_DEBUG("[InputBufferSync] Peer sent FIN, graceful close complete");
        return false;
    }

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 对端还未发送 FIN，继续等待
            return true;
        }
        // 其他错误（如 ECONNRESET）：客户端粗暴关闭，直接清理
        LOG_DEBUG("[InputBufferSync] Peer aborted connection: {}", strerror(errno));
        return false;
    }

    // n > 0：客户端在关闭过程中又发送了数据（违反协议），忽略并关闭
    LOG_WARN("[InputBufferSync] Peer sent data during graceful close, ignoring");
    return false;
}

void InputBufferSync::has_written(size_t bytes) {
    if (bytes > writable_bytes()) {
        LOG_ERROR("[InputBuffer] Overflow in has_written: {:zu}", bytes);
        return;
    }
    read_end_ += bytes;
}

void InputBufferSync::retrieve(size_t len) {
    if (len >= read_end_) {
        reset();
    } else {
        std::memmove(buffer_.data(), buffer_.data() + len, read_end_ - len);
        read_end_ -= len;
    }
}

bool InputBufferSync::read_from(int fd, bool use_edge_trigger, bool graceful_closing) {
    // 优雅关闭状态：只检查对端 FIN
    if (graceful_closing) {
        // LOG_INFO("Checking peer FIN");
        return check_peer_fin(fd);
    }

    // 正常读取
    if (readable_bytes() == buffer_.size()) {
        LOG_ERROR("[InputBufferSync] Buffer full, cannot read more.");
        return false;
    }

    ssize_t n = 0;
    if (!use_edge_trigger) {
        n = fiber::IO::recv(fd, write_ptr(), writable_bytes(), 0).value_or(-1);
    } else {
        // LOG_INFO("Reading from fd:{}", fd);
        n = fiber::IO::recv_et(fd, write_ptr(), writable_bytes(), 0).value_or(-1);
    }

    if (n > 0) {
        has_written(n);
        return true;
    }

    if (n == 0) {
        LOG_INFO("read_from recv 0, closing.");
    }

    return n == 0 ? false : (errno == EAGAIN || errno == EWOULDBLOCK);
}
