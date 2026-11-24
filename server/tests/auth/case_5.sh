#!/usr/bin/env bash
# 注册测试 case：各类角色注册成功
set -euo pipefail

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PASS=0
FAIL=0

ok()   { PASS=$((PASS+1)); echo -e "  ${GREEN}[PASS]${NC} $1"; }
fail() { FAIL=$((FAIL+1)); echo -e "  ${RED}[FAIL]${NC} $1"; }

PORT="${TEST_PORT:-${PORT:-8099}}"

# 检查点 1：不传 role，默认 student
BODY=$(curl -s --noproxy '*' -X POST "http://127.0.0.1:$PORT/api/v1/auth/register" \
    -H 'Content-Type: application/json' \
    -d '{"username":"regstu1","password":"mypass123","confirm_password":"mypass123"}')
echo "$BODY" | grep -q '"role":"student"' && ok "不传 role 默认 student" || fail "默认 role 非 student（响应: $BODY）"
echo "$BODY" | grep -q '"token"' && ok "注册返回 token" || fail "注册未返回 token（响应: $BODY）"
echo "$BODY" | grep -q '"username":"regstu1"' && ok "注册返回正确 username" || fail "username 不符（响应: $BODY）"

# 检查点 2：传 role=student
BODY=$(curl -s --noproxy '*' -X POST "http://127.0.0.1:$PORT/api/v1/auth/register" \
    -H 'Content-Type: application/json' \
    -d '{"username":"regstu2","password":"mypass123","confirm_password":"mypass123","role":"student"}')
echo "$BODY" | grep -q '"role":"student"' && ok "注册 role=student 成功" || fail "role=student 失败（响应: $BODY）"

# 检查点 3：自助注册 role=teacher 被拒（仅学生可自助注册）
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' -X POST \
    "http://127.0.0.1:$PORT/api/v1/auth/register" \
    -H 'Content-Type: application/json' \
    -d '{"username":"regtch1","password":"mypass123","confirm_password":"mypass123","role":"teacher"}')
[ "$HTTP_CODE" = "400" ] && ok "自助注册 role=teacher 返回 400" || fail "role=teacher 期望 400 实际 $HTTP_CODE"

# 检查点 4：自助注册 role=admin 被拒（仅学生可自助注册）
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' -X POST \
    "http://127.0.0.1:$PORT/api/v1/auth/register" \
    -H 'Content-Type: application/json' \
    -d '{"username":"regadm1","password":"mypass123","confirm_password":"mypass123","role":"admin"}')
[ "$HTTP_CODE" = "400" ] && ok "自助注册 role=admin 返回 400" || fail "role=admin 期望 400 实际 $HTTP_CODE"

[ "$FAIL" -eq 0 ] || exit 1