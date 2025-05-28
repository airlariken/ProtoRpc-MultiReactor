#include "RPCServer.h"
#include "rpc.pb.h"
#include "Logger.h"
#include <csignal>
#include "muduo/net/EventLoop.h"
// === Add global server pointer in one cpp file ===
RpcServer* _my_server = nullptr;


// 注册服务对象及其方法，以便服务端能够处理客户端的RPC请求
void RpcServer::NotifyService(google::protobuf::Service *service) {
    // 服务端需要知道客户端想要调用的服务对象和方法，
    // 这些信息会保存在一个数据结构（如 ServiceInfo）中。
    ServiceInfo service_info;

    // 参数类型设置为 google::protobuf::Service，是因为所有由 protobuf 生成的服务类
    // 都继承自 google::protobuf::Service，这样我们可以通过基类指针指向子类对象，
    // 实现动态多态。

    // 通过动态多态调用 service->GetDescriptor()，
    // GetDescriptor() 方法会返回 protobuf 生成的服务类的描述信息（ServiceDescriptor）。
    const google::protobuf::ServiceDescriptor *psd = service->GetDescriptor();

    // 通过 ServiceDescriptor，我们可以获取该服务类中定义的方法列表，
    // 并进行相应的注册和管理。

    // 获取服务的名字
    std::string service_name = psd->name();
    // 获取服务端对象service的方法数量
    int method_count = psd->method_count();

    // 打印服务名
    LOG(INFO) << "service_name=" << service_name;

    // 遍历服务中的所有方法，并注册到服务信息中
    for (int i = 0; i < method_count; ++i) {
        // 获取服务中的方法描述
        const google::protobuf::MethodDescriptor *pmd = psd->method(i);
        std::string method_name = pmd->name();
        LOG(INFO) << "method_name=" << method_name;
        service_info.method_map.emplace(method_name, pmd);  // 将方法名和方法描述符存入map
    }
    service_info.service = service;  // 保存服务对象
    service_map.emplace(service_name, service_info);  // 将服务信息存入服务map
}

// 启动RPC服务节点，开始提供远程网络调用服务
void RpcServer::Run(const std::string& server_ip, int server_port, const std::string& zook_ip, const std::string& zook_port) {
//    // 读取配置文件中的RPC服务器IP和端口
//    std::string server_ip = KrpcApplication::GetInstance().GetConfig().Load("rpcserverip");
//    int server_port = atoi(KrpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());

    // 使用muduo网络库，创建地址对象
    muduo::net::InetAddress address(server_ip, server_port);

    // 创建TcpServer对象
    server_ = std::make_shared<muduo::net::TcpServer>(&event_loop, address, "RPCServer");

    // 绑定连接回调和消息回调，分离网络连接业务和消息处理业务
    server_->setConnectionCallback(std::bind(&RpcServer::OnConnection, this, std::placeholders::_1));
//    server->setMessageCallback(std::bind(&KrpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));


    // 设置muduo库的线程数量
    server_->setThreadNum(5);

//    // 将当前RPC节点上要发布的服务全部注册到ZooKeeper上，让RPC客户端可以在ZooKeeper上发现服务
//    ZkClient zkclient;
//    zkclient.Start();  // 连接ZooKeeper服务器
//    // service_name为永久节点，method_name为临时节点
//    for (auto &sp : service_map) {
//        // service_name 在ZooKeeper中的目录是"/"+service_name
//        std::string service_path = "/" + sp.first;
//        zkclient.Create(service_path.c_str(), nullptr, 0);  // 创建服务节点
//        for (auto &mp : sp.second.method_map) {
//            std::string method_path = service_path + "/" + mp.first;
//            char method_path_data[128] = {0};
//            sprintf(method_path_data, "%s:%d", server_ip.c_str(), server_port);  // 将IP和端口信息存入节点数据
//            // ZOO_EPHEMERAL表示这个节点是临时节点，在客户端断开连接后，ZooKeeper会自动删除这个节点
//            zkclient.Create(method_path.c_str(), method_path_data, strlen(method_path_data), ZOO_EPHEMERAL);
//        }
//    }
//GPT重构版本，支持多server注册相同服务，负载均衡 begin

    ZkClient zkclient;
    zkclient.Start(zook_ip, zook_port);

// 1) for each service, make sure the persistent service node exists
    for (auto &sp : service_map) {
        const std::string service  = "/" + sp.first;
        zkclient.Create(service.c_str(), nullptr, 0, 0);

        // 2) for each method, make sure the persistent method node exists
        for (auto &mp : sp.second.method_map) {
            const std::string methodPath = service + "/" + mp.first;
            zkclient.Create(methodPath.c_str(), nullptr, 0, 0);

            // 3) now register *this* server instance as an ephemeral child:
            //    Option A: use server_ip:server_port in the name so it's unique
            std::string instName = server_ip + ":" + std::to_string(server_port);
            std::string instancePath = methodPath + "/" + instName;
            LOG(INFO)<<"writing server_ip"<<instName<<" into "<<methodPath;
            char buf[128];
            sprintf(buf, "%s:%d", server_ip.c_str(), server_port);
            zkclient.Create(instancePath.c_str(), buf, strlen(buf), ZOO_EPHEMERAL);

            instance_paths_.push_back(instancePath);     //  record instance paths
            //    Option B: let ZooKeeper append a sequence number:
            //std::string instPrefix = methodPath + "/server-";
            //zkclient.Create(instPrefix.c_str(),
            //                buf, strlen(buf),
            //                ZOO_EPHEMERAL | ZOO_SEQUENCE);
        }
    }

    //GPT重构版本，支持多server注册相同服务，负载均衡 end
    // RPC服务端准备启动，打印信息
    LOG(INFO) << "RpcServer start service at server_ip:" << server_ip << " server_port:" << server_port;

    // 启动网络服务
    server_->start();
    event_loop.loop();  // 进入事件循环
}

// 连接回调函数，处理客户端连接事件
void RpcServer::OnConnection(const muduo::net::TcpConnectionPtr &conn) {
    LOG(INFO) << "RpcServer - " << conn->peerAddress().toIpPort() << " -> "
             << conn->localAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");

    if (conn->connected()){
        std::shared_ptr<RPCChannel> krpcChannel_ptr = std::make_shared<RPCChannel>(conn);     //注意这里一定要传进去个conn我草曹操
        conn->setContext(krpcChannel_ptr);
        krpcChannel_ptr->setServices(&service_map);
        conn->setMessageCallback(std::bind(&RPCChannel::onMessage, krpcChannel_ptr.get(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        pending_requests_.fetch_add(1);
    }
    // @@@czw modified end@@@
    if (!conn->connected()) {
//        LOG(INFO)<<"server call conn->shutdown()";
        // 1) 清除 context，让 shared_ptr<KrpcChannel> 释放
        conn->setContext(std::shared_ptr<RPCChannel>());
        pending_requests_.fetch_sub(1) - 1;
//        conn->shutdown();//我草啊就是这b行代码导致客户端无法关闭，我草啊为啥啊，debug1h我去
        // 2) 不要再调用 conn->shutdown() ——muduo 在 TCP FIN/ACK 完成后自动 close
    }
}


void RpcServer::Cleanup() {
    LOG(INFO) << "Unregistering services from ZooKeeper...";
    for (const auto &path : instance_paths_) {
        LOG(INFO) <<"deleting service method:"<<path;
//        sleep(10);
        zkclient_.Delete(path.c_str());
    }
//    不需要在下线时再显式调用 zoo_delete() 来清理临时节点。关闭会话就足够了！！！
    zkclient_.Close(); // closes handle and deletes ephemerals
    LOG(INFO) << "ZooKeeper handle closed";
}


// 析构函数，退出事件循环
RpcServer::~RpcServer() {
    // After loop quits, perform cleanup
    Cleanup();
    LOG(INFO) << "~RpcServer()";
//    event_loop.quit();  // 退出事件循环
}

RpcServer::RpcServer() {
    // Save pointer for signal handler
    extern RpcServer* _my_server;
    _my_server = this;

    // 3) Setup signal handler for graceful shutdown
    auto signal_handler2 = [](int signo){
        LOG(INFO) << "Signal SIGTERM" << signo << " received, stopping server...";
//        _my_server->Cleanup();
        // 让 main() 自然退出，触发栈展开
        _my_server->event_loop.queueInLoop(std::bind(&muduo::net::EventLoop::quit, &_my_server->event_loop));
//        _my_server->event_loop.quit();
    };
    auto signal_handler1 = [](int signo){
        LOG(INFO) << "Signal SIGINT" << signo << " received, stopping server...";
//        _my_server->Cleanup();
        // 让 main() 自然退出，触发栈展开
        _my_server->StopServer();
//        _my_server->event_loop.quit();
    };
    ::signal(SIGINT,  signal_handler1);
    ::signal(SIGTERM, signal_handler2);
}

void RpcServer::StopServer() {
    Cleanup();          //下线Zookeeper，节点消失，会触发client端的watcher的回调
    // 标记为正在停止
//    stopping_.store(true);

    // 所有操作都在 Loop 线程执行
    event_loop.queueInLoop([this]() {
        // 1) 关闭监听，不再接受新连接/请求
        server_->disableAccept();
//        sleep(1);       //1s后检查是否还有残余连接
        // 2) 如果此时已经没有未完成请求，立即退出 loop
        if (pending_requests_.load() == 0) {
            event_loop.queueInLoop(std::bind(&muduo::net::EventLoop::quit, &event_loop));
            LOG(INFO)<<"No more conn left, safe close without rst conn";
            return ;
        }
        sleep(1);       //如果连接还有，给你3秒机会去处理，如果还是没完成，直接关了
        event_loop.queueInLoop(std::bind(&muduo::net::EventLoop::quit, &event_loop));
        LOG(INFO)<<"hard close with some conns still left";
        return ;
    });
}
