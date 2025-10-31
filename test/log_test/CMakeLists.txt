find_package(Threads REQUIRED)

add_executable(log_test_complete log_test_complete.cpp)
target_link_libraries(log_test_complete base_lib pthread)

add_executable(log_test_performance log_test_performance.cpp)
target_link_libraries(log_test_performance base_lib pthread Threads::Threads stdc++)