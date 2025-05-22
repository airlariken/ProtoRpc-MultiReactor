#ifndef _zookeeperutil_h_
#define _zookeeperutil_h_

#include<semaphore.h>
#include<zookeeper/zookeeper.h>
#include<string>
#include <vector>
class RpcServer; // forward declaration
//封装的zk客户端
class ZkClient
{
public:
    ZkClient();
    ~ZkClient();
    //zkclient启动连接zkserver
    void Start(const std::string& host, const std::string& port);
    //在zkserver中创建一个节点，根据指定的path
    void Create(const char* path,const char* data,int datalen,int state=0);
    void Delete(const char* path);
    void Close();

    std::string GetData(const char* path);      //根据参数指定的znode节点路径，或者znode节点值

    std::vector<std::string> GetChildren(const char* path);     // 获取指定 path 下的子节点名称列表

//GPT加入的带watcher的版本，用于感知服务上下线后的回调，修改cache
    // 带 watcher 的异步获取子节点
    int GetChildrenW(const char* path, watcher_fn watcher, void* watcherCtx, struct String_vector* strings);
    // 带 watcher 的异步获取节点数据
    int GetDataW(const char* path, watcher_fn watcher, void* watcherCtx, char* buffer, int* bufferlen);
private:
    //Zk的客户端句柄
    zhandle_t* m_zhandle;
};
#endif
