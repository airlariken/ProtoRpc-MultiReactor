//
// Created by orange on 4/30/25.
//

#ifndef KRPC_SERVICEDISCOVERY_H
#define KRPC_SERVICEDISCOVERY_H
#include <iostream>
#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "Zookeeperutil.h"
#include "Logger.h"

//#include <muduo/net/Callbacks.h>
//#include <muduo/net/Buffer.h>
//#include <muduo/net/TcpConnection.h>
#include <thread>
#include <random>
#include <queue>
#include <condition_variable>
// 伪代码示例
class ServiceDiscovery {
public:

    static ServiceDiscovery& instance();
    void init(const std::string& host, const std::string& port);
    std::string pickHost(const std::string& service, const std::string& method);
    std::string QueryServiceHost(ZkClient* zkclient, const std::string& service_name,
                                 const std::string& method_name, int &idx);

private:
    // watcher 回调（符合 C API 签名）
    static void zkWatcher(zhandle_t* zh, int type,  int state, const char* path, void* ctx);

    // 真正处理事件：path 变更后，重新拉取对应节点，更新 cache_
    void handleEvent(const std::string& path, int type);
private:
    ServiceDiscovery(): worker_(&ServiceDiscovery::processEvents, this), stop_(false){}
    ~ServiceDiscovery() {
        {
            std::lock_guard<std::mutex> lk(queueMtx_);
            stop_ = true;
        }
        queueCv_.notify_one();
        worker_.join();
    }
    ZkClient zkClient_;
    std::mutex cache_mutex_;
    std::mutex zk_mutex_;
    // key: "service/method" -> list of "ip:port"
    std::unordered_map<std::string, std::vector<std::string>> cache_;


    //demon线程用于独立管理zkwatcher触发的事件回调
    struct ZkEvent { std::string path; int type; };
    std::thread               worker_;
    std::mutex                queueMtx_;
    std::condition_variable   queueCv_;
    bool                      stop_;
    std::queue<ZkEvent>       events_;

    void processEvents();
    void enqueueEvent(const std::string& path, int type);


};
// 放在头文件或匿名命名空间里
inline thread_local std::mt19937  tls_rng{ std::random_device{}() };
#endif //KRPC_SERVICEDISCOVERY_H
