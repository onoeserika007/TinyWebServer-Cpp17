#include "config_manager.h"
#include "logger.h"
#include <iostream>
#include <variant>
#include <cassert>
#include <fstream>
#include <filesystem>

// void printConfigValue(const ConfigManager::ConfigValue& value) {
//     // std::visit([](const auto& v) {
//     //     std::cout << "Value: " << v << std::endl;
//     // }, value);
// }

// void testServerConfigFile() {
//     // std::cout << "=== Testing Server Configuration File ===" << std::endl;
    
//     // // 从项目配置文件加载
//     // if (!ConfigManager::Instance().init("conf/server.json")) {
//     //     LOG_ERROR("Failed to initialize config manager with server config");
//     //     return;
//     // }

//     // // 验证配置值是否正确（根据conf/server.json的内容）
//     // std::cout << "Verifying MySQL Configuration:" << std::endl;
//     // assert(ConfigManager::Instance().getMySQLHost() == "localhost");
//     // assert(ConfigManager::Instance().getMySQLPort() == 3306);
//     // assert(ConfigManager::Instance().getMySQLUser() == "root");
//     // assert(ConfigManager::Instance().getMySQLDatabase() == "webserver");
//     // std::cout << "MySQL configuration verified!" << std::endl;

//     // std::cout << "\nVerifying HTTP Server Configuration:" << std::endl;
//     // std::cout << "Port: " << ConfigManager::Instance().getServerPort() << std::endl;
//     // std::cout << "Worker Threads: " << ConfigManager::Instance().getWorkerThreads() << std::endl;
//     // std::cout << "Max Connections: " << ConfigManager::Instance().getMaxConnections() << std::endl;
//     // std::cout << "Document Root: " << ConfigManager::Instance().getDocumentRoot() << std::endl;
//     // std::cout << "HTTP Server configuration check completed!" << std::endl;

//     // std::cout << "\nVerifying Log Configuration:" << std::endl;
//     // std::cout << "Visible Level: " << ConfigManager::Instance().getLogVisibleLevel() << std::endl;
//     // std::cout << "Path: " << ConfigManager::Instance().getLogPath() << std::endl;
//     // std::cout << "To Console: " << (ConfigManager::Instance().getLogToConsole() ? "yes" : "no") << std::endl;
//     // std::cout << "To File: " << (ConfigManager::Instance().getLogToFile() ? "yes" : "no") << std::endl;
//     // std::cout << "Flush Interval: " << ConfigManager::Instance().getLogFlushInterval() << std::endl;
//     // std::cout << "Log configuration check completed!" << std::endl;

//     // // 测试保存配置
//     // std::cout << "\nTesting config save functionality..." << std::endl;
//     // assert(ConfigManager::Instance().saveConfig("saved_server_config.json"));
//     // std::cout << "Config saved successfully!" << std::endl;
// }

// void testConfigSerialization() {
//     // std::cout << "\n=== Testing Configuration Serialization ===" << std::endl;
    
//     // // 重置配置管理器以确保干净的状态
//     // ConfigManager::Instance().reset();
    
//     // // 创建测试配置
//     // Json::Value root;
//     // root["mysql"]["host"] = "serialize_test_host";
//     // root["mysql"]["port"] = 4000;
//     // root["mysql"]["user"] = "serialize_test_user";
//     // root["mysql"]["password"] = "serialize_test_password";
//     // root["mysql"]["database"] = "serialize_test_db";
//     // root["mysql"]["pool_size"] = 15;
    
//     // root["server"]["port"] = 9500;
//     // root["server"]["worker_threads"] = 10;
//     // root["server"]["max_connections"] = 8000;
//     // root["server"]["document_root"] = "/serialize/test/root";
//     // root["server"]["timeout_ms"] = 5000;

//     // root["log"]["visible_level"] = "warn";
//     // root["log"]["async"] = false;
//     // root["log"]["path"] = "/serialize/test/logs";
//     // root["log"]["to_console"] = false;
//     // root["log"]["to_file"] = true;
//     // root["log"]["flush_interval"] = 10;
//     // root["log"]["roll_size"] = 10000000;
    
//     // // 写入测试配置文件
//     // Json::StreamWriterBuilder writer;
//     // writer["commentStyle"] = "None";
//     // writer["indentation"] = "    ";
//     // std::unique_ptr<Json::StreamWriter> streamWriter(writer.newStreamWriter());
    
//     // std::ofstream file("serialize_test_input.json");
//     // streamWriter->write(root, &file);
//     // file.close();
    
//     // // 从文件加载配置
//     // ConfigManager& config = ConfigManager::Instance();
//     // assert(config.init("serialize_test_input.json"));
    
//     // // 验证配置值
//     // assert(config.getMySQLHost() == "serialize_test_host");
//     // assert(config.getMySQLPort() == 4000);
//     // assert(config.getMySQLUser() == "serialize_test_user");
//     // assert(config.getMySQLDatabase() == "serialize_test_db");
//     // assert(config.getMySQLPoolSize() == 15);
    
//     // assert(config.getServerPort() == 9500);
//     // assert(config.getWorkerThreads() == 10);
//     // assert(config.getMaxConnections() == 8000);
//     // assert(config.getDocumentRoot() == "/serialize/test/root");
//     // assert(config.getTimeoutMs() == 5000);

//     // assert(config.getLogVisibleLevel() == "warn");
//     // assert(config.getLogAsync() == false);
//     // assert(config.getLogPath() == "/serialize/test/logs");
//     // assert(config.getLogToConsole() == false);
//     // assert(config.getLogToFile() == true);
//     // assert(config.getLogFlushInterval() == 10);
//     // assert(config.getLogRollSize() == 10000000);
    
//     // std::cout << "Configuration serialization test passed!" << std::endl;
    
//     // // 测试保存配置并验证文件内容
//     // assert(config.saveConfig("serialize_test_output.json"));
    
//     // // 读取保存的文件并验证内容
//     // std::ifstream savedFile("serialize_test_output.json");
//     // std::string content((std::istreambuf_iterator<char>(savedFile)), 
//     //                     std::istreambuf_iterator<char>());
//     // savedFile.close();
    
//     // // 简单验证内容不为空
//     // assert(!content.empty());
//     // std::cout << "Configuration saved to file successfully!" << std::endl;
// }

int main() {
    // 初始化日志系统
    // Logger::Instance().Init("config_test", true);
    
    // testServerConfigFile();
    // testConfigSerialization();
    
    // // 清理测试文件
    // std::filesystem::remove("saved_server_config.json");
    // std::filesystem::remove("serialize_test_input.json");
    // std::filesystem::remove("serialize_test_output.json");
    
    // std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}