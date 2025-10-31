#include "mysql_conn_pool.h"
#include "mysql_connection.h"
#include "logger.h"
#include "config_manager.h"
#include <memory>
#include <stdexcept>

MySQLConnectionGuard::~MySQLConnectionGuard() {
    if (conn_) {
        MySQLConnectionPool::Instance().releaseConnection(conn_);
        conn_ = nullptr;
    }
}

MySQLConnectionPool& MySQLConnectionPool::Instance() {
    static MySQLConnectionPool instance;
    return instance;
}

bool MySQLConnectionPool::init() {
    auto& config = ConfigManager::Instance();
    
    host_ = config.get<std::string>("mysql.host", "localhost");
    user_ = config.get<std::string>("mysql.user", "root");
    pwd_ = config.get<std::string>("mysql.password", "123456");
    db_name_ = config.get<std::string>("mysql.database", "webserver_db");
    port_ = config.get<int>("mysql.port", 3306);
    max_conn_ = config.get<int>("mysql.pool_size", 8);
    curr_conn_ = 0;
    
    for (int i = 0; i < max_conn_; ++i) {
        MySQLConnection* conn = createConnection();
        if (conn) {
            conn_queue_.push(conn);
            ++curr_conn_;
        } else {
            LOG_ERROR("MySQL connection pool init failed at conn #{:d}", i);
            return false;
        }
    }
    
    return true;
}

MySQLConnectionPool::~MySQLConnectionPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!conn_queue_.empty()) {
        auto conn = conn_queue_.front();
        conn_queue_.pop();
        delete conn;
    }
}

MySQLConnection* MySQLConnectionPool::createConnection() {
    try {
        auto conn = new MySQLConnection();
        if (!conn->connect(host_, user_, pwd_, db_name_, port_)) {
            delete conn;
            return nullptr;
        }
        return conn;
    } catch (const std::exception& e) {
        LOG_ERROR("Create MySQL connection failed: {:s}", e.what());
        return nullptr;
    }
}

std::shared_ptr<MySQLConnectionGuard> MySQLConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (conn_queue_.empty()) {
        if (curr_conn_ < max_conn_) {
            MySQLConnection* conn = createConnection();
            if (conn) {
                ++curr_conn_;
                return std::make_shared<MySQLConnectionGuard>(conn);
            }
        }
        // 等待可用连接
        cond_.wait(lock);
    }
    
    MySQLConnection* conn = conn_queue_.front();
    conn_queue_.pop();
    return std::make_shared<MySQLConnectionGuard>(conn);
}

void MySQLConnectionPool::releaseConnection(MySQLConnection* conn) {
    if (!conn) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    conn_queue_.push(conn);
    cond_.notify_one();
}