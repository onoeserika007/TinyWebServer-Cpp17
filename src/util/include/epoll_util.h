//
// Created by inory on 10/29/25.
//

#ifndef EPOLL_UTIL_H
#define EPOLL_UTIL_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/epoll.h>

namespace EpollUtil {
    int setNonBlocking(int fd);

    void addFdRead(int epoll_fd, int fd, bool one_shot, bool edge_trig);

    void removeFd(int epoll_fd, int fd);

    void modFd(int epoll_fd, int fd, int ev, bool edge_trig);
} // namespace EpollUtil

#endif //EPOLL_UTIL_H
