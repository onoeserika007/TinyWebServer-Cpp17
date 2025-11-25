//
// Created by inory on 11/20/25.
//

// #define NDEBUG
#include <iostream>
#include <csignal>

#include "webserver/webserver.h"
#include "serika/basic/config_manager.h"
#include "scheduler.h"

// void handler(int sig) {
//     std::cout << "Caught SIGINT\n";
// }

FIBER_MAIN() {
    // std::signal(SIGINT, handler);

    try {
        ConfigManager& config_mgr = ConfigManager::Instance();
        auto host = config_mgr.get<std::string>("server.host", "127.0.0.1");
        auto port = config_mgr.get<int>("server.port", 8080);
        auto num_sub_reactor = config_mgr.get<int>("server.num_sub_reactor", 4);

        // 如果使用了shared_from_this，当然要保证第一个ref是存在的。所以这里必须使用shared_ptr
        auto server_ref = std::make_shared<EpollServer>(host, port, num_sub_reactor);
        server_ref->start();
        server_ref->acceptLoop();
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}