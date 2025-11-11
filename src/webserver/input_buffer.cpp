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

bool InputBuffer::check_peer_fin(int fd) {
    char buf[1];
    ssize_t n = recv(fd, buf, sizeof(buf), MSG_PEEK);
    
    if (n == 0) {
        // 收到 FIN，正常关闭
        LOG_DEBUG("[InputBuffer] Peer sent FIN, graceful close complete");
        return false;
    }
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 对端还未发送 FIN，继续等待
            return true;
        }
        // 其他错误（如 ECONNRESET）：客户端粗暴关闭，直接清理
        LOG_DEBUG("[InputBuffer] Peer aborted connection: {}", strerror(errno));
        return false;
    }
    
    // n > 0：客户端在关闭过程中又发送了数据（违反协议），忽略并关闭
    LOG_WARN("[InputBuffer] Peer sent data during graceful close, ignoring");
    return false;
}

bool InputBuffer::read_from(int fd, bool use_edge_trigger, bool graceful_closing) {
    // 优雅关闭状态：只检查对端 FIN
    if (graceful_closing) {
        return check_peer_fin(fd);
    }
    
    // 正常读取
    if (readable_bytes() == buffer_.size()) {
        LOG_ERROR("[InputBuffer] Buffer full, cannot read more.");
        return false;
    }

    return use_edge_trigger ? read_et(fd) : read_lt(fd);
}