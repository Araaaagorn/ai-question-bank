#!/usr/bin/env bash
# 冒烟测试：健康检查 + 静态页服务
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

# 检查点 1：健康检查
BODY=$(curl -s --noproxy '*' "http://127.0.0.1:$PORT/api/v1/health")
echo "$BODY" | grep -q '"status":"ok"' && ok "健康检查返回 status=ok" || fail "健康检查: $BODY"

# 检查点 2：静态页服务
CODE=$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/")
[ "$CODE" = "200" ] && ok "静态页返回 200" || fail "静态页 HTTP $CODE"

[ "$FAIL" -eq 0 ] || exit 1