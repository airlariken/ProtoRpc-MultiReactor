#include "Zookeeperutil.h"
#include <mutex>
#include "Logger.h"
#include <condition_variable>

std::mutex cv_mutex;        // 全局锁，用于保护共享变量的线程安全
std::condition_variable cv; // 条件变量，用于线程间通信
bool is_connected = false;  // 标记ZooKeeper客户端是否连接成功

// 全局的watcher观察器，用于接收ZooKeeper服务器的通知
void global_watcher(zhandle_t *zh, int type, int status, const char *path, void *watcherCtx) {
    if (type == ZOO_SESSION_EVENT) {  // 回调消息类型和会话相关的事件
        if (status == ZOO_CONNECTED_STATE) {  // ZooKeeper客户端和服务器连接成功
            std::lock_guard<std::mutex> lock(cv_mutex);  // 加锁保护
            is_connected = true;  // 标记连接成功
        }
    }
    cv.notify_all();  // 通知所有等待的线程
}

// 构造函数，初始化ZooKeeper客户端句柄为空
ZkClient::ZkClient() : m_zhandle(nullptr) {}

// 析构函数，关闭ZooKeeper连接
ZkClient::~ZkClient() {
    if (m_zhandle != nullptr) {
        zookeeper_close(m_zhandle);  // 关闭ZooKeeper连接
    }
}

// 启动ZooKeeper客户端，连接ZooKeeper服务器
void ZkClient::Start(const std::string& host, const std::string& port) {
    // 从配置文件中读取ZooKeeper服务器的IP和端口
//    std::string host = KrpcApplication::GetInstance().GetConfig().Load("zookeeperip");
//    std::string port = KrpcApplication::GetInstance().GetConfig().Load("zookeeperport");
    std::string connstr = host + ":" + port;  // 拼接连接字符串

    /*
    zookeeper_mt：多线程版本
    ZooKeeper的API客户端程序提供了三个线程：
    1. API调用线程
    2. 网络I/O线程（使用pthread_create和poll）
    3. watcher回调线程（使用pthread_create）
    */

    // 1. 先把所有 C-client 日志重定向到 /dev/null（Linux）
    FILE* nullfp = fopen("/dev/null", "w");
    zoo_set_log_stream(nullfp);
    // 2. 可选：只保留 ERROR 及以上级别
    zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);

    // 使用zookeeper_init初始化一个ZooKeeper客户端对象，异步建立与服务器的连接
    m_zhandle = zookeeper_init(connstr.c_str(), global_watcher, 6000, nullptr, nullptr, 0);
    if (nullptr == m_zhandle) {  // 初始化失败
        LOG(ERROR) << "zookeeper_init error";
        exit(EXIT_FAILURE);  // 退出程序
    }

    // 等待连接成功
    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait(lock, [] { return is_connected; });  // 阻塞等待，直到连接成功
    LOG(INFO) << "zookeeper_init success";  // 记录日志，表示连接成功
}

// 创建ZooKeeper节点
void ZkClient::Create(const char *path, const char *data, int datalen, int state) {
    char path_buffer[128];  // 用于存储创建的节点路径
    int bufferlen = sizeof(path_buffer);

    // 检查节点是否已经存在
    int flag = zoo_exists(m_zhandle, path, 0, nullptr);
    if (flag == ZNONODE) {  // 如果节点不存在
        // 创建指定的ZooKeeper节点
        flag = zoo_create(m_zhandle, path, data, datalen, &ZOO_OPEN_ACL_UNSAFE, state, path_buffer, bufferlen);
        if (flag == ZOK) {  // 创建成功
            LOG(INFO) << "znode create success... path:" << path;
        } else {  // 创建失败
            LOG(ERROR) << "znode create failed... path:" << path;
            exit(EXIT_FAILURE);  // 退出程序
        }
    }
}

// 获取ZooKeeper节点的数据
std::string ZkClient::GetData(const char *path) {
    char buf[64];  // 用于存储节点数据
    int bufferlen = sizeof(buf);

    // 获取指定节点的数据
    int flag = zoo_get(m_zhandle, path, 0, buf, &bufferlen, nullptr);
    if (flag != ZOK) {  // 获取失败
        LOG(ERROR) << "zoo_get error";
        return "";  // 返回空字符串
    } else {  // 获取成功
        return buf;  // 返回节点数据
    }
    return "";  // 默认返回空字符串
}

std::vector<std::string> ZkClient::GetChildren(const char* path) {
    struct String_vector children;
    int ret = zoo_get_children(m_zhandle , path, 0, &children);
    std::vector<std::string> result;
    if (ret != ZOK) {
        LOG(ERROR) << "[ZkClient] GetChildren failed for path `" << path << "`: code=" << ret << std::endl;
        return result;
    }
    for (int i = 0; i < children.count; ++i) {
        result.emplace_back(children.data[i]);
    }

    deallocate_String_vector(&children);
    return result;
}

int ZkClient::GetChildrenW(const char* path,
                           watcher_fn watcher,
                           void* watcherCtx,
                           struct String_vector* strings) {
    // ZOO_CHILD_EVENT 会触发 watcher
    return zoo_wget_children(m_zhandle, path, watcher, watcherCtx, strings);
}
int ZkClient::GetDataW(const char* path,
                       watcher_fn watcher,
                       void* watcherCtx,
                       char* buffer,
                       int* bufferlen) {
    // ZOO_CHANGED_EVENT 会触发 watcher
    return zoo_wget(m_zhandle, path, watcher, watcherCtx, buffer, bufferlen, nullptr);
}



// === Modifications in zookeeperutil.cpp ===
void ZkClient::Delete(const char* path) {
    int flag = zoo_delete(m_zhandle, path, -1);
    if (flag == ZOK) LOG(INFO) << "znode delete success: " << path;
    else LOG(ERROR) << "znode delete failed: " << path;
}

void ZkClient::Close() {
    if (m_zhandle) {
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
        LOG(INFO) << "ZooKeeper session closed";
    }
}
// === Add global provider pointer in one cpp file ===
RpcServer* g_provider = nullptr;

// === Modifications in zookeeperutil.cpp ===