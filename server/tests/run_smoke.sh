#!/usr/bin/env bash
# 冒烟测试：构建后启动服务 → 健康检查 → 静态页检查 → 关闭
# 由 `make test` 调用（工作目录为 server/）
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${TEST_PORT:-8099}"
export PORT
export DATABASE_PATH="data/test_smoke.db"

./ai-question-bank-server >/tmp/aqb_server.log 2>&1 &
PID=$!
trap 'kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true' EXIT

# 等待服务就绪（最多 6 秒）
for _ in $(seq 1 20); do
    if curl -s "http://127.0.0.1:$PORT/api/v1/health" >/dev/null 2>&1; then break; fi
    sleep 0.3
done

BODY=$(curl -s "http://127.0.0.1:$PORT/api/v1/health")
echo "health -> $BODY"
echo "$BODY" | grep -q '"status":"ok"' || {
    echo "✘ 健康检查失败"; cat /tmp/aqb_server.log; exit 1
}

CODE=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/")
[ "$CODE" = "200" ] || {
    echo "✘ 静态页访问失败 HTTP $CODE"; cat /tmp/aqb_server.log; exit 1
}
echo "✔ 冒烟测试通过（健康检查 + 静态页）"
