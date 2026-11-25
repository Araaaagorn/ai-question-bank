#!/usr/bin/env python3
"""AI+题库平台 · 压测基线工具（纯标准库，无外部依赖）。

在无 wrk / hey / ab 的课程服务器上，用「固定并发 + 固定时长（或固定请求数）」
复现压测，输出吞吐(RPS)、延迟分位数、状态码分布与错误率。

场景（scenario）：
  health        GET  /api/v1/health                      无鉴权
  login         POST /api/v1/auth/login                  demo 账号，每次请求新建会话
  questions     GET  /api/v1/questions                   Bearer token（setup 阶段登录一次）
  chat_submit   POST /api/v1/chat                        Bearer token + question_id

用法示例：
  python3 scripts/loadtest/bench.py --url http://127.0.0.1:18080 \
      --scenario health --concurrency 16 --duration 10

  python3 scripts/loadtest/bench.py --url http://127.0.0.1:18080 \
      --scenario login --concurrency 16 --requests 200

  python3 scripts/loadtest/bench.py --url http://127.0.0.1:18080 \
      --scenario questions --concurrency 32 --duration 10

  python3 scripts/loadtest/bench.py --url http://127.0.0.1:18080 \
      --scenario chat_submit --concurrency 16 --duration 10 --json-out result.json
"""

import argparse
import http.client
import json
import sys
import threading
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from urllib.parse import urlsplit

DEFAULT_DEMO_USER = "student"
DEFAULT_DEMO_PASS = "student123"
QUESTION_ID = 1


def pct(sorted_vals, p):
    """线性插值分位数；空序列返回 None。"""
    if not sorted_vals:
        return None
    k = (len(sorted_vals) - 1) * p
    lo = int(k)
    hi = lo + 1 if lo + 1 < len(sorted_vals) else lo
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def login_token(host, port, user, password):
    """执行一次登录，返回 token 或 None。"""
    try:
        conn = http.client.HTTPConnection(host, port, timeout=10)
        payload = json.dumps({"username": user, "password": password})
        conn.request("POST", "/api/v1/auth/login", body=payload,
                     headers={"Content-Type": "application/json"})
        resp = conn.getresponse()
        status = resp.status
        body = resp.read().decode("utf-8", "replace")
        conn.close()
        if status == 200:
            return json.loads(body).get("token")
    except Exception:
        pass
    return None


def build_request(scenario, token, question_id, exam_id):
    """返回 (method, path, body_bytes, headers)。"""
    if scenario == "health":
        return "GET", "/api/v1/health", None, {}
    if scenario == "login":
        body = json.dumps({"username": DEFAULT_DEMO_USER,
                           "password": DEFAULT_DEMO_PASS}).encode("utf-8")
        return "POST", "/api/v1/auth/login", body, {"Content-Type": "application/json"}
    if scenario == "questions":
        return "GET", "/api/v1/questions", None, {"Authorization": "Bearer " + token}
    if scenario == "exams":
        return "GET", "/api/v1/exams", None, {"Authorization": "Bearer " + token}
    if scenario == "exam_export":
        if exam_id is None:
            raise ValueError("exam_export 场景需要 --exam-id")
        return "GET", "/api/v1/exams/%d/export/markdown" % exam_id, None, \
            {"Authorization": "Bearer " + token}
    if scenario == "chat_submit":
        body = json.dumps({"question_id": question_id,
                           "message": "请讲解这道题",
                           "mode": "solve"}).encode("utf-8")
        return "POST", "/api/v1/chat", body, {
            "Content-Type": "application/json",
            "Authorization": "Bearer " + token,
        }
    raise ValueError("unknown scenario: " + scenario)


class Stopwatch:
    """统一控制停止条件：时长截止 或 请求数耗尽。"""

    def __init__(self, duration, requests):
        self.deadline = time.time() + duration if duration else None
        self.remaining = requests if requests else None
        self.lock = threading.Lock()
        self.started = time.time()

    def next(self):
        """返回 True 表示可以继续发下一个请求。"""
        if self.deadline is not None and time.time() >= self.deadline:
            return False
        if self.remaining is not None:
            with self.lock:
                if self.remaining <= 0:
                    return False
                self.remaining -= 1
        return True

    def elapsed(self):
        return time.time() - self.started


def run_worker(host, port, scenario, token, question_id, exam_id, stop, per_request_timeout):
    samples = []  # list of (latency_ms, status_int_or_None)
    conn = http.client.HTTPConnection(host, port, timeout=per_request_timeout)
    method, path, body, headers = build_request(scenario, token, question_id, exam_id)
    try:
        while stop.next():
            t0 = time.perf_counter()
            status = None
            try:
                conn.request(method, path, body=body, headers=headers)
                resp = conn.getresponse()
                status = resp.status
                resp.read()  # 消费响应体，释放连接供 keep-alive 复用
            except Exception:
                try:
                    conn.close()
                except Exception:
                    pass
                conn = http.client.HTTPConnection(host, port, timeout=per_request_timeout)
                status = None
            dt = (time.perf_counter() - t0) * 1000.0
            samples.append((dt, status))
    finally:
        try:
            conn.close()
        except Exception:
            pass
    return samples


def main():
    ap = argparse.ArgumentParser(description="AI+题库平台压测基线工具（纯标准库）")
    ap.add_argument("--url", required=True, help="服务基址，如 http://127.0.0.1:18080")
    ap.add_argument("--scenario", required=True,
                    choices=["health", "login", "questions", "exams", "exam_export", "chat_submit"])
    ap.add_argument("--question-id", type=int, default=1, help="chat_submit 的题目 id")
    ap.add_argument("--exam-id", type=int, default=None, help="exam_export 的试卷 id")
    ap.add_argument("--concurrency", type=int, default=16, help="并发线程数")
    ap.add_argument("--duration", type=float, default=None, help="持续秒数（与 --requests 二选一）")
    ap.add_argument("--requests", type=int, default=None, help="总请求数（与 --duration 二选一）")
    ap.add_argument("--timeout", type=float, default=10.0, help="单请求 socket 超时秒数")
    ap.add_argument("--user", default=DEFAULT_DEMO_USER, help="demo 账号")
    ap.add_argument("--password", default=DEFAULT_DEMO_PASS, help="demo 密码")
    ap.add_argument("--json-out", default=None, help="把结果写为 JSON 文件")
    args = ap.parse_args()

    if (args.duration is None) == (args.requests is None):
        ap.error("必须且只能指定 --duration 或 --requests 之一")

    parts = urlsplit(args.url)
    host = parts.hostname or "127.0.0.1"
    port = parts.port or 80

    if args.scenario == "exam_export" and args.exam_id is None:
        ap.error("exam_export 场景需要 --exam-id")

    token = None
    if args.scenario in ("questions", "exams", "exam_export", "chat_submit"):
        token = login_token(host, port, args.user, args.password)
        if token is None:
            print("✘ 无法登录获取 token（场景需要鉴权）", file=sys.stderr)
            sys.exit(2)

    stop = Stopwatch(args.duration, args.requests)
    all_samples = []
    with ThreadPoolExecutor(max_workers=args.concurrency) as ex:
        futures = [ex.submit(run_worker, host, port, args.scenario, token,
                             args.question_id, args.exam_id, stop, args.timeout)
                   for _ in range(args.concurrency)]
        for f in futures:
            all_samples.extend(f.result())

    total = len(all_samples)
    elapsed = stop.elapsed()
    latencies = sorted(dt for dt, _ in all_samples)
    conn_errors = sum(1 for _, st in all_samples if st is None)
    ok = sum(1 for _, st in all_samples if st is not None and 200 <= st < 300)
    status_hist = Counter(st for _, st in all_samples)

    rps = total / elapsed if elapsed > 0 else 0.0
    report = {
        "scenario": args.scenario,
        "url": args.url,
        "concurrency": args.concurrency,
        "duration_seconds": round(elapsed, 3),
        "total_requests": total,
        "rps": round(rps, 2),
        "latency_ms": {
            "min": round(latencies[0], 3) if latencies else None,
            "avg": round(sum(latencies) / len(latencies), 3) if latencies else None,
            "p50": round(pct(latencies, 0.50), 3) if latencies else None,
            "p90": round(pct(latencies, 0.90), 3) if latencies else None,
            "p99": round(pct(latencies, 0.99), 3) if latencies else None,
            "max": round(latencies[-1], 3) if latencies else None,
        },
        "http_2xx": ok,
        "connection_errors": conn_errors,
        "status_histogram": {str(k): v for k, v in
                             sorted(status_hist.items(), key=lambda x: (x[0] is None, x[0] or 0))},
    }

    print(f"场景        : {args.scenario}")
    print(f"并发/时长   : {args.concurrency} 线程 / {elapsed:.2f}s")
    print(f"总请求数    : {total}")
    print(f"吞吐        : {rps:.2f} req/s")
    print(f"延迟(ms)    : min={report['latency_ms']['min']} avg={report['latency_ms']['avg']} "
          f"p50={report['latency_ms']['p50']} p90={report['latency_ms']['p90']} "
          f"p99={report['latency_ms']['p99']} max={report['latency_ms']['max']}")
    print(f"HTTP 2xx    : {ok}")
    print(f"连接错误    : {conn_errors}")
    print(f"状态码分布  : {dict(status_hist)}")

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(report, f, ensure_ascii=False, indent=2)
        print(f"结果已写入  : {args.json_out}")


if __name__ == "__main__":
    main()
