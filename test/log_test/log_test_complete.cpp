//
// Created by inory on 10/28/25.
//

// test_logger.cpp
#include "logger.h"
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>
#include <fstream>
#include <regex>

int main() {
    // init logger - base name "testlog"
    Logger::Instance().Init("testlog", true, 10000, 8192, 10 * 1024 * 1024, 0);

    const int THREADS = 16;
    const int MSGS_PER_THREAD = 8000;
    const int TOTAL = THREADS * MSGS_PER_THREAD;

    std::atomic<int> global_seq{0};

    std::mutex log_mutex;

    // spawn producers
    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([t, &global_seq, MSGS_PER_THREAD, &log_mutex]() {
            for (int i = 0; i < MSGS_PER_THREAD; ++i) {
                std::lock_guard lk(log_mutex);
                int seq = global_seq.fetch_add(1, std::memory_order_relaxed);
                // include seq and thread id and a small payload
                LOG_INFO("SEQ:%d thread:%d i:%d payload:HelloWorld", seq, t, i);
                // optional: tiny sleep to create more interleaving
                // if (i % 1000 == 0) std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }

    for (auto &th : threads) th.join();

    // shutdown logger to flush all messages
    Logger::Instance().Shutdown();

    // now parse the log file
    // construct today's file name like logger
    time_t tt = time(nullptr);
    struct tm tm_now = *localtime(&tt);

    auto fname = Logger::Instance().GetCurrentLogName();
    std::ifstream fin(fname);
    if (!fin) {
        std::cerr << "Failed to open log file: " << fname << "\n";
        return 2;
    }

    std::string line;
    std::regex re("SEQ:(\\d+)");
    int expected = 0;
    int count = 0;
    bool ok = true;

    while (std::getline(fin, line)) {
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            int seq = std::stoi(m[1].str());
            if (seq != expected) {
                std::cerr << "Mismatch at count=" << count << ": got seq=" << seq << " expected=" << expected << "\n";
                ok = false;
                // break; // 可以继续收集错误信息
                expected = seq + 1; // continue but report
            } else {
                ++expected;
            }
            ++count;
        }
    }

    std::cout << "Total SEQ found: " << count << " expected: " << TOTAL << "\n";
    if (ok && count == TOTAL) {
        std::cout << "TEST PASSED: logs are strictly ordered and complete.\n";
    } else {
        std::cout << "TEST FAILED: ordering or completeness issue detected.\n";
    }

    return 0;
}


