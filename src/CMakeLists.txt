# 除了可以直接使用CMAKE_CURRENT_SOURCE_DIR的相对路径，也可以使用get_filename_component来获取当前目录的绝对路径
#这样就可以保证CMake 的某些操作可能需要使用绝对路径，尤其是跨目录的构建场景。绝对路径可以避免相对路径带来的混淆和潜在问题。
#动态调整目录结构：如果项目目录层次较深，或者使用子模块或外部依赖，绝对路径可以方便地定位正确的文件，而不用担心路径拼接或依赖 CMake 的当前工作目录。

#示例：
#get_filename_component(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)
#file(GLOB_RECURSE SRC_FILES ${SRC_DIR}/*.cc)
#获取当前目录下的所有源文件
file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

## --- Proto 文件 & 生成 ---
## 1. 找到所有 .proto
#file(GLOB PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.proto)
#message(STATUS "Found proto files: ${PROTO_FILES}")
## 2. 生成 .pb.cc 和 .pb.h
#set(GENERATED_PROTO_SRCS "")
#set(GENERATED_PROTO_HDRS "")
#
#foreach(proto ${PROTO_FILES})
#    # 提取文件名和目录
#    get_filename_component(proto_name ${proto} NAME_WE)
#    get_filename_component(proto_dir  ${proto} DIRECTORY)
#
#    # 输出路径：.cc 放回 proto 同目录，.h 放入 ../include
#    set(proto_src ${proto_dir}/${proto_name}.pb.cc)
#    set(proto_hdr ${proto_dir}/../include/${proto_name}.pb.h)
#
#    # 确保 include 目录存在
#    file(MAKE_DIRECTORY ${proto_dir}/../include)
#
#    add_custom_command(
#        OUTPUT ${proto_src} ${proto_hdr}
#        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
#            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
#            --cpp_out=${proto_dir}            # .cc/.h 默认都生成到此处
#            ${proto}
#        COMMAND ${CMAKE_COMMAND} -E
#            copy ${proto_dir}/${proto_name}.pb.h ${proto_hdr}
#            # protoc 生成的 .pb.h 先落在 proto_dir，再拷贝到 ../include
#        DEPENDS ${proto}
#        COMMENT "Generating ${proto_name}.pb.cc and copying header to include/"
#        VERBATIM
#    )
#
#    list(APPEND GENERATED_PROTO_SRCS ${proto_src})
#    list(APPEND GENERATED_PROTO_HDRS ${proto_hdr})
#endforeach()
## --- Proto 文件 & 生成 ---

# 获取 protobuf 的生成文件
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.pb.cc)
#include_directories(../muduo)  # 根据你的目录结构调整
#创建静态库或共享库
add_library(krpc_core STATIC ${SRC_FILES} ${PROTO_SRCS})


#链接protobuf库
target_link_libraries(krpc_core PUBLIC ${LIBS})

#设置头文件的路径
target_include_directories(krpc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 添加编译选项
target_compile_options(krpc_core PRIVATE -std=c++11 -Wall)