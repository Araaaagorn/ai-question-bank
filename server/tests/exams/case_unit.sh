#!/usr/bin/env bash
# exams 存储层单元测试 case（Phase 1，无需启动服务器进程）
set -euo pipefail

cd "$(dirname "$0")/../.."

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

BIN="${AQB_TEST_BIN_DIR:?缺少 AQB_TEST_BIN_DIR（请通过 run_tests.sh 执行）}/exams_unit"

if [ ! -x "$BIN" ]; then
    echo -e "  ${RED}[FAIL] 测试二进制不存在: $BIN${NC}"
    exit 1
fi

if "$BIN"; then
    echo -e "  ${GREEN}[PASS] exams 存储层单元测试全部通过${NC}"
else
    echo -e "  ${RED}[FAIL] exams 存储层单元测试存在失败${NC}"
    exit 1
fi
