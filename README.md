# CMake规范

根目录的cmake只负责项目标准制定和架构，具体的构建选项全部由子目录负责
子目录cmake编写时遵循现有规范，如
``` cmake
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
```

注意：测试程序只需要链接相应的库（如util_lib、base_lib），不需要显式添加include目录，
因为库的target_include_directories设置会自动传播包含路径。

```cmake
# 测试程序只需要链接库，不需要显式添加include目录
add_executable(config_test config_test.cpp)
target_link_libraries(config_test
    util_lib  # 自动获取util/include路径
    base_lib  # 自动获取base/include路径
)
```

获取当前目录全部源文件，不要显式添加include dir，include dir应该通过target_include_directories添加给子模块，其他使用的地方通过链接静态库自动获取include dir

cmake构建使用cmake --build . -j32

所有测试的运行都默认在根目录

检测到代码和缓存不一致，以磁盘内容为准，因为我可能有手动修改