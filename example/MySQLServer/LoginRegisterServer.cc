#include <iostream>
#include <string>
#include <mysql/mysql.h>
#include "../user.pb.h"
#include "Application.h"
#include "RPCServer.h"
#include <muduo/base/ThreadPool.h>
#include <thread>
#include "ConnectionPool.h"
#include "Logger.h"
class UserServiceImpl : public Kuser::UserServiceRpc {
private:
    // 线程池
    muduo::ThreadPool threadPool_;
    // 全局 MySQL 连接池
    MySQLConnectionPool pool_;

public:
    UserServiceImpl(const string& database_username, const string& database_passwd, int thread_num = 10)
            : threadPool_("UserService Workers' Threads"),
              pool_("192.168.3.1", database_username, database_passwd, "chat", 3306, 50, 50)
    {
        threadPool_.start(thread_num);
    }

// 真正的登录逻辑：查询 user_table 中用户名对应的密码并比对
    bool Login(const std::string& name, const std::string& pwd) {
        MYSQL* conn = pool_.getConnection();
        if (!conn) {
            std::cerr << "[Login] 获取 MySQL 连接失败" << std::endl;
            return false;
        }

        // 对用户名做转义，防止 SQL 注入
        std::string escName(name);
        char* esc_buffer = new char[2 * escName.size() + 1];
        mysql_real_escape_string(conn, esc_buffer, escName.c_str(), escName.length());

        // 构造查询并打印日志
        std::string query = std::string("SELECT password FROM user WHERE name='")
                            + esc_buffer + "'";
//        LOG(INFO) << "[Login] 执行查询: " << query << std::endl;
        delete[] esc_buffer;

        if (mysql_query(conn, query.c_str()) != 0) {
            LOG(INFO) << "[Login] 查询错误: " << mysql_error(conn);
            pool_.releaseConnection(conn);
            return false;
        }

        MYSQL_RES* result = mysql_store_result(conn);
        if (!result) {
            LOG(WARNING) << "[Login] 获取结果集失败: " << mysql_error(conn);
            pool_.releaseConnection(conn);
            return false;
        }

        unsigned long rows = mysql_num_rows(result);
//        LOG(INFO) << "[Login] 找到行数: " << rows << std::endl;

        bool success = false;
        if (rows > 0) {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row && row[0]) {
                std::string stored = row[0];
                success = (stored == pwd);
                if (!success) {
                    LOG(INFO) << "[Login] 密码不匹配: 输入='" << pwd
                              << "', 存储='" << stored << "'" ;
                }
                else{
//                    LOG(INFO)<<"[Login]登录成功 用户"<<name<<"密码"<<pwd;
                }
            }
        }

        mysql_free_result(result);
        pool_.releaseConnection(conn);
        return success;
    }

    // RPC 接口：线程池异步调用 Login 逻辑
    void Login(::google::protobuf::RpcController* controller,
               const ::Kuser::LoginRequest* request,
               ::Kuser::LoginResponse* response,
               ::google::protobuf::Closure* done) override {
        auto safe_response = std::shared_ptr<Kuser::LoginResponse>(
                response, [](Kuser::LoginResponse*){});
        auto safe_done = std::shared_ptr<google::protobuf::Closure>(
                done, [](google::protobuf::Closure*){});

        std::string name = request->name();
        std::string pwd  = request->pwd();

        threadPool_.run([=]() {
            bool ok = Login(name, pwd);
            Kuser::ResultCode* code = safe_response->mutable_result();
            code->set_errcode(ok ? 0 : 1);
            code->set_errmsg(ok ? "" : "用户名或密码错误");
            safe_response->set_success(ok);
            safe_done->Run();
        });
    }
};

int main(int argc, char** argv) {
//    KrpcApplication::Init(argc, argv);
    auto& app = Application::Instance(argc, argv);

    // 获取 Zookeeper 地址与端口
    std::string zkHost  = app.ZkHost();
    int         zkPort  = app.ZkPort();

    // 获取 Server 部署地址与端口
    std::string srvHost = app.ServerHost();
    int         srvPort = app.ServerPort();

    // 从配置文件里读取其它配置项
    std::string db_username = app.GetConfig("db_username");
    std::string db_passwd =  app.GetConfig("db_passwd");
    // 创建一个 RPC 服务提供者对象
    RpcServer _rpc_server;

    // 将 UserService 对象发布到 RPC 节点上，使其可以被远程调用
    _rpc_server.NotifyService(new UserServiceImpl(db_username, db_passwd));

    // 启动 RPC 服务节点，进入阻塞状态，等待远程的 RPC 调用请求
    _rpc_server.Run(srvHost, srvPort, zkHost, std::to_string(zkPort));

    return 0;
}
