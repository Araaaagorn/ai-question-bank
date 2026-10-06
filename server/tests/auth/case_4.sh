#!/usr/bin/env bash
# 认证测试 case：登出（登出 / 登出后 token 失效）
# 独立测试 case，由 run_tests.sh 以目录为单位调用
set -euo pipefail

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PASS=0
FAIL=0

ok()   { PASS=$((PASS+1)); echo -e "  ${GREEN}[PASS]${NC} $1"; }
fail() { FAIL=$((FAIL+1)); echo -e "  ${RED}[FAIL]${NC} $1"; }

PORT="${TEST_PORT:-${PORT:-8099}}"

# 先登录获取 token（本 case 自给自足，不依赖外部状态）
BODY=$(curl -s --noproxy '*' -X POST "http://127.0.0.1:$PORT/api/v1/auth/login" \
    -H 'Content-Type: application/json' \
    -d '{"username":"admin","password":"admin123"}')
ADMIN_TOKEN=$(echo "$BODY" | sed -n 's/.*"token"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')
if [ -z "$ADMIN_TOKEN" ]; then
    fail "无法获取 admin token（登录接口异常），跳过后续登出测试"
    exit 1
fi

# 检查点 1：登出返回 200
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:$PORT/api/v1/auth/logout" \
    -H "Authorization: Bearer $ADMIN_TOKEN")
[ "$HTTP_CODE" = "200" ] && ok "登出返回 200" || fail "登出期望 200 实际 $HTTP_CODE"

# 检查点 2：登出后原 token 失效
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/auth/me" \
    -H "Authorization: Bearer $ADMIN_TOKEN")
[ "$HTTP_CODE" = "401" ] && ok "登出后 token 失效返回 401" || fail "登出后 token 仍有效"

[ "$FAIL" -eq 0 ] || exit 1