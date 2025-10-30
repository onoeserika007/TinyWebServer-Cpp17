#include "mysql_connection.h"
#include <stdexcept>

MySQLConnection::MySQLConnection() : conn_(nullptr), result_(nullptr) {
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        throw std::runtime_error("MySQL init failed");
    }
}

MySQLConnection::~MySQLConnection() {
    if (result_) {
        mysql_free_result(result_);
        result_ = nullptr;
    }
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

bool MySQLConnection::connect(const std::string& host, 
                            const std::string& user,
                            const std::string& pwd, 
                            const std::string& db_name,
                            unsigned int port) {
    MYSQL* ret = mysql_real_connect(conn_, host.c_str(), user.c_str(),
                                  pwd.c_str(), db_name.c_str(), port,
                                  nullptr, 0);
    return ret != nullptr;
}

bool MySQLConnection::exec(const std::string& sql) {
    if (mysql_query(conn_, sql.c_str()) != 0) {
        LOG_ERROR("MySQL exec failed: {:s}", mysql_error(conn_));
        return false;
    }
    return true;
}

bool MySQLConnection::query(const std::string& sql) {
    if (result_) {
        mysql_free_result(result_);
        result_ = nullptr;
    }
    
    if (mysql_query(conn_, sql.c_str()) != 0) {
        LOG_ERROR("MySQL query failed: {:s}", mysql_error(conn_));
        return false;
    }
    
    result_ = mysql_store_result(conn_);
    return result_ != nullptr;
}

MYSQL_ROW MySQLConnection::fetchRow() {
    return result_ ? mysql_fetch_row(result_) : nullptr;
}