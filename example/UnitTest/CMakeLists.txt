#创建服务端可执行文件
add_executable(UnitTest_ServiceDiscovery UnitTest_ServiceDiscovery.cc)

#链接必要的库，尤其是example中生成的静态库
target_link_libraries(UnitTest_ServiceDiscovery krpc_core ${LIBS})

# 设置编译选项
target_compile_options(UnitTest_ServiceDiscovery PRIVATE -std=c++11 -Wall)

# 设置 client 可执行文件输出目录
set_target_properties(UnitTest_ServiceDiscovery PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)




# 查找 MySQL 客户端库（需要事先 apt install libmysqlclient-dev）
# 调用 mysql_config 获取编译选项
execute_process(
        COMMAND mysql_config --cflags
        OUTPUT_VARIABLE MYSQL_CFLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

# 调用 mysql_config 获取链接选项
execute_process(
        COMMAND mysql_config --libs
        OUTPUT_VARIABLE MYSQL_LIBS
        OUTPUT_STRIP_TRAILING_WHITESPACE
)


#创建服务端可执行文件
add_executable(UnitTest_MySQLConnPoolStressTest UnitTest_MySQLConnPoolStressTest.cc ${PROTO_SRCS})

target_include_directories(UnitTest_MySQLConnPoolStressTest PRIVATE ../MySQLServer)

#链接必要的库，尤其是example中生成的静态库
target_link_libraries(UnitTest_MySQLConnPoolStressTest ${LIBS} ${MYSQL_LIBS})

# 设置编译选项 将 MYSQL_CFLAGS 中的选项添加到全局编译选项中
target_compile_options(UnitTest_MySQLConnPoolStressTest PRIVATE -std=c++11 -Wall ${MYSQL_CFLAGS})

# 设置 client 可执行文件输出目录
set_target_properties(UnitTest_MySQLConnPoolStressTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)