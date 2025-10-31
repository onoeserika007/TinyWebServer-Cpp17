#include "user_service.h"
#include "logger.h"
#include "config_manager.h"
#include "mysql_conn_pool.h"
#include <vector>
#include <string>

UserService& UserService::Instance() {
    static UserService instance;
    return instance;
}

bool UserService::init() {
    // Initialize MySQL connection pool
    return MySQLConnectionPool::Instance().init();
}

bool UserService::registerUser(const std::string& username, const std::string& password) {
    if (userExists(username)) {
        return false;
    }
    
    auto conn = MySQLConnectionPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[UserService] Failed to get MySQL connection");
        return false;
    }
    
    // Escape strings to prevent SQL injection
    std::string safe_username = escapeString(username);
    std::string safe_password = escapeString(password);
    
    std::string sql = "INSERT INTO user(username, passwd) VALUES('" + 
                      safe_username + "', '" + safe_password + "')";
                      
    return conn->get()->exec(sql);
}

bool UserService::verifyUser(const std::string& username, const std::string& password) {
    auto conn = MySQLConnectionPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[UserService] Failed to get MySQL connection");
        return false;
    }
    
    std::string safe_username = escapeString(username);
    std::string safe_password = escapeString(password);
    
    std::string sql = "SELECT passwd FROM user WHERE username='" + safe_username + "'";
    
    if (!conn->get()->query(sql)) {
        return false;
    }
    
    MYSQL_RES* result = conn->get()->getResult();
    if (!result) {
        return false;
    }
    
    MYSQL_ROW row = conn->get()->fetchRow();
    if (!row) {
        return false;
    }
    
    return password == row[0];  // Compare with stored password
}

bool UserService::userExists(const std::string& username) {
    auto conn = MySQLConnectionPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[UserService] Failed to get MySQL connection");
        return false;
    }
    
    std::string safe_username = escapeString(username);
    std::string sql = "SELECT COUNT(*) FROM user WHERE username='" + safe_username + "'";
    
    if (!conn->get()->query(sql)) {
        return false;
    }
    
    MYSQL_RES* result = conn->get()->getResult();
    if (!result) {
        return false;
    }
    
    MYSQL_ROW row = conn->get()->fetchRow();
    if (!row) {
        return false;
    }
    
    return std::stoi(row[0]) > 0;
}

std::string UserService::escapeString(const std::string& str) {
    auto conn = MySQLConnectionPool::Instance().getConnection();
    if (!conn) {
        LOG_ERROR("[UserService] Failed to get MySQL connection for escaping string");
        return str;
    }
    
    std::vector<char> escaped(str.length() * 2 + 1);
    mysql_real_escape_string(conn->get()->getRawConn(), escaped.data(), str.c_str(), str.length());
    return std::string(escaped.data());
}