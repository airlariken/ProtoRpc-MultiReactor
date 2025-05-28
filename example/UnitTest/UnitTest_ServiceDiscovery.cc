// archive not used
// Created by orange on 4/30/25.
#include <iostream>
#include <cstdlib>
#include "ServiceDiscovery.h"
#include "Zookeeperutil.h"
#include "Krpcapplication.h"

int main(int argc, char* argv[]) {
//    try {
        KrpcApplication::Init(argc, argv);

        // 1. 初始化 ServiceDiscovery
        ServiceDiscovery::instance().init();

        // 2. 从命令行获取 service 和 method，或使用默认值
        std::string serviceName = (argc > 3) ? argv[3] : "TestService";
        std::string methodName  = (argc > 4) ? argv[4] : "TestMethod";

        // 3. 测试 pickHost
        std::string cachedHost = ServiceDiscovery::instance().pickHost(serviceName, methodName);
        std::cout << "[pickHost] " << serviceName << "/" << methodName
                  << " -> " << cachedHost << std::endl;

        // 4. 测试直接查询（fallback QueryServiceHost）
        ZkClient zkClient;
        zkClient.Start();

        int idx = 0;
        std::string directHost = ServiceDiscovery::instance().QueryServiceHost(&zkClient, serviceName, methodName, idx);
        std::cout << "[QueryServiceHost] "<<serviceName<<'/'<<methodName<<"\t idx=" << idx << ", host=" << directHost << std::endl;
//    } catch (const std::exception& ex) {
//        std::cerr << "Exception: " << ex.what() << std::endl;
//        return EXIT_FAILURE;
//    }

    return EXIT_SUCCESS;
}