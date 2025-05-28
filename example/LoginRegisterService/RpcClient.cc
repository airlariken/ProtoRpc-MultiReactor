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
    uint16_t port = static_cast<uint16_t>(std::stoi(hostData.substr(pos + 1)));
    return InetAddress(ip, port);
}

// RpcClient 类：支持多次请求并区分 clientId_
class RpcClient : noncopyable {
public:
    RpcClient(EventLoop* loop, const InetAddress& serverAddr,
              int clientId, int numRequests)
            : loop_(loop),
              client_(loop, serverAddr, "RpcClient"),
              channel_(new RPCChannel()),
              stub_(channel_.get()),
              clientId_(clientId),
              remainingRequests_(numRequests)
    {
        client_.setConnectionCallback(
                std::bind(&RpcClient::onConnection, this, std::placeholders::_1));
        client_.setMessageCallback(
                std::bind(&RPCChannel::onMessage, channel_.get(),
                          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    void connect() {
        client_.connect();
    }

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            LOG_INFO << "[Client " << clientId_ << "] connected, starting requests";
            channel_->setConnection(conn);
            sendLogin();
        } else {
            LOG_INFO << "[Client " << clientId_ << "] disconnected, quitting loop";
            loop_->quit();
        }
    }

    void sendLogin() {
        Kuser::LoginRequest request;
        request.set_name("zhangsan");
        request.set_pwd("123456");
        auto* response = new Kuser::LoginResponse;
        stub_.Login(nullptr, &request, response,
                    google::protobuf::NewCallback(this,
                                                  &RpcClient::onLoginDone, response));
    }

    void onLoginDone(Kuser::LoginResponse* resp) {
        if (resp->result().errcode() == 0) {
            g_successCount.fetch_add(1, std::memory_order_relaxed);
//            LOG_INFO << "[Client " << clientId_
//                     << "] login success: " << resp->success();
        } else {
//            LOG_WARN << "[Client " << clientId_
//                     << "] login error: " << resp->result().errmsg();
        }
        delete resp;

        if (--remainingRequests_ > 0) {
            LOG(INFO)<<"sending new Request...";
            sleep(1);       //睡1s继续发
            // 还有剩余请求，继续发
            sendLogin();
        } else {
            sleep(30);      //试一下rpc server的下线
            // 请求完成，主动断开
            client_.disconnect();
        }
    }

    EventLoop*                   loop_;
    TcpClient                    client_;
    std::shared_ptr<RPCChannel> channel_;
    Kuser::UserServiceRpc_Stub   stub_;
    int                          clientId_;
    int                          remainingRequests_;
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
            InetAddress addr = getAddr(service, method);
            LOG(INFO)<<"rpc server addr is"<<addr.toIpPort();
            RpcClient client(&loop, addr, i, requestsPerClient);
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
