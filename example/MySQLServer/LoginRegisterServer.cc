#include <iostream>
#include <string>
#include <mysql/mysql.h>
#include "../user.pb.h"
#include "Krpcapplication.h"
#include "RPCServer.h"
#include <muduo/base/ThreadPool.h>
#include <random>
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
    UserServiceImpl(int thread_num = 3)
            : threadPool_("UserService Workers' Threads"),
              pool_("192.168.3.1", "root", "zmxncbv.345", "chat", 3306, 2, 10)
    {
        threadPool_.start(thread_num);
    }

    // 真正的登录逻辑：查询 user_table 中用户名对应的密码并比对
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
    KrpcApplication::Init(argc, argv);
    RpcServer provider;
    provider.NotifyService(new UserServiceImpl());
    provider.Run();
    return 0;
}
