# 获取当前源文件所在目录的绝对路径
get_filename_component(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)

file(GLOB_RECURSE SRC_LIST
        "${SRC_DIR}/*.cpp"
        "${SRC_DIR}/*.c"
)

add_library(webserver_lib ${SRC_LIST})
target_link_libraries(webserver_lib base_lib util_lib fiber_lib)
target_include_directories(webserver_lib
PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)