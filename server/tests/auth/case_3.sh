#!/usr/bin/env bash
# 认证测试 case：/me 身份查询（有效 token / 无 token / 无效 token）
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

# 先登录获取 token（本 case 自给自足，不依赖外部状态）
BODY=$(curl -s --noproxy '*' -X POST "http://127.0.0.1:$PORT/api/v1/auth/login" \
    -H 'Content-Type: application/json' \
    -d '{"username":"admin","password":"admin123"}')
ADMIN_TOKEN=$(echo "$BODY" | grep -o '"token":"[^"]*"' | cut -d'"' -f4)
if [ -z "$ADMIN_TOKEN" ]; then
    fail "无法获取 admin token（登录接口异常），跳过后续 /me 测试"
    exit 1
fi

# 检查点 1：有效 token 访问 me
BODY=$(curl -s --noproxy '*' "http://127.0.0.1:$PORT/api/v1/auth/me" \
    -H "Authorization: Bearer $ADMIN_TOKEN")
echo "$BODY" | grep -q '"name":"系统管理员"' && ok "me 返回正确用户" || fail "me 结果异常（响应: $BODY）"

# 检查点 2：无 token 访问 me
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/auth/me")
[ "$HTTP_CODE" = "401" ] && ok "无 token 返回 401" || fail "无 token 期望 401 实际 $HTTP_CODE"

# 检查点 3：无效 token 访问 me
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/auth/me" \
    -H "Authorization: Bearer invalidtoken123")
[ "$HTTP_CODE" = "401" ] && ok "无效 token 返回 401" || fail "无效 token 期望 401 实际 $HTTP_CODE"

[ "$FAIL" -eq 0 ] || exit 1