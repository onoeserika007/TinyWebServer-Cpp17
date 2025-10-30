add_executable(config_test config_test.cpp)
target_link_libraries(config_test
    util_lib
    base_lib
)

target_include_directories(config_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/util/include
    ${CMAKE_SOURCE_DIR}/src/base/include
)