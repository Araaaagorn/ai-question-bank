#!/usr/bin/env bash
# 认证测试 case：登录失败场景（错误密码 / 不存在用户 / 缺少字段）
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

# 检查点 1：密码错误
HTTP_CODE=$(curl -s -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:$PORT/api/v1/auth/login" \
    -H 'Content-Type: application/json' \
    -d '{"username":"admin","password":"wrong"}')
[ "$HTTP_CODE" = "401" ] && ok "错误密码返回 401" || fail "错误密码期望 401 实际 $HTTP_CODE"

# 检查点 2：不存在的用户
HTTP_CODE=$(curl -s -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:$PORT/api/v1/auth/login" \
    -H 'Content-Type: application/json' \
    -d '{"username":"hacker","password":"x"}')
[ "$HTTP_CODE" = "401" ] && ok "不存在用户返回 401（防枚举）" || fail "不存在用户期望 401 实际 $HTTP_CODE"

# 检查点 3：缺少字段
HTTP_CODE=$(curl -s -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:$PORT/api/v1/auth/login" \
    -H 'Content-Type: application/json' \
    -d '{"username":"admin"}')
[ "$HTTP_CODE" = "400" ] && ok "缺少字段返回 400" || fail "缺少字段期望 400 实际 $HTTP_CODE"

[ "$FAIL" -eq 0 ] || exit 1