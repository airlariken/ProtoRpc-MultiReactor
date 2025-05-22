// KrpcChannel.cpp
#include "RPCChannel.h"

#include "ServiceDiscovery.h"
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

using namespace muduo;
using namespace muduo::net;

KrpcChannel::KrpcChannel(const std::shared_ptr<muduo::net::TcpConnection>& conn):prototype_(&Krpc::RpcHeader::default_instance()),conn_(conn)
{
}
KrpcChannel::KrpcChannel():prototype_(&Krpc::RpcHeader::default_instance())
{
}

void KrpcChannel::CallMethod(const ::google::protobuf::MethodDescriptor* method,
                             ::google::protobuf::RpcController* controller,
                             const ::google::protobuf::Message* request,
                             ::google::protobuf::Message* response,
                             ::google::protobuf::Closure* done) {
    // 1. 序列化请求体（payload）
    std::string payload;
    if (!request->SerializeToString(&payload)) {
        controller->SetFailed("Failed to serialize request.");
        return;
    }

    // 2. 构造 RPCHeader
    Krpc::RpcHeader header;
    int64_t id = id_.incrementAndGet();
    header.set_type(Krpc::REQUEST);
    header.set_id(id);
    header.set_service_name( method->service()->name());
    header.set_method_name( method->name());
    header.set_payload(payload);  // 将 request 内容作为 payload 填入

    std::string headerStr;
    if (!header.SerializeToString(&headerStr)) {
        controller->SetFailed("serialize rpc header error");
        return;
    }

    // 4. 注册 callback，等待响应
    {
        MutexLockGuard lock(mutex_);
        outstandings_[id] = OutstandingCall{response, done};
    }

    // 5. 发送 packet 到服务器（可选加长度头部）
    if (conn_) {
        // 发送带长度前缀的消息，便于服务端 framing（如果你希望有长度前缀）
        uint32_t len = static_cast<uint32_t>(headerStr.size()+sizeof(len));
        // 2) 转成网络字节序（big-endian）
        uint32_t netLen = htonl(len);

        std::string sendBuf;
        sendBuf.append(reinterpret_cast<const char*>(&netLen), sizeof(netLen));  // 4 字节长度
        sendBuf += headerStr;
        conn_->send(sendBuf);  // 假设 conn_->send 支持 string 发送
    } else {
        controller->SetFailed("No active connection.");
    }
}

void KrpcChannel::onRPCMessage(const TcpConnectionPtr& conn, const RpcMessagePtr& messagePtr, Timestamp receive_time)
{
    Krpc::RpcHeader& message = *messagePtr;

    if (message.type() == Krpc::RESPONSE) {
        // 6) 处理 RESPONSE 消息
        uint64_t id = message.id();
        OutstandingCall call;
        {
            MutexLockGuard lock(mutex_);
            auto it = outstandings_.find(id);
            if (it == outstandings_.end()) {
                // 找不到对应的调用，直接返回
                return;
            }
            call = it->second;
            outstandings_.erase(it);
        }
        // 7) 将 payload 反序列化到用户传入的 response 对象
        if (call.response) {
            if (!call.response->ParseFromString(message.payload())) {
                LOG(ERROR) << "failed to parse response payload, id=" << id;
            }
        }
        // 8) 调用回调
        if (call.done) {
            call.done->Run();
        }
    }
    else if (message.type() == Krpc::REQUEST) {
        // 1) 从 header 中取出 service 和 method 名称
        const std::string& svcName   = message.service_name();
        const std::string& mthdName  = message.method_name();
        uint64_t         callId      = message.id();

        // 2) 在本地服务表中查找对应的 service
        auto sit = services_->find(svcName);
        if (sit == services_->end()) {
            LOG(ERROR) << "No service named " << svcName;
            return;
        }
        ServiceInfo service_info =  sit->second;
        ::google::protobuf::Service* service = service_info.service;

        // 3) 找到对应的 MethodDescriptor
        const ::google::protobuf::ServiceDescriptor* sdsc = service->GetDescriptor();
        const ::google::protobuf::MethodDescriptor* md = sdsc->FindMethodByName(mthdName);
        if (!md) {
            LOG(ERROR) << "No method " << mthdName << " in service " << svcName;
            return;
        }

        // 4) 根据 method 原型，New 出 request/response 对象
        std::unique_ptr<::google::protobuf::Message> req(
                service->GetRequestPrototype(md).New());
        ::google::protobuf::Message* rsp(service->GetResponsePrototype(md).New());  //由CallMethod用uniqueptr接管

        // 5) 反序列化 payload 到 req
        if (!req->ParseFromString(message.payload())) {
            LOG(ERROR) << "Failed to parse request payload for call " << callId;
            return;
        }

        // 6) 异步调用：执行 service 方法后由用户done->run()后填充 rsp并发送
        service->CallMethod(md, /* controller= */ nullptr,req.get(), rsp, NewCallback(this, &KrpcChannel::doneCallback, rsp, callId));

    }
}

void KrpcChannel::onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp receive_time) {
    const static int kLenField = sizeof(int32_t);
    const static int kMaxMessageLen = 64*1024*1024; // same as codec_stream.h kDefaultTotalBytesLimit
//    LOG(INFO) << "onMessage triggered";
    while (buf->readableBytes() >= static_cast<size_t>(kLenField)) {
        // 1) 读长度前缀（包含自身长度）
        uint32_t msgLen = static_cast<uint32_t>(buf->peekInt32());

        if (msgLen < kLenField || msgLen > kMaxMessageLen) {
            LOG(ERROR) << "rpc message invalid length: " << msgLen;
            conn->shutdown();
            return;
        }
        // 2) 如果半包，等待后续数据
        if (buf->readableBytes() < static_cast<size_t>(msgLen)) {
            LOG(INFO) << "rpc message is not ready: " << msgLen;
            return;
        }
        // 3) 去掉长度字段
        buf->retrieve(kLenField);
        // 4) 读取整个 RpcHeader 序列化数据
        std::string raw = buf->retrieveAsString(msgLen-kLenField);

        // 5) 解析 RpcHeader 这里使用了原型模式（应该是类似工厂模式）在类构造函数的时候传入了具体工厂的instance
        MessagePtr message(prototype_->New());
        if (!message->ParseFromString(raw)) {
            LOG(ERROR) << "failed to parse RpcHeader";
            return;
        }
        onRPCMessage(conn, ::muduo::down_pointer_cast<Krpc::RpcHeader>(message), receive_time);//这里应该改成回调方式
    }
}

//bool KrpcChannel::parseFromBuffer(StringPiece buf, google::protobuf::Message* message){
//    return message->ParseFromArray(buf.data(), buf.size());
//}


void KrpcChannel::doneCallback(::google::protobuf::Message* response, uint64_t id){
    std::unique_ptr<google::protobuf::Message> d(response);     //接管response
    // 7) 将 rsp 序列化，并构造 RESPONSE header
    std::string rspPayload;
    if (!response->SerializeToString(&rspPayload)) {
        LOG(ERROR) << "Failed to serialize response for call " << id;
        return;
    }
    Krpc::RpcHeader respHdr;
    respHdr.set_type(Krpc::RESPONSE);
    respHdr.set_id(id);
    respHdr.set_payload(rspPayload);

    std::string raw;
    if (!respHdr.SerializeToString(&raw)){
        LOG(ERROR) << "Failed to serialize response for call " << id;
        return;
    }
    uint32_t len = static_cast<uint32_t>(raw.size()) + sizeof(len);
    // 2) 转成网络字节序（big-endian）
    uint32_t netLen = htonl(len);
    // 8) 发送带长度前缀的 response
    std::string sendBuf;
    sendBuf.append(reinterpret_cast<const char*>(&netLen), sizeof(netLen));
    sendBuf += raw;
    conn_->send(sendBuf);
}


KrpcChannel::~KrpcChannel() {
//    LOG(INFO) << "RpcChannel::dtor - " << this;
    for (const auto& outstanding : outstandings_)
    {
        OutstandingCall out = outstanding.second;
        delete out.response;
        delete out.done;
    }
}
