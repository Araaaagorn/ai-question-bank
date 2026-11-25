#!/usr/bin/env python3
"""模拟内部 Pi AI 服务的慢响应（用于压测 C 后端的 chat 慢请求阻塞场景）。

不模拟真实 AI 流程：对 `POST /internal/v1/jobs` 仅 sleep 一段 [min,max] 均匀随机延迟，
然后返回 202 + 最小合法任务 JSON，用来让 C 单线程在内部 HTTP 调用上被阻塞。

用法：
  python3 scripts/loadtest/mock_ai_service.py --port 8001 \
      --min-delay-ms 300 --max-delay-ms 1500
"""
import argparse
import json
import random
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path == "/internal/v1/jobs":
            length = int(self.headers.get("Content-Length") or 0)
            if length > 0:
                self.rfile.read(length)
            delay_ms = random.uniform(self.server.min_delay_ms, self.server.max_delay_ms)
            time.sleep(delay_ms / 1000.0)
            self._json(202, {
                "job_id": str(uuid.uuid4()),
                "conversation_id": str(uuid.uuid4()),
                "question_id": 1,
                "status": "queued",
                "mode": "solve",
            })
        else:
            self.send_error(404)

    def do_GET(self):
        if self.path.startswith("/internal/v1/jobs/"):
            self._json(200, {
                "job_id": str(uuid.uuid4()),
                "conversation_id": str(uuid.uuid4()),
                "question_id": 1,
                "status": "queued",
                "mode": "solve",
            })
        else:
            self.send_error(404)

    def _json(self, code, obj):
        data = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, fmt, *args):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8001)
    ap.add_argument("--min-delay-ms", type=float, default=300.0)
    ap.add_argument("--max-delay-ms", type=float, default=1500.0)
    args = ap.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.min_delay_ms = args.min_delay_ms
    server.max_delay_ms = args.max_delay_ms
    print(f"[mock] listening 127.0.0.1:{args.port} delay~U({args.min_delay_ms:.0f},{args.max_delay_ms:.0f})ms",
          flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
