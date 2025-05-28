// RPCChannel.h
#ifndef _RPCCHANNEL_H_
#define _RPCCHANNEL_H_

#include <google/protobuf/service.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <muduo/base/Atomic.h>
#include <muduo/base/Mutex.h>
#include <map>
#include <atomic>
#include "rpc.pb.h"
struct ServiceInfo
{
    google::protobuf::Service* service;
    std::unordered_map<std::string, const google::protobuf::MethodDescriptor*> method_map;
};

class RPCChannel : public ::google::protobuf::RpcChannel {
private:

    struct OutstandingCall {
        ::google::protobuf::Message* response;
        ::google::protobuf::Closure* done;
    };
public:
    explicit RPCChannel(const std::shared_ptr<muduo::net::TcpConnection>& conn);
    explicit RPCChannel();
    ~RPCChannel() override;

    // 发起异步 RPC 调用
    void CallMethod(const ::google::protobuf::MethodDescriptor* method,
                    ::google::protobuf::RpcController* controller,
                    const ::google::protobuf::Message* request,
                    ::google::protobuf::Message* response,
                    ::google::protobuf::Closure* done) override;

    // 设置连接
    void setConnection(const muduo::net::TcpConnectionPtr& conn) {
        conn_ = conn;
    }
    void setServices(const std::map<std::string, ServiceInfo>* services)
    {
        services_ = services;
    }

    // 接收数据回调
    void onMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, muduo::Timestamp receive_time);
    typedef std::shared_ptr<google::protobuf::Message> MessagePtr;
    typedef std::shared_ptr<Krpc::RpcHeader> RpcMessagePtr;
    void onRPCMessage(const muduo::net::TcpConnectionPtr& conn, const RpcMessagePtr& messagePtr, muduo::Timestamp receive_time);
    void doneCallback(::google::protobuf::Message* response, uint64_t id);

private:

    muduo::net::TcpConnectionPtr conn_;
    muduo::AtomicInt64            id_;//默认为0
    muduo::MutexLock              mutex_;
    std::map<int64_t, OutstandingCall> outstandings_ GUARDED_BY(mutex_);

    const std::map<std::string, ServiceInfo>* services_;


    const ::google::protobuf::Message *prototype_;

};

#endif // _RPCCHANNEL_H_
