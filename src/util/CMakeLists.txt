# 查找 MySQL 客户端库
find_library(MYSQLCLIENT_LIB mysqlclient REQUIRED)
if(NOT MYSQLCLIENT_LIB)
    message(FATAL_ERROR "mysqlclient library not found!")
endif()
message(STATUS "Found mysqlclient: ${MYSQLCLIENT_LIB}")

# 获取源文件目录的绝对路径
get_filename_component(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)

# 将该目录下所有源文件添加到 SRC_LIST 变量中
aux_source_directory(${SRC_DIR} SRC_LIST)

add_library(util_lib ${SRC_LIST})
target_include_directories(util_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(util_lib 
    base_lib
    ${MYSQLCLIENT_LIB}
    jsoncpp_static)