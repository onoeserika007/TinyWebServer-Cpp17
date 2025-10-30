add_executable(mysql_pool_test mysql_pool_test.cpp)
target_link_libraries(mysql_pool_test
    util_lib
    base_lib
)