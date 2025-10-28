//
// Created by inory on 10/28/25.
//

#ifndef WEBSERVER_H
#define WEBSERVER_H


#include <string>

class EpollServer {
public:
  EpollServer(const std::string& host, int port);
  ~EpollServer();

  void eventloop();

private:
  void acceptConnections();
  void handleRead(int fd);
  void handleWrite(int fd);
  int setNonBlocking(int fd);

  void initLogger();
  void initEpoll();


private:
  int server_fd_;
  int epoll_fd_;
  std::string host_;
  int port_;
};


#endif //WEBSERVER_H
