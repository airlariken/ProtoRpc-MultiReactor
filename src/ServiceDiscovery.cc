//
// Created by orange on 4/30/25.
//
#include "ServiceDiscovery.h"

// 简单的 split 工具函数：按单一字符分割
static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) {
            elems.push_back(item);
        }
    }
    return elems;
}

ServiceDiscovery &ServiceDiscovery::instance() {
    static ServiceDiscovery inst;   //c++11线程安全
    return inst;
}

//旧版本
//void ServiceDiscovery::init() {
//    std::lock_guard<std::mutex> lock(cache_mutex_);
//    // 启动并连接到 ZooKeeper
//    zkClient_.Start();
//
//
//    // 获取所有服务节点，过滤系统节点
//    std::vector<std::string> services;
//    {
//        std::lock_guard<std::mutex> zklock(zk_mutex_);
//        services = zkClient_.GetChildren("/");
//    }
//    for (const auto& service : services) {
//        if (service == "zookeeper") continue;  // 跳过系统节点
//        std::string servicePath = "/" + service;
//        std::vector<std::string> methods;
//        {
//            std::lock_guard<std::mutex> zklock(zk_mutex_);
//            methods = zkClient_.GetChildren(servicePath.c_str());
//        }
//        for (const auto& method : methods) {
//            std::string methodPath = servicePath + "/" + method;
//            std::vector<std::string> addresses;
//            {
//                std::lock_guard<std::mutex> zklock(zk_mutex_);
//                addresses = zkClient_.GetChildren(methodPath.c_str());
//            }
//            std::vector<std::string> hostList;
//            if (addresses.empty()) {
//                // 如果没有子节点，则直接读取方法节点的数据
//                std::string hostData;
//                {
//                    std::lock_guard<std::mutex> zklock(zk_mutex_);
//                    hostData = zkClient_.GetData(methodPath.c_str());
//                }
//                if (!hostData.empty()) {
//                    hostList.push_back(hostData);
//                }
//            } else {
//                // 有子节点时，遍历每个实例节点
//                for (const auto& node : addresses) {
//                    std::string fullPath = methodPath + "/" + node;
//                    std::string hostData;
//                    {
//                        std::lock_guard<std::mutex> zklock(zk_mutex_);
//                        hostData = zkClient_.GetData(fullPath.c_str());
//                    }
//                    if (!hostData.empty()) {
//                        hostList.push_back(hostData);
//                    }
//                }
//            }
//            cache_[service + "/" + method] = std::move(hostList);
//        }
//    }
//}

// ServiceDiscovery.cpp
void ServiceDiscovery::init(const std::string& host, const std::string& port) {
    zkClient_.Start(host, port);

    // 列出根目录下所有 service
    struct String_vector sv;
    zkClient_.GetChildrenW("/", zkWatcher, this, &sv);

    for (int i = 0; i < sv.count; ++i) {
        std::string service = sv.data[i];
        if (service == "zookeeper") continue;

        std::string servicePath = "/" + service;
        struct String_vector mv;
        zkClient_.GetChildrenW(servicePath.c_str(), zkWatcher, this, &mv);

        for (int j = 0; j < mv.count; ++j) {
            std::string method = mv.data[j];
            std::string methodPath = servicePath + "/" + method;

            // 同时对节点数据和子节点都注册 watcher
            struct String_vector av;
            zkClient_.GetChildrenW(methodPath.c_str(), zkWatcher, this, &av);

            std::vector<std::string> hosts;
            if (av.count == 0) {
                char buf[128];
                int len = sizeof(buf);
                zkClient_.GetDataW(methodPath.c_str(), zkWatcher, this, buf, &len);
                hosts.emplace_back(buf, len);
            } else {
                for (int k = 0; k < av.count; ++k) {
                    std::string instPath = methodPath + "/" + av.data[k];
                    char buf[128];
                    int len = sizeof(buf);
                    zkClient_.GetDataW(instPath.c_str(), zkWatcher, this, buf, &len);
                    hosts.emplace_back(buf, len);
                }
            }

            std::lock_guard<std::mutex> lk(cache_mutex_);
            cache_[service + "/" + method] = std::move(hosts);
            deallocate_String_vector(&av);
        }
        deallocate_String_vector(&mv);
    }
    deallocate_String_vector(&sv);
}



std::string ServiceDiscovery::QueryServiceHost(ZkClient* zkclient,
                                               const std::string& service_name,
                                               const std::string& method_name,
                                               int &idx) {
    std::string methodPath = "/" + service_name + "/" + method_name;
    // 获取子节点列表
    std::vector<std::string> children;
    {
        std::lock_guard<std::mutex> lock(zk_mutex_);
        children = zkclient->GetChildren(methodPath.c_str());
    }
    if (children.empty()) {
        // 无子节点时，直接读取方法节点的数据
        std::lock_guard<std::mutex> lock(zk_mutex_);
        idx = -1;
        return zkclient->GetData(methodPath.c_str());
    }
    // 随机选择一个子节点
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    idx = std::rand() % children.size();
    std::string nodePath = methodPath + "/" + children[idx];
    std::lock_guard<std::mutex> lock(zk_mutex_);
    return zkclient->GetData(nodePath.c_str());
}



std::string ServiceDiscovery::pickHost(const std::string& service, const std::string& method) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    std::string key = service + "/" + method;
    auto it = cache_.find(key);
    if (it == cache_.end() || it->second.empty()) {
        throw std::runtime_error("No hosts found for key: " + key);
    }


    const auto& hosts = it->second;

    // 分布在 [0, hosts.size()-1]
    std::uniform_int_distribution<std::size_t> dist(0, hosts.size() - 1);
    // 随机选择一个实例,这种方法不太能在多线程真正随机
//    std::srand(static_cast<unsigned>(std::time(nullptr)));
//    int idx = std::rand() % hosts.size();
    std::size_t idx = dist(tls_rng);
    return hosts[idx];
}


void ServiceDiscovery::zkWatcher(zhandle_t* zh, int type,int state,const char* path, void* ctx) {
    if (type == ZOO_CHILD_EVENT || type == ZOO_CHANGED_EVENT) {
        LOG(INFO)<<"some server service/method is up/down";
        auto* self = static_cast<ServiceDiscovery*>(ctx);
        self->handleEvent(path, type);
    }
}


void ServiceDiscovery::handleEvent(const std::string& path, int type) {
    // path 类似 "/UserService/Login" 或 "/UserService/Login/instance123"
    // 解析出 service/method
    auto parts = split(path, '/');  // 自行实现 split
    if (parts.size() < 3) return;
    std::string service = parts[1];
    std::string method  = parts[2];
    std::string key     = service + "/" + method;

    // 重新拉一次子节点和数据（同 init 逻辑），更新 cache_
    std::vector<std::string> newHosts;
    struct String_vector av;
    zkClient_.GetChildrenW(path.c_str(), zkWatcher, this, &av);

    if (av.count == 0) {
        char buf[128]; int len = sizeof(buf);
        zkClient_.GetDataW(path.c_str(), zkWatcher, this, buf, &len);
        newHosts.emplace_back(buf, len);
    } else {
        for (int i = 0; i < av.count; ++i) {
            std::string instPath = path + "/" + av.data[i];
            char buf[128]; int len = sizeof(buf);
            zkClient_.GetDataW(instPath.c_str(), zkWatcher, this, buf, &len);
            newHosts.emplace_back(buf, len);
        }
    }

    deallocate_String_vector(&av);
    {
        std::lock_guard<std::mutex> lk(cache_mutex_);
        cache_[key] = std::move(newHosts);
    }
}


