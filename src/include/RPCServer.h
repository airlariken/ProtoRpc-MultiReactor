#ifndef _RPCServer_H__
#define _RPCServer_H__
//muduo related
#include<muduo/net/TcpServer.h>
#include<muduo/net/EventLoop.h>
#include<muduo/net/InetAddress.h>
#include<muduo/net/TcpConnection.h>
//muduo related
// protobuf related
#include<google/protobuf/descriptor.h>
#include "google/protobuf/service.h"
// protobuf related
#include "Zookeeperutil.h"
#include <RPCChannel.h>

#include<string>
#include<map>
#include<memory>

class RpcServer
{
public:
    RpcServer();
    //这里是提供给外部使用的，可以发布rpc方法的函数接口。
    void NotifyService(google::protobuf::Service* service);
      ~RpcServer();
    //启动rpc服务节点，开始提供rpc远程网络调用服务
    void Run(const std::string& server_ip, int server_port,
             const std::string& zook_ip = "127.0.0.1", const std::string& zook_port="2181");
    void StopServer();

private:
    std::shared_ptr<muduo::net::TcpServer> server_;

    muduo::net::EventLoop event_loop;
    std::map<std::string, ServiceInfo>service_map;//保存服务对象和rpc方法

//    std::atomic<bool>            stopping_{false};
    std::atomic<int>             pending_requests_{0};

    // New members for graceful shutdown
    ZkClient zkclient_;                      // Moved from local in Run
    std::vector<std::string> instance_paths_; // Store all ephemeral node paths
    void Cleanup(); // unregister from ZK and close session

    void OnConnection(const muduo::net::TcpConnectionPtr& conn);
//    void OnMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buffer, muduo::Timestamp receive_time);
//    void SendRpcResponse(const muduo::net::TcpConnectionPtr& conn, google::protobuf::Message* response);
};
#endif







