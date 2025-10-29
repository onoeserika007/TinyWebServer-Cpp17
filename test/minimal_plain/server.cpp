//
// Created by inory on 10/28/25.
//

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080 // 监听端口

// 处理客户端请求并发送响应
void handle_request(int client_fd) {
    char buffer[1024];

    // 读取客户端请求
    int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        std::cerr << "Failed to read from client\n";
        close(client_fd);
        return;
    }
    buffer[bytes_read] = '\0'; // 确保字符串结尾为 '\0'

    // 简单的 HTTP 请求解析（这里只是识别 GET 请求）
    if (bytes_read > 0) {
        std::cout << "Received request:\n" << buffer << std::endl;

        // 构造一个 HTTP 响应
        std::string response = "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/html\r\n"
                               "Content-Length: 13\r\n"
                               "\r\n"
                               "<h1>Hello</h1>";

        // 发送 HTTP 响应
        write(client_fd, response.c_str(), response.length());
    }

    // 关闭客户端连接
    close(client_fd);
}

int main() {
    // 创建服务器 socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    // 配置服务器地址
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT); // 设置端口
    server_addr.sin_addr.s_addr = INADDR_ANY; // 绑定所有网络接口

    // 绑定 socket 和地址
    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind socket\n";
        close(server_fd);
        return 1;
    }

    // 监听端口
    if (listen(server_fd, 5) < 0) {
        std::cerr << "Failed to listen on socket\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Server is listening on port " << PORT << "...\n";

    // 进入接收和处理客户端请求的循环
    while (true) {
        // 接受客户端连接
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            std::cerr << "Failed to accept client connection\n";
            continue; // 继续等待下一个连接
        }

        // 处理客户端请求
        handle_request(client_fd);
    }

    // 关闭服务器 socket
    close(server_fd);
    return 0;
}
