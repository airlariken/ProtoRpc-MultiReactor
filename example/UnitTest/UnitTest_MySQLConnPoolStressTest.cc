// Updated by ChatGPT: query-based MySQL stress test
// Created by orange on 3/6/25, modified for query stress testing
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <random>
#include <atomic>
#include <chrono>
#include "ConnectionPool.h"

// 原子计数
static std::atomic<long> totalQueries{0};
static std::atomic<long> successfulQueries{0};

// 生成指定长度的随机字符串，用于查询中的随机用户名
std::string generateRandomString(size_t length) {
    const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string result;
    result.reserve(length);
    thread_local std::mt19937 engine{std::random_device{}()};
    std::uniform_int_distribution<> dist(0, chars.size() - 1);
    for (size_t i = 0; i < length; ++i) {
        result.push_back(chars[dist(engine)]);
    }
    return result;
}

// 每个线程执行查询压力测试
void queryRandomData(MySQLConnectionPool& pool, int threadId, int iterations, const std::string& tableName) {
    for (int i = 0; i < iterations; ++i) {
        MYSQL* conn = pool.getConnection();
        if (!conn) {
            std::cerr << "线程 " << threadId << " 获取连接失败" << std::endl;
            continue;
        }
        // 随机生成用户名进行查询
//        std::string user = generateRandomString(8);
        std::string user = "zhangsan";
        std::string sql = "SELECT password FROM `" + tableName + "` WHERE name='" + user + "'";

        if (mysql_query(conn, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            if (res) {
                // 遍历结果（可选）
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    // 可处理 row[0]
                }
                mysql_free_result(res);
                successfulQueries.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            std::cerr << "线程 " << threadId << " 查询失败: " << mysql_error(conn) << std::endl;
        }
        pool.releaseConnection(conn);
        totalQueries.fetch_add(1, std::memory_order_relaxed);
    }
}

int main() {
    // 数据库连接配置，根据实际情况修改
//    std::string host = "127.0.0.1";
    std::string host = "localhost";
    std::string user = "root";
    std::string password = "zmxncbv.345";
    std::string database = "chat";
    int port = 3306;

    // 初始化连接池：初始创建2个连接，最多允许10个连接
    MySQLConnectionPool pool(host, user, password, database, port, 30, 30);

    // 设定压力测试参数
    const std::string tableName = "user"; // 假定表已存在并含 name/password 字段
    int numThreads = 30;        // 启动线程数
    int iterationsPerThread = 80000; // 每个线程查询次数

    // 计时开始
    auto startTime = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(queryRandomData, std::ref(pool), i, iterationsPerThread, tableName);
    }
    for (auto& t : threads) {
        t.join();
    }
    auto endTime = std::chrono::high_resolution_clock::now();

    // 统计结果
    double durationSec = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count() / 1000.0;
    long total = totalQueries.load();
    long success = successfulQueries.load();
    double qps = total / durationSec;

    std::cout << "总查询数: " << total << std::endl;
    std::cout << "成功查询数: " << success << std::endl;
    std::cout << "耗时(s): " << durationSec << std::endl;
    std::cout << "平均 QPS: " << qps << std::endl;

    return 0;
}
