#!/usr/bin/env bash
# 管理员创建账号测试：admin 可创建 teacher/student，非 admin 拒绝
set -euo pipefail

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PASS=0
FAIL=0

ok()   { PASS=$((PASS+1)); echo -e "  ${GREEN}[PASS]${NC} $1"; }
fail() { FAIL=$((FAIL+1)); echo -e "  ${RED}[FAIL]${NC} $1"; }

PORT="${TEST_PORT:-${PORT:-8099}}"
BASE="http://127.0.0.1:$PORT"

token_of() {
    curl -s --noproxy '*' -X POST "$BASE/api/v1/auth/login" \
        -H 'Content-Type: application/json' \
        -d "{\"username\":\"$1\",\"password\":\"$2\"}" \
        | sed -n 's/.*"token"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p'
}

ADMIN_TOKEN=$(token_of admin admin123)
TEACHER_TOKEN=$(token_of teacher teacher123)
STUDENT_TOKEN=$(token_of student student123)
[ -n "$ADMIN_TOKEN" ] || { fail "admin 登录失败"; exit 1; }

# 检查点 1：admin 创建 teacher（201 + role=teacher）
BODY=$(curl -s --noproxy '*' -X POST "$BASE/api/v1/admin/users" \
    -H 'Content-Type: application/json' -H "Authorization: Bearer $ADMIN_TOKEN" \
    -d '{"username":"newteacher","password":"mypass123","confirm_password":"mypass123","role":"teacher"}')
echo "$BODY" | grep -q '"role":"teacher"' && ok "admin 创建 teacher 成功" || fail "创建 teacher 失败（响应: $BODY）"

# 检查点 2：新建 teacher 可登录
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' -X POST "$BASE/api/v1/auth/login" \
    -H 'Content-Type: application/json' \
    -d '{"username":"newteacher","password":"mypass123"}')
[ "$HTTP_CODE" = "200" ] && ok "新建 teacher 可登录" || fail "新建 teacher 登录失败（$HTTP_CODE）"

# 检查点 3：admin 创建 student（201）
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' -X POST "$BASE/api/v1/admin/users" \
    -H 'Content-Type: application/json' -H "Authorization: Bearer $ADMIN_TOKEN" \
    -d '{"username":"newstudent","password":"mypass123","confirm_password":"mypass123","role":"student"}')
[ "$HTTP_CODE" = "201" ] && ok "admin 创建 student 成功" || fail "创建 student 失败（$HTTP_CODE）"

# 检查点 4：teacher 调管理员接口 → 403
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' -X POST "$BASE/api/v1/admin/users" \
    -H 'Content-Type: application/json' -H "Authorization: Bearer $TEACHER_TOKEN" \
    -d '{"username":"x1","password":"mypass123","confirm_password":"mypass123","role":"teacher"}')
[ "$HTTP_CODE" = "403" ] && ok "teacher 调管理员接口返回 403" || fail "teacher 期望 403 实际 $HTTP_CODE"

# 检查点 5：student 调管理员接口 → 403
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' -X POST "$BASE/api/v1/admin/users" \
    -H 'Content-Type: application/json' -H "Authorization: Bearer $STUDENT_TOKEN" \
    -d '{"username":"x2","password":"mypass123","confirm_password":"mypass123","role":"teacher"}')
[ "$HTTP_CODE" = "403" ] && ok "student 调管理员接口返回 403" || fail "student 期望 403 实际 $HTTP_CODE"

# 检查点 6：未登录 → 401
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' -X POST "$BASE/api/v1/admin/users" \
    -H 'Content-Type: application/json' \
    -d '{"username":"x3","password":"mypass123","confirm_password":"mypass123","role":"teacher"}')
[ "$HTTP_CODE" = "401" ] && ok "未登录调管理员接口返回 401" || fail "未登录期望 401 实际 $HTTP_CODE"

# 检查点 7：无效 role → 400
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' -X POST "$BASE/api/v1/admin/users" \
    -H 'Content-Type: application/json' -H "Authorization: Bearer $ADMIN_TOKEN" \
    -d '{"username":"x4","password":"mypass123","confirm_password":"mypass123","role":"superadmin"}')
[ "$HTTP_CODE" = "400" ] && ok "无效 role 返回 400" || fail "无效 role 期望 400 实际 $HTTP_CODE"

# 检查点 8：重复用户名 → 409
HTTP_CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' -X POST "$BASE/api/v1/admin/users" \
    -H 'Content-Type: application/json' -H "Authorization: Bearer $ADMIN_TOKEN" \
    -d '{"username":"newteacher","password":"mypass123","confirm_password":"mypass123","role":"teacher"}')
[ "$HTTP_CODE" = "409" ] && ok "重复用户名返回 409" || fail "重复用户名期望 409 实际 $HTTP_CODE"

[ "$FAIL" -eq 0 ] || exit 1
