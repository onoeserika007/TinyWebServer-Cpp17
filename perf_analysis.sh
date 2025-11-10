#!/bin/bash
# 性能分析脚本

set -e

SERVER_BIN="./build/bin/epoll_server"
SERVER_PID=""
DURATION=30  # 采样时长（秒）

echo "========================================="
echo "  TinyWebServer 性能分析工具"
echo "========================================="

# 1. 启动服务器（后台运行）
echo "[1] 启动服务器..."
$SERVER_BIN &
SERVER_PID=$!
echo "服务器 PID: $SERVER_PID"
sleep 2

# 2. 启动 webbench 压测（后台运行）
echo "[2] 启动 webbench 压测..."
./build/bin/webbench -c 1000 -t $DURATION http://127.0.0.1:8080/ > /dev/null 2>&1 &
WEBBENCH_PID=$!
sleep 2

# 3. perf record - CPU 采样分析
echo "[3] 采集 CPU 性能数据 (${DURATION}s)..."
sudo perf record -F 999 -p $SERVER_PID -g -o perf.data -- sleep $DURATION

# 等待 webbench 完成
wait $WEBBENCH_PID 2>/dev/null || true

# 4. 生成 perf report
echo "[4] 生成性能报告..."
sudo perf report -i perf.data --stdio > perf_report.txt
echo "详细报告已保存到: perf_report.txt"

# 5. 生成火焰图友好格式
echo "[5] 生成调用栈摘要..."
sudo perf script -i perf.data > perf_script.txt
echo "调用栈数据已保存到: perf_script.txt"

# 6. Top 热点函数
echo ""
echo "========================================="
echo "  Top 10 热点函数"
echo "========================================="
sudo perf report -i perf.data --stdio -n --sort overhead,symbol | head -40

# 7. 停止服务器
echo ""
echo "[6] 停止服务器..."
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo ""
echo "========================================="
echo "  分析完成！"
echo "========================================="
echo "1. 查看详细报告: less perf_report.txt"
echo "2. 查看调用栈: less perf_script.txt"
echo "3. 交互式分析: sudo perf report -i perf.data"
