
#获取protobuf生成的.cc
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../*.pb.cc)

#创建服务端可执行文件
add_executable(Client RpcClient.cc ${PROTO_SRCS})


#链接必要的库，尤其是example中生成的静态库
target_link_libraries(Client krpc_core ${LIBS})

# 设置编译选项
target_compile_options(Client PRIVATE -std=c++11 -Wall)

# 设置 client 可执行文件输出目录
set_target_properties(Client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)


#获取protobuf生成的.cc
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../*.pb.cc)

#创建服务端可执行文件
add_executable(ClientWithReconn RpcClientWithReconn.cc ${PROTO_SRCS})


#链接必要的库，尤其是example中生成的静态库
#target_include_directories(Server2 PUBLIC ../muduo)
target_link_libraries(ClientWithReconn PUBLIC krpc_core  ${LIBS})


# 设置编译选项
target_compile_options(ClientWithReconn PRIVATE -std=c++11 -Wall)

# 设置 client 可执行文件输出目录
set_target_properties(ClientWithReconn PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)




#获取protobuf生成的.cc
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../*.pb.cc)

#创建服务端可执行文件
add_executable(Server MyRpcServerImp.cc ${PROTO_SRCS})


#链接必要的库，尤其是example中生成的静态库
#target_include_directories(Server2 PUBLIC ../muduo)
target_link_libraries(Server PUBLIC krpc_core  ${LIBS})


# 设置编译选项
target_compile_options(Server PRIVATE -std=c++11 -Wall)

# 设置 client 可执行文件输出目录
set_target_properties(Server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)



#获取protobuf生成的.cc
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../*.pb.cc)

#创建服务端可执行文件
add_executable(ClientConcc RpcClientConcc.cc ${PROTO_SRCS})


#链接必要的库，尤其是example中生成的静态库
#target_include_directories(Server2 PUBLIC ../muduo)
target_link_libraries(ClientConcc PUBLIC krpc_core  ${LIBS})


# 设置编译选项
target_compile_options(ClientConcc PRIVATE -std=c++11 -Wall)

# 设置 client 可执行文件输出目录
set_target_properties(ClientConcc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)