cmake_minimum_required(VERSION 3.5)
project(Krpc)

# 设置全局 C++ 标准及调试模式（如果需要调试，请将类型设置为 Debug）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_BUILD_TYPE Debug)  # 确保调试信息被生成

#设置头文件目录，供所有子项目使用
# 查找 protobuf 包
set(CMAKE_PREFIX_PATH "/usr/local/protobuf" ${CMAKE_PREFIX_PATH})
find_package(Protobuf REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

# 添加 muduo 子目录(假设 muduo 的 CMakeLists.txt 已编写好)
add_subdirectory(muduo)
include_directories(muduo)

# 添加包含目录：这里除了 protobuf 的头文件，还需要添加 muduo 的源码头文件目录
include_directories(${Protobuf_INCLUDE_DIRS})

#message(STATUS "Using muduo ${CMAKE_SOURCE_DIR}/muduo/include")
set(LIBS
    #    protobuf
    ${Protobuf_LIBRARIES}
    pthread
    zookeeper_mt
    muduo_net
    muduo_base
    glog
)
# 添加你的其他子目录
add_subdirectory(src)
add_subdirectory(example)