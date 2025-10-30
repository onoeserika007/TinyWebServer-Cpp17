# 获取源文件目录的绝对路径
get_filename_component(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)

aux_source_directory(${SRC_DIR} SRC_LIST)

# Build shared library
add_library(base_lib SHARED ${SRC_LIST})

# Make the include directory available to users of this library
target_include_directories(base_lib 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link with jsoncpp and pthread
target_link_libraries(base_lib 
    PUBLIC 
        jsoncpp_static 
        pthread
)