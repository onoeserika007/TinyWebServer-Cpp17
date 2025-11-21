
add_executable(epoll_server main.cpp)

target_link_libraries(epoll_server webserver_lib fiber_lib)

add_executable(fiber_server fiber_main.cpp)

target_link_libraries(fiber_server webserver_lib fiber_lib)