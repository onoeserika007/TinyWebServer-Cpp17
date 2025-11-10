//
// Created by inory on 10/28/25.
//

#include <iostream>

#include "config_manager.h"
#include "webserver.h"

int main() {
    try {
        ConfigManager& config_mgr = ConfigManager::Instance();
        auto host = config_mgr.get<std::string>("server.host", "127.0.0.1");
        auto port = config_mgr.get<int>("server.port", 8080);
        auto num_sub_reactor = config_mgr.get<int>("server.num_sub_reactor", 4);
        EpollServer server(host, port, num_sub_reactor);
        server.eventloop();
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
