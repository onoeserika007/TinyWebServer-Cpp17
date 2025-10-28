
add_executable(epoll_server main.cpp)

target_link_libraries(epoll_server webserver_lib)