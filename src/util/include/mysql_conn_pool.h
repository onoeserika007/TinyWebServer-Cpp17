#ifndef MYSQL_CONN_POOL_H
#define MYSQL_CONN_POOL_H

#include "mysql_connection.h"
#include <string>
#include <queue>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <memory>
#include "logger.h"

// RAII方式使用连接
class MySQLConnectionGuard {
public:
    explicit MySQLConnectionGuard(MySQLConnection* conn) : conn_(conn) {}
    ~MySQLConnectionGuard();
    
    MySQLConnection* get() { return conn_; }
    
private:
    MySQLConnection* conn_;
};

class MySQLConnectionPool {
public:
    static MySQLConnectionPool& Instance();
    
    // 从ConfigManager获取配置并初始化连接池
    bool init();
              
    // 获取连接（返回RAII管理对象）
    std::shared_ptr<MySQLConnectionGuard> getConnection();
    void releaseConnection(MySQLConnection* conn);
    
private:
    MySQLConnectionPool() = default;
    ~MySQLConnectionPool();
    
    // 创建一个新的连接
    MySQLConnection* createConnection();
    
    std::string host_;
    std::string user_;
    std::string pwd_;
    std::string db_name_;
    unsigned int port_;
    
    int max_conn_;
    int curr_conn_;
    
    std::queue<MySQLConnection*, std::deque<MySQLConnection*>> conn_queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

#endif // MYSQL_CONN_POOL_H