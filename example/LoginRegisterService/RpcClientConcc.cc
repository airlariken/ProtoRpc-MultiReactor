//
// Created by orange on 5/26/25.
//
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
#include <atomic>

using namespace muduo;
using namespace muduo::net;

// 全局成功计数
static std::atomic<int>  g_successCount{0};

// 同 getAddr 保持不变
InetAddress getAddr(const std::string& service, const std::string& method){
    std::string hostData;
    try {
        hostData = ServiceDiscovery::instance().pickHost(service, method);
    } catch (const std::exception& ex) {
        LOG_ERROR << "ServiceDiscovery error: " << ex.what();
        return {"0.0.0.0", 123};
    }
    auto pos = hostData.find(':');
    std::string ip = hostData.substr(0, pos);
    uint16_t port = static_cast<uint16_t>(std::stoi(hostData.substr(pos + 1)));
    return InetAddress(ip, port);
}

// RpcClient 支持一次性并发发起 N 个 RPC
class RpcClient : noncopyable {
public:
    RpcClient(EventLoop* loop,
              const InetAddress& serverAddr,
              int clientId,
              int numRequests)
            : loop_(loop),
              client_(loop, serverAddr, "RpcClient"),
              channel_(new RPCChannel()),
              stub_(channel_.get()),
              clientId_(clientId),
              totalRequests_(numRequests),
              pendingResponses_(numRequests)
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
    // 连接成功时，立即并发发起所有请求
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            LOG_INFO << "[Client " << clientId_ << "] connected, sending "
                     << totalRequests_ << " requests in parallel";
            channel_->setConnection(conn);
            for (int i = 0; i < totalRequests_; ++i) {
                sendLogin();
            }
        } else {
            LOG_INFO << "[Client " << clientId_ << "] disconnected";
            loop_->quit();
        }
    }

    // 真正发送一次 Login RPC
    void sendLogin() {
        Kuser::LoginRequest request;
        request.set_name("zhangsan");
        request.set_pwd("123456");
        // 注意：Response 对象必须是堆上分配，以便异步回调里 delete
        auto* response = new Kuser::LoginResponse;
        stub_.Login(nullptr, &request, response,
                    google::protobuf::NewCallback(
                            this, &RpcClient::onLoginDone, response));
    }

    // 每收到一个回调就递减 pendingResponses_
    void onLoginDone(Kuser::LoginResponse* resp) {
        if (resp->result().errcode() == 0) {
            g_successCount.fetch_add(1, std::memory_order_relaxed);
        }
        delete resp;

        // 所有回调都收完了，就断开连接
        if (pendingResponses_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
//            LOG_INFO << "[Client " << clientId_ << "] all responses received, disconnecting";
            client_.disconnect();
        }
    }

private:
    EventLoop*                   loop_;
    TcpClient                    client_;
    std::shared_ptr<RPCChannel>  channel_;
    Kuser::UserServiceRpc_Stub   stub_;
    int                          clientId_;
    int                          totalRequests_;
    std::atomic<int>             pendingResponses_;
};

int main(int argc, char** argv) {
    if (argc < 5) {
        printf("Usage: %s <service> <method> <num_threads> <requests_per_client>\n", argv[0]);
        return 1;
    }
    std::string service  = argv[1];      // e.g. "UserServiceRpc"
    std::string method   = argv[2];      // e.g. "Login"
    int numThreads       = std::atoi(argv[3]);
    int requestsPerClient= std::atoi(argv[4]);

    auto& app = Application::Instance(argc, argv);
    ServiceDiscovery::instance().init(app.ZkHost(), std::to_string(app.ZkPort()));

    auto t_start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&service, &method, i, requestsPerClient]() {
            EventLoop loop;
            InetAddress addr = getAddr(service, method);
            LOG_INFO << "[Client " << i << "] server addr = " << addr.toIpPort();
            RpcClient client(&loop, addr, i, requestsPerClient);
            client.connect();
            loop.loop();
        });
    }

    for (auto& t : threads) t.join();

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t_end - t_start).count();
    int totalReqs = numThreads * requestsPerClient;
    int success   = g_successCount.load();
    double qps    = totalReqs / elapsed;

    LOG_INFO << "===== Pressure Test Summary =====";
    LOG_INFO << "Threads: " << numThreads
             << ", Requests/Thread: " << requestsPerClient;
    LOG_INFO << "Total Requests: " << totalReqs
             << ", Success: " << success;
    LOG_INFO << "Elapsed Time: " << elapsed << " s";
    LOG_INFO << "Overall QPS: " << qps << " req/s";

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
