#ifndef MYSQL_CONNECTION_H
#define MYSQL_CONNECTION_H

#include <mysql/mysql.h>
#include <string>
#include "logger.h"

class MySQLConnection {
public:
    MySQLConnection();
    ~MySQLConnection();
    
    bool connect(const std::string& host, 
                const std::string& user,
                const std::string& pwd, 
                const std::string& db_name,
                unsigned int port = 3306);
    
    bool exec(const std::string& sql);
    bool query(const std::string& sql);
    
    MYSQL_RES* getResult() { return result_; }
    MYSQL_ROW fetchRow();
    MYSQL* getRawConn() { return conn_; }

private:
    MYSQL* conn_;
    MYSQL_RES* result_;
};

#endif // MYSQL_CONNECTION_H