//
// Created by inory on 10/28/25.
//

#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

#define SERVER_IP "127.0.0.1"  // 服务器地址
#define SERVER_PORT 8080      // 服务器端口

// 发送 HTTP 请求并接收响应
void send_request() {
  // 创建 socket
  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    std::cerr << "Failed to create socket\n";
    return;
  }

  // 配置服务器地址
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

  // 连接到服务器
  if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    std::cerr << "Failed to connect to server\n";
    close(client_fd);
    return;
  }

  // 构造一个 HTTP 请求
  std::string request = "GET / HTTP/1.1\r\n"
                        "Host: localhost:8080\r\n"
                        "Connection: close\r\n"
                        "\r\n";

  // 发送 HTTP 请求
  send(client_fd, request.c_str(), request.length(), 0);

  // 接收服务器响应
  char buffer[1024];
  int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
  if (bytes_received < 0) {
    std::cerr << "Failed to receive response\n";
  } else {
    buffer[bytes_received] = '\0';  // 确保字符串结尾为 '\0'
    std::cout << "Received response:\n" << buffer << std::endl;
  }

  // 关闭连接
  close(client_fd);
}

int main() {
  send_request();
  return 0;
}
