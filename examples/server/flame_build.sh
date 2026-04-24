#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

PROF_FILE="./server.prof"
SVG_FILE="./flame.svg"
FLAMEGRAPH_DIR="$HOME/FlameGraph"

# 检查 prof 文件
if [ ! -f "$PROF_FILE" ]; then
    echo "[flame_build.sh] 找不到 $PROF_FILE，请先运行 run.sh 并完成压测"
    exit 1
fi

# 检查 server 二进制
if [ ! -f "./server" ]; then
    echo "[flame_build.sh] 找不到 server 二进制，请先 make"
    exit 1
fi

# 检查 FlameGraph，没有就自动拉取
if [ ! -f "$FLAMEGRAPH_DIR/flamegraph.pl" ]; then
    echo "[flame_build.sh] 未找到 FlameGraph，正在克隆..."
    git clone --depth=1 https://github.com/brendangregg/FlameGraph "$FLAMEGRAPH_DIR"
fi

# 检查 google-pprof
if ! command -v google-pprof &>/dev/null; then
    echo "[flame_build.sh] 未找到 google-pprof，请安装: sudo apt install google-perftools"
    exit 1
fi

echo "[flame_build.sh] 生成火焰图..."
google-pprof --collapsed ./server "$PROF_FILE" \
    | "$FLAMEGRAPH_DIR/flamegraph.pl" \
    > "$SVG_FILE"

echo "[flame_build.sh] 完成：$SVG_FILE"

# WSL 下尝试用 Windows 浏览器打开
if command -v explorer.exe &>/dev/null; then
    explorer.exe "$(wslpath -w "$SVG_FILE")"
fi
