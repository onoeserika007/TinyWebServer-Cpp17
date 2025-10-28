//
// Created by inory on 10/28/25.
//

#include "webserver.h"
#include <iostream>

int main() {
  try {
    EpollServer server("127.0.0.1", 8080);
    server.start();
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
