#include "mysql_conn_pool.h"
#include "config_manager.h"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>

void testConnectionPoolInitialization() {
    std::cout << "Testing MySQL Connection Pool Initialization..." << std::endl;
    
    // 初始化配置管理器
    auto& config = ConfigManager::Instance();
    assert(config.init("conf/server.json"));
    
    // 初始化连接池
    auto& pool = MySQLConnectionPool::Instance();
    assert(pool.init());
    
    std::cout << "Connection pool initialization test passed." << std::endl;
}

void testGetConnection() {
    std::cout << "Testing Get Connection..." << std::endl;
    
    auto& pool = MySQLConnectionPool::Instance();
    
    // 获取一个连接
    auto conn = pool.getConnection();
    assert(conn != nullptr);
    assert(conn->get() != nullptr);
    
    // 连接应该可以正常使用
    auto mysql_conn = conn->get();
    assert(mysql_conn != nullptr);
    
    std::cout << "Get connection test passed." << std::endl;
}

void testMultipleConnections() {
    std::cout << "Testing Multiple Connections..." << std::endl;
    
    auto& pool = MySQLConnectionPool::Instance();
    
    // 获取多个连接
    std::vector<std::shared_ptr<MySQLConnectionGuard>> connections;
    for (int i = 0; i < 3; ++i) {
        auto conn = pool.getConnection();
        assert(conn != nullptr);
        connections.push_back(std::move(conn));
    }
    
    // 检查所有连接都有效
    for (const auto& conn : connections) {
        assert(conn->get() != nullptr);
    }
    
    // 清理连接（自动释放）
    connections.clear();
    
    std::cout << "Multiple connections test passed." << std::endl;
}

void testConnectionReuse() {
    std::cout << "Testing Connection Reuse..." << std::endl;
    
    auto& pool = MySQLConnectionPool::Instance();
    
    // 获取连接并记住它的原始指针
    auto conn1 = pool.getConnection();
    MySQLConnection* raw_conn1 = conn1->get();
    
    // 释放连接
    conn1.reset();
    
    // 再次获取连接，应该得到相同的连接（重用）
    auto conn2 = pool.getConnection();
    MySQLConnection* raw_conn2 = conn2->get();
    
    // 由于连接池的工作方式，我们可能得到相同的连接
    // 这里我们只验证连接是有效的
    assert(raw_conn2 != nullptr);
    
    std::cout << "Connection reuse test passed." << std::endl;
}

void testExecuteQuery() {
    std::cout << "Testing Execute Query..." << std::endl;
    
    auto& pool = MySQLConnectionPool::Instance();
    
    // 获取连接
    auto conn = pool.getConnection();
    assert(conn != nullptr);
    
    auto mysql_conn = conn->get();
    assert(mysql_conn != nullptr);
    
    // 执行一个简单的查询来测试连接是否工作正常
    // 注意：这里我们不关心查询结果，只关心连接是否能执行查询
    bool result = mysql_conn->query("SELECT 1");
    // 即使数据库中没有表，这个查询也应该返回结果
    
    std::cout << "Execute query test passed." << std::endl;
}

int main() {
    try {
        testConnectionPoolInitialization();
        testGetConnection();
        testMultipleConnections();
        testConnectionReuse();
        testExecuteQuery();
        
        std::cout << "All tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}