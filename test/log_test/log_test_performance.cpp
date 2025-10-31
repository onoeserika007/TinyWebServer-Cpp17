//
// Created by inory on 10/28/25.
//

//
// Created by inory on 10/28/25.
//

// test_logger_perf.cpp
#include <atomic>
#include <chrono> // 高精度计时库
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <string>
#include <thread>
#include <unistd.h> // getpid on POSIX
#include <vector>
#include "logger.h"

// -------------------------- 性能测试参数配置（可按需调整）--------------------------
const int THREADS = 16; // 并发写日志线程数
const int MSGS_PER_THREAD = 10000; // 每个线程写入的日志条数
const int PAYLOAD_SIZE = 200; // 每条日志的payload大小（字节），模拟不同长度日志
const bool ENABLE_ASYNC = true; // 是否启用异步日志（true=异步，false=同步）
const bool ENABLE_VALIDATION = false; // 是否验证日志完整性（true=验证，会增加耗时）
const size_t LOG_QUEUE_SIZE = 20000; // 异步日志队列大小（仅异步模式生效）
const size_t LOG_ROTATE_SIZE = 100 * 1024 * 1024; // 日志轮转大小（避免频繁轮转影响性能）
// -----------------------------------------------------------------------------------

// 生成固定长度的payload，模拟真实日志内容
std::string generate_payload() {
    static const std::string base = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string payload;
    payload.reserve(PAYLOAD_SIZE);
    for (int i = 0; i < PAYLOAD_SIZE; ++i) {
        payload += base[i % base.size()];
    }
    return payload;
}

int main() {
    // 1. 初始化性能测试环境
    const std::string payload = generate_payload();
    const int TOTAL_LOGS = THREADS * MSGS_PER_THREAD;
    std::atomic<int> global_seq{0}; // 仅用于统计总日志数，无锁（原子操作不影响性能）

    // 2. 初始化日志库（按性能测试参数配置）
    Logger::Instance().Init("testlog_perf", // 日志基础名
                            ENABLE_ASYNC, // 异步/同步模式
                            LOG_QUEUE_SIZE, // 异步队列大小
                            8192, // 日志缓冲区大小
                            LOG_ROTATE_SIZE, // 日志轮转大小（调大避免性能干扰）
                            0 // 不关闭日志
    );

    std::cout << "===================== 日志性能测试开始 =====================\n";
    std::cout << "测试配置：\n";
    std::cout << "  - 并发线程数: " << THREADS << "\n";
    std::cout << "  - 单线程日志数: " << MSGS_PER_THREAD << "\n";
    std::cout << "  - 总日志数: " << TOTAL_LOGS << "\n";
    std::cout << "  - 单条日志payload大小: " << PAYLOAD_SIZE << " 字节\n";
    std::cout << "  - 日志模式: " << (ENABLE_ASYNC ? "异步" : "同步") << "\n";
    std::cout << "  - 完整性验证: " << (ENABLE_VALIDATION ? "开启" : "关闭") << "\n";
    std::cout << "============================================================\n";

    // 3. 高精度计时：记录日志写入总耗时（含异步队列消费+Shutdown刷盘）
    auto start_time = std::chrono::high_resolution_clock::now();

    // 4. 启动多线程并发写日志（无额外锁，模拟真实高并发场景）
    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([t, &global_seq, MSGS_PER_THREAD, &payload]() {
            for (int i = 0; i < MSGS_PER_THREAD; ++i) {
                // 原子操作获取seq（无锁，性能开销极低）
                int seq = global_seq.fetch_add(1, std::memory_order_relaxed);
                // 写入日志：包含seq、线程ID、循环计数、固定长度payload
                LOG_INFO("SEQ:{:d} thread:{:d} i:{:d} payload:{:s}", seq, t, i, payload.c_str());
            }
        });
    }

    // 5. 等待所有写日志线程结束
    for (auto &th: threads) {
        if (th.joinable()) {
            th.join();
        }
    }

    // 6. 关闭日志库（确保异步队列中所有日志刷盘完成，计入总耗时）
    Logger::Instance().Shutdown();

    // 7. 停止计时，计算性能指标
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    double total_seconds = total_duration.count() / 1000.0; // 总耗时（秒）
    double throughput = TOTAL_LOGS / total_seconds; // 吞吐量（条/秒）
    double avg_latency_us = (total_duration.count() * 1000.0) / TOTAL_LOGS; // 平均单条耗时（微秒）

    // 8. 可选：验证日志完整性（仅确认总条数，不校验顺序，避免影响性能统计）
    bool is_complete = true;
    if (ENABLE_VALIDATION) {
        auto log_fname = Logger::Instance().GetCurrentLogName();
        std::ifstream fin(log_fname);
        if (!fin) {
            std::cerr << "完整性验证失败：无法打开日志文件 " << log_fname << "\n";
            is_complete = false;
        } else {
            std::string line;
            std::regex re("SEQ:(\\d+)");
            int actual_count = 0;
            while (std::getline(fin, line)) {
                if (std::regex_search(line, re)) {
                    actual_count++;
                }
            }
            if (actual_count != TOTAL_LOGS) {
                std::cerr << "完整性验证失败：实际日志数 " << actual_count << "，期望 " << TOTAL_LOGS << "\n";
                is_complete = false;
            } else {
                std::cout << "完整性验证通过：实际日志数 = 期望日志数 = " << TOTAL_LOGS << "\n";
            }
        }
    }

    // 9. 输出性能测试结果
    std::cout << "\n===================== 日志性能测试结果 =====================\n";
    std::cout << "总耗时: " << total_duration.count() << " ms (" << total_seconds << " s)\n";
    std::cout << "总日志数: " << TOTAL_LOGS << " 条\n";
    std::cout << "吞吐量: " << std::fixed << std::setprecision(2) << throughput << " 条/秒\n";
    std::cout << "平均单条日志耗时: " << std::fixed << std::setprecision(2) << avg_latency_us << " 微秒\n";
    if (ENABLE_VALIDATION) {
        std::cout << "日志完整性: " << (is_complete ? "✅ 完整" : "❌ 不完整") << "\n";
    }
    std::cout << "============================================================\n";

    return 0;
}
