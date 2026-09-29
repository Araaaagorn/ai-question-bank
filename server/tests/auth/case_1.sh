#!/usr/bin/env bash
# 认证测试 case：三个预设账号登录成功
# 独立测试 case，由 run_tests.sh 以目录为单位调用
set -euo pipefail

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PASS=0
FAIL=0

ok()   { PASS=$((PASS+1)); echo -e "  ${GREEN}[PASS]${NC} $1"; }
fail() { FAIL=$((FAIL+1)); echo -e "  ${RED}[FAIL]${NC} $1"; }

PORT="${TEST_PORT:-8099}"

# 检查点 1：admin 登录
BODY=$(curl -s -X POST "http://127.0.0.1:$PORT/api/v1/auth/login" \
    -H 'Content-Type: application/json' \
    -d '{"username":"admin","password":"admin123"}')
echo "$BODY" | grep -q '"role":"admin"' && ok "admin 登录成功" || fail "admin 登录失败（响应: $BODY）"
echo "$BODY" | grep -q '"token"' && ok "admin 返回了 token" || fail "admin 未返回 token"

# 检查点 2：teacher 登录
BODY=$(curl -s -X POST "http://127.0.0.1:$PORT/api/v1/auth/login" \
    -H 'Content-Type: application/json' \
    -d '{"username":"teacher","password":"teacher123"}')
echo "$BODY" | grep -q '"role":"teacher"' && ok "teacher 登录成功" || fail "teacher 登录失败（响应: $BODY）"

# 检查点 3：student 登录
BODY=$(curl -s -X POST "http://127.0.0.1:$PORT/api/v1/auth/login" \
    -H 'Content-Type: application/json' \
    -d '{"username":"student","password":"student123"}')
echo "$BODY" | grep -q '"role":"student"' && ok "student 登录成功" || fail "student 登录失败（响应: $BODY）"

[ "$FAIL" -eq 0 ] || exit 1