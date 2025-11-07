//
// Created by inory on 10/28/25.
//

#include <iostream>
#include "webserver.h"

int main() {
    try {
        EpollServer server("0.0.0.0", 8080);
        server.eventloop();
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
