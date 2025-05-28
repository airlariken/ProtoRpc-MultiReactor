# 基于muduo的RPC服务
- 支持基于Zookeeper的rpc服务上下线感知
- 支持客户端负载均衡
- 服务端支持线程池与异步任务
- RPC Ping-Pong测试单机16核 1000+wQPS
- RPC Mysql查询服务单机16核 100+wQPS（基于连接池的异步实现）





# RPCChannel
一个基于 Muduo 网络库和 Google Protocol Buffers 实现的轻量级异步 RPC 框架。核心组件 RPCChannel 负责：

序列化/反序列化请求与响应

维护出站调用队列（等待服务器响应）

调度本地服务（处理入站 RPC 请求）


客户端与服务端的连接全部基于RPCChannel类，该类负责对protobuf的request和response进行封装，并且交付给下层的高性能网络库

- 基于protobuf的消息格式，具体详见src/rpc.proto文件 
  - 自定义消息头解决粘包拆包问题
  - 支持并发发送，使用id号对消息进行编号和索引，

| 方法                  | 功能                                                                                                                                                      |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CallMethod(...)`   | 发起异步 RPC 请求：<br>1. 序列化用户 `request` <br>2. 构造并序列化 `RpcHeader` <br>3. 注册回调到 `outstandings_` <br>4. 通过 TCP 连接发送带长度前缀的消息                                    |
| `onMessage(...)`    | TCP 底层接收回调：<br>1. 解析长度前缀 <br>2. 提取完整报文并反序列化到 `RpcHeader` <br>3. 转交给 `onRPCMessage`                                                                      |
| `onRPCMessage(...)` | 统一处理 `REQUEST` / `RESPONSE`：<br>- **RESPONSE**：查找对应 `id` 的回调，反序列化 payload，执行用户 `done` 回调<br>- **REQUEST**：查找本地服务和方法，反序列化请求，异步调用并在执行完毕后触发 `doneCallback` |
| `doneCallback(...)` | 服务端异步方法结束后的回调：<br>1. 序列化 `response` <br>2. 构造并序列化响应 `RpcHeader` <br>3. 通过 TCP 连接发送响应                                                                    |

## Zookeeperutil类
ZkClient 是对官方 ZooKeeper C 客户端（zookeeper.h）的轻量封装，提供：

与 ZooKeeper 集群的同步连接管理（阻塞式等待会话建立）

常用的节点操作：创建、删除、读取数据、列出子节点

带 Watcher 的异步接口：GetChildrenW / GetDataW，便于上层实现服务发现、配置变更监听

### 主要职责
1. 会话管理

   - 异步调用 zookeeper_init 建立会话

   - 在 global_watcher 中捕获 ZOO_CONNECTED_STATE，并通过条件变量通知阻塞的 Start

2. 节点操作

   - Create：检查是否存在，若不存在则调用 zoo_create

   - Delete：调用 zoo_delete，版本无限制（version=-1）

   - GetData／GetChildren：同步接口，失败时返回空或空列表

   - GetDataW／GetChildrenW：注册 watcher，支持上层动态感知节点或子节点变化

3. 资源清理

   - 析构函数中自动关闭会话

   - Close 方法可显式提前断开




| 方法签名                                                                                        | 功能说明                       |
| ------------------------------------------------------------------------------------------- | -------------------------- |
| `void Start(const std::string& host, const std::string& port)`                              | 阻塞连接 ZooKeeper，直到会话建立成功    |
| `void Create(const char* path, const char* data, int datalen, int state=0)`                 | 创建节点，`state` 可选持久或临时       |
| `void Delete(const char* path)`                                                             | 删除指定节点                     |
| `std::string GetData(const char* path)`                                                     | 同步读取节点数据，失败返回空             |
| `std::vector<std::string> GetChildren(const char* path)`                                    | 同步列出子节点，失败返回空列表            |
| `int GetChildrenW(const char* path, watcher_fn watcher, void* ctx, String_vector* strings)` | 异步列出子节点并注册 watcher，监听子节点增删 |
| `int GetDataW(const char* path, watcher_fn watcher, void* ctx, char* buffer, int* buflen)`  | 异步读取数据并注册 watcher，监听节点数据变更 |
| `void Close()`                                                                              | 关闭会话并释放 `m_zhandle`        |



## ServiceDiscovery类
ServiceDiscovery 基于 ZooKeeper 实现服务注册与发现，支持：

启动时批量拉取所有服务/方法节点及其实例列表

动态监听节点变化（子节点增删、数据变更），自动更新本地 cache_

随机/轮询/一致性哈新负载均衡选取可用实例

它与 RPCChannel 配合，为 RPC 客户端提供透明的服务发现能力。


| 方法签名                                                                                        | 功能说明                                         |
| ------------------------------------------------------------------------------------------- | -------------------------------------------- |
| `static ServiceDiscovery& instance()`                                                       | 单例访问                                         |
| `void init(const std::string& host, const std::string& port)`                               | 连接 ZooKeeper 并完成第一次全量拉取与 watcher 注册          |
| `std::string pickHost(const std::string& service, const std::string& method)`               | 从本地缓存随机选取一个实例，若不存在抛出异常                       |
| `static void zkWatcher(zhandle_t*, int type, int state, const char* path, void* ctx)`       | ZooKeeper C API watcher 回调，将事件封装后交给后台线程处理    |
| `void enqueueEvent(const std::string& path, int type)`                                      | 将 watcher 触发的事件加入 `events_` 队列               |
| `void processEvents()`                                                                      | 后台线程主循环，消费队列并调用 `handleEvent` 更新缓存           |
| `void handleEvent(const std::string& path, int type)`                                       | 对单个路径重新拉取子节点与数据，并更新 `cache_`                 |
| `std::string QueryServiceHost(ZkClient*, const std::string&, const std::string&, int &idx)` | （备用）按需直接从 ZooKeeper 同步查询并随机选取实例，返回 `ip:port` |


# RPC客户端

## 项目简介

本目录下包含三种 RPC 客户端压测示例，用于评估基于 Muduo + Protobuf + ZooKeeper 的 RPC 框架在不同场景下的性能与可靠性：

1. **串行请求压测**
   每个客户端按固定间隔（1 s）依次发送请求，适合模拟低并发持续调用场景。

2. **并行请求压测**
   每个客户端在连接成功后一次性并发发起 N 个请求，适合测量短时高并发吞吐能力。

3. **带服务上下线动态感知与重连压测**
   在串行发送的基础上，客户端对服务上下线事件自动感知并重新选择可用实例，适合测试高可用与故障恢复性能。

---

## 依赖

* C++11/C++14
* [Muduo](https://github.com/chenshuo/muduo) 网络库
* Google Protocol Buffers（`protobuf >= 3.0`）
* ZooKeeper C API（通过 `ZkClient` 封装）
* `ServiceDiscovery`、`RPCChannel`、`Application`（同项目其他模块）
* CMake 构建工具

---

## 构建

在项目根目录的 `CMakeLists.txt` 中添加三个可执行目标：

```cmake
add_executable(serial_client   serial_client.cpp)
add_executable(parallel_client parallel_client.cpp)
add_executable(reconnect_client reconnect_client.cpp)

target_link_libraries(serial_client   muduo_net muduo_base protobuf zookeeper_mt)
target_link_libraries(parallel_client muduo_net muduo_base protobuf zookeeper_mt)
target_link_libraries(reconnect_client muduo_net muduo_base protobuf zookeeper_mt)
```

然后：

```bash
mkdir build && cd build
cmake ..
make -j
```

---

## 使用示例

三个程序的通用启动参数：

```
Usage: <exe> <serviceName> <methodName> <numThreads> <requestsPerClient>
```

* `serviceName`／`methodName`：注册在 ZooKeeper 上的服务与方法名称
* `numThreads`：并发客户线程数
* `requestsPerClient`：每个线程发起的请求总数

在运行前，确保已启动 ZooKeeper 服务并注册至少一个 RPCServer 实例。

---

## 客户端示例

### 1. 串行请求压测（`serial_client`）

* **连接建立后**：每秒发送一次 `Login` 请求
* **请求完成后**：等待 30 s，再主动断开连接
* **统计**：全局 `g_successCount` 累积成功次数，结束时打印总时长与 QPS

```bash
example/LoginRegisterService/RpcClient.cc UserServiceRpc Login 10 100
```

10 线程，每线程顺序发送 100 次请求。

---

### 2. 并行请求压测（`parallel_client`）

* **连接建立后**：立即并发发起所有 N 个 RPC 请求
* **收到最后一个响应后**：自动断开连接
* **统计**：同上，测量极限并发下的吞吐性能

```bash
example/LoginRegisterService/RpcClientConcc.cc UserServiceRpc Login 10 1000
```

10 线程，每线程并行发起 1000 次请求。

---

### 3. 带动态重连的压测（`reconnect_client`）

* **行为**：与串行版本类似，但在连接断开时（如服务下线），自动从 `ServiceDiscovery` 再次 `pickHost`，并重连新实例直至完成所有请求
* **适用场景**：测试服务上下线时客户端的高可用与自动恢复能力

```bash
./example/LoginRegisterService/RpcClientWithReconn.cc UserServiceRpc Login 5 200
```

5 线程，每线程发送 200 次请求，期间可模拟先关闭某个服务实例观察客户端重连效果。

---

## 注意事项

* **服务发现**：客户端启动前需调用 `ServiceDiscovery::instance().init(zkHost, zkPort)`。
* **线程安全**：全局计数器使用 `std::atomic` 保护，无需额外同步。
* **评测结束**：程序退出前调用 `ShutdownProtobufLibrary()` 清理 protobuf 资源。




# RPC服务端

---
`RpcServer` 基于 Muduo 网络库和 ZooKeeper 提供了一个可插拔的 RPC 服务端框架，支持：

* 动态注册任意 Protobuf `Service`
* 启动 TCP 服务器并对外提供 RPC 调用
* 向 ZooKeeper 注册自己为多个服务实例（临时节点），供客户端发现
* 优雅停机：处理未完成请求后再关闭服务与 ZK 会话

---


## 快速开始

```cpp
#include "RPCServer.h"
#include "user_service.pb.h"  // 你的 protobuf Service 定义

int main() {
  RpcServer server;

  // 1. 注册业务 Service（由 protobuf 生成的类实例）
  MyUserServiceImpl userSvc;
  server.NotifyService(&userSvc);

  // 2. 启动 RPC 服务，并向 ZooKeeper 注册
  server.Run("0.0.0.0", 8000, "127.0.0.1", "2181");

  // 3. 在收到 SIGINT/SIGTERM 时，会自动优雅停机
  return 0;
}
```

---


### 主要职责

1. **服务注册**

    * `NotifyService(Service*)`：遍历 `ServiceDescriptor` 自动收集方法列表，保存到内部 `service_map`。
2. **网络启动**

    * 在 `Run(...)` 中创建 `muduo::net::TcpServer`，绑定连接与消息回调，设置线程数并启动 `EventLoop`。
3. **ZooKeeper 注册**

    * 在 `Run` 内使用 `ZkClient`：

        * 创建持久化节点 `/ServiceName` 和 `/ServiceName/MethodName`
        * 为每个方法创建一个**临时子节点**，路径如 `/Service/Method/host:port`
        * 保存所有实例路径以便后续清理
4. **连接管理**

    * `OnConnection`：

        * 连接建立时创建 `RPCChannel`，绑定到连接上下文，并设置消息回调为 `RPCChannel::onMessage`
        * 连接断开时释放上下文，递减 `pending_requests_`
5. **优雅停机**

    * 信号处理（`SIGINT`/`SIGTERM`）触发 `StopServer()` 或退出 `EventLoop`
    * `StopServer()`：

        * 禁止再接受新连接
        * 等待最多 3 秒处理未完成的请求，然后退出循环
        * 调用 `Cleanup()` 注销 ZooKeeper 上的临时节点并关闭会话

### 关键方法

| 方法签名                                                                                            | 功能说明                                   |
| ----------------------------------------------------------------------------------------------- | -------------------------------------- |
| `void NotifyService(google::protobuf::Service* service)`                                        | 注册一个业务 Service，将其方法描述符加入 `service_map` |
| `void Run(const std::string& ip, int port, const std::string& zkIp, const std::string& zkPort)` | 启动 TCPServer、注册到 ZK 并进入事件循环            |
| `void OnConnection(const TcpConnectionPtr& conn)`                                               | 处理新连接／断开：创建或释放 `RPCChannel`，维护并发计数     |
| `void StopServer()`                                                                             | 优雅停机入口：停收新连接，等待未完成请求，然后退出循环            |
| `void Cleanup()`                                                                                | 删除所有在 ZK 上的临时实例节点，关闭 ZK 会话             |

### 主要成员变量

* `std::shared_ptr<TcpServer> server_`：Muduo TCP 服务对象
* `EventLoop event_loop`：Muduo 事件循环
* `std::map<std::string, ServiceInfo> service_map`：已注册的服务及方法
* `ZkClient zkclient_`：ZooKeeper 客户端，用于注册和注销
* `std::vector<std::string> instance_paths_`：本实例在 ZK 上创建的所有临时节点路径
* `std::atomic<int> pending_requests_`：当前活跃连接／请求计数，用于优雅停机

---

## Graceful Shutdown

1. 捕获 `SIGINT` 或 `SIGTERM`，调用 `StopServer()` 或 `EventLoop::quit()`
2. `StopServer()` 在 Loop 线程：

    * `disableAccept()` 停止接收新连接
    * 检查 `pending_requests_` 是否为 0

        * 若是，则立即 `quit()`
        * 否则等待 1 秒后强制 `quit()`
3. `~RpcServer()` 析构时调用 `Cleanup()`，依次删除 ZK 上的临时节点并关闭会话

---

## 注意事项

* **线程安全**：Muduo 保证回调都在 `event_loop` 线程执行，`pending_requests_` 无需额外同步。
* **ZK 节点清理**：临时节点在会话断开时自动删除，但仍在 `Cleanup()` 中显式 `Delete` 以加快触发客户端 watcher。
* **高可用**：可启动多个 `RpcServer` 实例向同一 ZK 路径注册，实现客户端的随机／均衡调用。
* **扩展**：可在 `OnConnection` 中加入心跳、限流、认证等逻辑；或在 `Run` 前后注册更多信号和健康检查。

---

更多细节请查看源码注释与示例工程。


# RPC服务端压力测试
## PingPong测试

## MySQL账户查询测试
