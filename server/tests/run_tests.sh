#!/usr/bin/env bash
# ============================================================================
# 测试运行器：管理服务生命周期 + 以目录为单位调用测试 case
#
# 用法：
#   bash tests/run_tests.sh --all              # 运行全部测试目录
#   bash tests/run_tests.sh auth               # 运行指定测试目录
#   bash tests/run_tests.sh health auth        # 运行多个测试目录
#
# 每个测试目录下包含 case_*.sh，每个 case 独立运行、自给自足。
# 环境变量：TEST_PORT（默认 8099）、DATABASE_PATH（默认 data/test_all.db）
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

# ── 颜色 ──
GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

PASS=0
FAIL=0
TOTAL_CASES=0

# ── 配置 ──
TEST_PORT="${TEST_PORT:-8099}"
export TEST_PORT
export PORT="$TEST_PORT"
DB_PATH="${DATABASE_PATH:-data/test_all.db}"
export DATABASE_PATH
SERVER_LOG="/tmp/aqb_test_server.log"

# ── 解析参数 ──
if [ $# -eq 0 ]; then
    echo -e "${RED}用法: $0 [--all | dir_name1 dir_name2 ...]${NC}"
    exit 1
fi

if [ "$1" = "--all" ]; then
    # 自动发现 tests/ 下所有包含 case_*.sh 的子目录
    DIRS=()
    for d in tests/*/; do
        dirname=$(basename "$d")
        # 跳过 run_tests.sh 自身的目录（如果有）
        [ "$dirname" = "run_tests" ] && continue
        # 检查是否存在 case_*.sh 文件
        if ls "$d"case_*.sh 2>/dev/null >/dev/null; then
            DIRS+=("$dirname")
        fi
    done
else
    DIRS=("$@")
fi

if [ ${#DIRS[@]} -eq 0 ]; then
    echo -e "${RED}未找到任何测试目录${NC}"
    exit 1
fi

# ── 服务管理 ──

SERVER_PID=""

start_server() {
    rm -f "$DB_PATH"
    kill "$(lsof -t -i ":$TEST_PORT" 2>/dev/null)" 2>/dev/null || true
    sleep 0.3

    ./ai-question-bank-server >"$SERVER_LOG" 2>&1 &
    SERVER_PID=$!

    for _ in $(seq 1 20); do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo -e "${RED}⚠ 服务进程启动失败，日志:${NC}"
            cat "$SERVER_LOG"
            exit 1
        fi
        if curl -s "http://127.0.0.1:$TEST_PORT/api/v1/health" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.3
    done

    echo -e "${RED}⚠ 服务未在 6 秒内就绪，日志:${NC}"
    cat "$SERVER_LOG"
    exit 1
}

stop_server() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$DB_PATH"
    SERVER_PID=""
}

cleanup() { stop_server; }
trap cleanup EXIT

# ── 主流程 ──

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  测试运行器${NC}"
echo -e "${CYAN}  端口: $TEST_PORT  数据库: $DB_PATH${NC}"
echo -e "${CYAN}  测试目录: ${DIRS[*]}${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

start_server

# 以目录为单位循环
for dir_name in "${DIRS[@]}"; do
    dir_path="tests/${dir_name}"
    if [ ! -d "$dir_path" ]; then
        echo -e "${RED}[FAIL] 测试目录不存在: $dir_path${NC}"
        FAIL=$((FAIL + 1))
        continue
    fi

    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}  测试组: ${dir_name}${NC}"
    echo -e "${CYAN}========================================${NC}"

    dir_case_pass=0
    dir_case_fail=0

    # 遍历该目录下所有 case_*.sh（按文件名排序）
    for case_script in $(ls "$dir_path"/case_*.sh 2>/dev/null | sort); do
        case_name=$(basename "$case_script" .sh)
        echo -e "${CYAN}  --- ${dir_name}/${case_name} ---${NC}"

        set +e
        bash "$case_script"
        rc=$?
        set -euo pipefail

        # 检查服务是否还在运行
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo -e "${RED}⚠ 服务已崩溃，尝试重启...${NC}"
            SERVER_PID=""
            start_server
        fi

        if [ "$rc" -eq 0 ]; then
            echo -e "  ${GREEN}[PASS] ${dir_name}/${case_name}${NC}"
            dir_case_pass=$((dir_case_pass + 1))
        else
            echo -e "  ${RED}[FAIL] ${dir_name}/${case_name}${NC}"
            dir_case_fail=$((dir_case_fail + 1))
        fi
        echo ""
    done

    if [ "$dir_case_fail" -eq 0 ]; then
        echo -e "  ${GREEN}[PASS] 测试组 ${dir_name} ($dir_case_pass 个 case 全部通过)${NC}"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}[FAIL] 测试组 ${dir_name} ($dir_case_pass 通过, $dir_case_fail 失败)${NC}"
        FAIL=$((FAIL + 1))
    fi
    TOTAL_CASES=$((TOTAL_CASES + dir_case_pass + dir_case_fail))
    echo ""
done

stop_server

# ── 汇总 ──
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  测试汇总${NC}"
echo -e "${CYAN}========================================${NC}"
echo -e "  case 通过: ${GREEN}${TOTAL_CASES}${NC}"
echo -e "  测试组通过: ${GREEN}${PASS}${NC}  测试组失败: ${RED}${FAIL}${NC}"

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}✔ 全部测试通过${NC}"
    exit 0
else
    echo -e "${RED}✘ 存在失败的测试组${NC}"
    exit 1
fi