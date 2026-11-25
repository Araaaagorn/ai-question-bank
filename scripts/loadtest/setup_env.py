#!/usr/bin/env python3
"""搭建 chat / 试卷压测环境（platform 路由模式），输出 env.json。

流程：
1. 登录 teacher / student；
2. teacher 创建一份 active 试卷 → 供 exams / exam_export 场景；
3. （可选 --grant-db）调用 scripts/ai_access.py 给 student 授予平台 AI 授权，
   使 chat_submit 能走到内部 HTTP 调用（由 mock 慢响应模拟阻塞）。

用法：
  python3 scripts/loadtest/setup_env.py --url http://127.0.0.1:18080 \
      --grant-db server/data/bench_baseline.db --out tmp/loadtest/env.json
"""
import argparse
import http.client
import json
import os
import subprocess
import sys
import urllib.parse


def req(host, port, method, path, token=None, body=None):
    conn = http.client.HTTPConnection(host, port, timeout=15)
    headers = {}
    if token:
        headers["Authorization"] = "Bearer " + token
    data = None
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    conn.request(method, path, body=data, headers=headers)
    resp = conn.getresponse()
    payload = resp.read().decode("utf-8", "replace")
    status = resp.status
    conn.close()
    return status, payload


def fail(step, status, payload):
    print(f"✘ {step}: HTTP {status} {payload}", file=sys.stderr)
    raise SystemExit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:18080")
    ap.add_argument("--grant-db", default=None,
                    help="可选：platform 模式的数据库路径，用于授予学生 AI 授权")
    ap.add_argument("--out", default="tmp/loadtest/env.json")
    args = ap.parse_args()
    u = urllib.parse.urlsplit(args.url)
    host, port = u.hostname or "127.0.0.1", u.port or 80

    st, b = req(host, port, "POST", "/api/v1/auth/login",
                body={"username": "teacher", "password": "teacher123"})
    if st != 200:
        fail("教师登录", st, b)
    teacher_token = json.loads(b)["token"]

    st, b = req(host, port, "POST", "/api/v1/auth/login",
                body={"username": "student", "password": "student123"})
    if st != 200:
        fail("学生登录", st, b)
    student_token = json.loads(b)["token"]

    st, b = req(host, port, "POST", "/api/v1/exams", token=teacher_token,
                body={"name": "压测试卷", "description": "loadtest baseline"})
    if st not in (200, 201):
        fail("教师创建试卷", st, b)
    exam_id = json.loads(b)["exam"]["id"]

    if args.grant_db:
        subprocess.run(
            [sys.executable, "scripts/ai_access.py", "--db", args.grant_db,
             "grant", "--username", "student", "--reason", "loadtest"],
            check=True)

    env = {
        "base_url": args.url,
        "teacher_token": teacher_token,
        "student_token": student_token,
        "exam_id": exam_id,
    }
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(env, f, ensure_ascii=False, indent=2)
    print(json.dumps(env, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
