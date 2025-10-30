#ifndef USER_SERVICE_H
#define USER_SERVICE_H

#include <string>
#include <memory>
#include "mysql_conn_pool.h"

class UserService {
public:
    static UserService& Instance();
    
    // 初始化服务（连接池已经在mysql_conn_pool中初始化）
    bool init();
    
    // 用户注册
    bool registerUser(const std::string& username, const std::string& password);
    
    // 用户登录
    bool verifyUser(const std::string& username, const std::string& password);
    
    // 检查用户是否存在
    bool userExists(const std::string& username);
    
private:
    UserService() = default;
    
    std::string escapeString(const std::string& str);
};

#endif // USER_SERVICE_H