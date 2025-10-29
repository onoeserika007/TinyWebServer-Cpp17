#!/usr/bin/env bash
# ----------------------------------------
# format.sh - 批量使用 clang-format 格式化 C++ 源码
# ----------------------------------------

# 终止脚本时如果有错误则退出
set -e

# 检查 clang-format 是否存在
if ! command -v clang-format-18 &> /dev/null; then
    echo "Error: clang-format-18 not found. Please install it first."
    echo "For Ubuntu: sudo apt install clang-format-18"
    exit 1
fi

# 打印当前 clang-format 版本
echo "Using $(clang-format-18 --version)"

# 查找 .cpp/.hpp/.h/.cc/.cxx 文件并格式化
echo "Formatting all C/C++ source files..."
find . \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" -o -name "*.cxx" \) \
    -not -path "./build/*" \
    -not -path "./cmake-build-*/*" \
    | xargs clang-format-18 -i

echo "Format complete."
