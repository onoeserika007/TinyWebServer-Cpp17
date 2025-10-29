//
// Created by inory on 10/29/25.
//

#include "epoll_util.h"
#include "logger.h"
#include <cstring>


namespace EpollUtil {
    int setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) {
            throw std::runtime_error("fcntl failed");
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            throw std::runtime_error("fcntl failed to set O_NONBLOCK");
        }
        return flags;
    }

    void addFdRead(int epoll_fd, int fd, bool one_shot, bool edge_trig) {
        // 注册客户端 fd 时只监听读事件, 此时写事件被触发会引起问题
        epoll_event ev{};
        ev.data.fd = fd;
        ev.events = EPOLLIN | EPOLLRDHUP; // 关注读写事件
        if (edge_trig) {
            ev.events |= EPOLLET;
        }

        if (one_shot) {
            ev.events |= EPOLLONESHOT;
        }

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
            LOG_ERROR("Failed to add client socket to epoll: errno=%d, error: %s", errno, strerror(errno));
            close(fd);
        }
    }

    void removeFd(int epoll_fd, int fd) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, 0);
        close(fd);
    }

    void modFd(int epoll_fd, int fd, int ev, bool edge_trig) {
        epoll_event event {};
        event.data.fd = fd;
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

        if (edge_trig) {
            event.events |= EPOLLET;
        }

        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
    }

}