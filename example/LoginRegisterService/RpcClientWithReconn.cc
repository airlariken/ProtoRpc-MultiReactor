#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/TcpClient.h"
#include "ServiceDiscovery.h"
#include "RPCChannel.h"
#include "../user.pb.h"
#include <google/protobuf/stubs/common.h>
#include "Application.h"
#include <vector>
#include <thread>

using namespace muduo;
using namespace muduo::net;
// 全局统计量
static std::atomic<int>  g_successCount{0};

// 获取 RPC 服务地址（保持不变）
InetAddress getAddr(const std::string& service, const std::string& method){
    std::string hostData;
    try {
        hostData = ServiceDiscovery::instance().pickHost(service, method);
    } catch (const std::exception& ex) {
        LOG_ERROR << "ServiceDiscovery error: " << ex.what();
        return {"0.0.0.0", 123};
    }
    auto pos = hostData.find(':');
    if (pos == std::string::npos) {
        LOG_ERROR << "Invalid hostData: " << hostData;
        return {"0.0.0.0", 123};
    }
    std::string ip = hostData.substr(0, pos);
    LOG(INFO)<<"rpc load balancing pick host:"<<hostData;
    uint16_t port = static_cast<uint16_t>(std::stoi(hostData.substr(pos + 1)));
    return InetAddress(ip, port);
}


class RpcClientWithReconn{
public:
    RpcClientWithReconn(EventLoop* loop,
              const std::string& service,
              const std::string& method,
              int clientId,
              int numRequests)
            : loop_(loop),
              service_(service),
              method_(method),
              clientId_(clientId),
              remainingRequests_(numRequests),
              channel_(new RPCChannel()),      // 先创建 channel/stub
              stub_(channel_.get())
    {
        // 用第一次从 ServiceDiscovery 拿到的地址初始化 client_
        InetAddress addr = getAddr(service_, method_);
        initClient(addr);
    }

    // 启动（或重连）入口
    void connect() {
        client_->connect();
    }

private:
    // 每次（重）初始化一个 TcpClient，并绑定回调
    void initClient(const InetAddress& serverAddr) {
        // 1) 如果之前还有 client_，先断掉
        if (client_) {
            client_->disconnect();
        }
        // 2) 新建 TcpClient
        client_.reset(new TcpClient(loop_, serverAddr, "RpcClient"));
        // 3) 绑定连接/消息回调
        client_->setConnectionCallback(
                std::bind(&RpcClientWithReconn::onConnection, this, std::placeholders::_1));
        client_->setMessageCallback(
                std::bind(&RPCChannel::onMessage,
                          channel_.get(),
                          std::placeholders::_1,
                          std::placeholders::_2,
                          std::placeholders::_3));
    }

    // 连接状态回调
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            LOG_INFO << "[Client " << clientId_
                     << "] connected to " << conn->peerAddress().toIpPort();
            // 关联 channel 并发起第一条 RPC
            channel_->setConnection(conn);
            sendLogin();
        }
        else {
            LOG_WARN << "[Client " << clientId_
                     << "] disconnected from server";
            if (remainingRequests_ > 0) {
//                LOG(INFO)<<"sleeping 20s for debug use";
//                sleep(20);
                // 还有剩余请求，重连新地址
                InetAddress newAddr = getAddr(service_, method_);
                LOG_INFO << "[Client " << clientId_
                         << "] retrying with " << newAddr.toIpPort();
                initClient(newAddr);
                client_->connect();
            }
            else {
                // 全部请求做完了，就退出 loop
                loop_->quit();
            }
        }
    }

    // 发送一个 Login RPC
    void sendLogin() {
        Kuser::LoginRequest req;
        req.set_name("zhangsan");
        req.set_pwd("123456");
        auto* resp = new Kuser::LoginResponse;
        stub_.Login(nullptr, &req, resp,
                    google::protobuf::NewCallback(
                            this, &RpcClientWithReconn::onLoginDone, resp));
    }

    // 收到 RPC 回包
    void onLoginDone(Kuser::LoginResponse* resp) {
        if (resp->result().errcode() == 0) {
            g_successCount.fetch_add(1, std::memory_order_relaxed);
        }
        delete resp;

        if (--remainingRequests_ > 0) {
            // 还有 RPC，要间隔一秒继续发
            LOG(INFO)<<"sending new Request";
            sleep(1);
            sendLogin();
        }
        else {
            // 做个演示，等会服务端关机
            sleep(30);
            client_->disconnect();
        }
    }

    // 内部成员
    EventLoop*                           loop_;
    std::string                          service_, method_;
    int                                  clientId_;
    std::atomic<int>                     remainingRequests_;
    std::unique_ptr<TcpClient>           client_;      // <-- 改为指针
    std::shared_ptr<RPCChannel>          channel_;
    Kuser::UserServiceRpc_Stub           stub_;
};


int main(int argc, char** argv) {
    if (argc < 7) {
        printf("Usage: %s <service> <method> <num_threads> <requests_per_client>\n", argv[0]);
        return 1;
    }
    std::string service = argv[3];      // e.g. "UserServiceRpc"
    std::string method  = argv[4];      // e.g. "Login"
    int numThreads      = std::atoi(argv[5]);
    int requestsPerClient = std::atoi(argv[6]);

//    KrpcApplication::Init(argc, argv);
    // 初始化（建议放在 main 开头）
    auto& app = Application::Instance(argc, argv);

    // 获取 Zookeeper 地址与端口
    std::string zkHost  = app.ZkHost();
    int         zkPort  = app.ZkPort();

//    // 获取 Server 部署地址与端口
//    std::string srvHost = app.ServerHost();
//    int         srvPort = app.ServerPort();

    // 从配置文件里读取其它配置项
    std::string someValue = app.GetConfig("some_key");


    ServiceDiscovery::instance().init(zkHost,  std::to_string(zkPort));
    // 记录开始时间
    auto t_start = std::chrono::high_resolution_clock::now();
    // 启动多线程 client
    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([service, method, i, requestsPerClient]() {
            EventLoop loop;
//            InetAddress addr = getAddr(service, method);
//            LOG(INFO)<<"rpc server addr is"<<addr.toIpPort();
            RpcClientWithReconn client(&loop, service, method, i, requestsPerClient);
            client.connect();
            loop.loop();
        });
    }

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    // 记录结束时间，计算 QPS
    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsedSec = std::chrono::duration<double>(t_end - t_start).count();

    int totalRequests = numThreads * requestsPerClient;
    double qps = totalRequests / elapsedSec;
    int  success = g_successCount.load();
    // 打印评估结果
    LOG_INFO << "===== Pressure Test Summary =====";
    LOG_INFO << "Threads: " << numThreads
             << ", Requests/Thread: " << requestsPerClient;
    LOG_INFO << "Total Requests: " << totalRequests
             << ", Success: " << success;
    LOG_INFO << "Elapsed Time: " << elapsedSec << " s";
    LOG_INFO << "Overall QPS: "   << qps << " req/s";

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}