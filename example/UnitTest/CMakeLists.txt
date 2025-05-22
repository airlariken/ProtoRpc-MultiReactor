#创建服务端可执行文件
add_executable(UnitTest_ServiceDiscovery UnitTest_ServiceDiscovery.cc)

#链接必要的库，尤其是example中生成的静态库
target_link_libraries(UnitTest_ServiceDiscovery krpc_core ${LIBS})

# 设置编译选项
target_compile_options(UnitTest_ServiceDiscovery PRIVATE -std=c++11 -Wall)

# 设置 client 可执行文件输出目录
set_target_properties(UnitTest_ServiceDiscovery PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)





#获取protobuf生成的.cc
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../*.pb.cc)

#创建服务端可执行文件
add_executable(Client2 RpcClient.cc ${PROTO_SRCS})


#链接必要的库，尤其是example中生成的静态库
target_link_libraries(Client2 krpc_core ${LIBS})

# 设置编译选项
target_compile_options(Client2 PRIVATE -std=c++11 -Wall)

# 设置 client 可执行文件输出目录
set_target_properties(Client2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)





#获取protobuf生成的.cc
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../*.pb.cc)

#创建服务端可执行文件
add_executable(Server2 MyRpcServerImp.cc ${PROTO_SRCS})


#链接必要的库，尤其是example中生成的静态库
#target_include_directories(Server2 PUBLIC ../muduo)
target_link_libraries(Server2 PUBLIC krpc_core  ${LIBS})


# 设置编译选项
target_compile_options(Server2 PRIVATE -std=c++11 -Wall)

# 设置 client 可执行文件输出目录
set_target_properties(Server2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)