#!/usr/bin/env python3
"""批量渲染 diagrams/ 下的 PlantUML 源文件为 SVG（文档引用）与 PNG（自检用）。

渲染服务：kroki.io（POST 明文）。需要外网连通。
用法：
    py tools/render_puml.py            # 渲染全部
    py tools/render_puml.py seq1       # 只渲染文件名包含 seq1 的图
"""
import pathlib
import sys
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent / "diagrams"
URL = "https://kroki.io/plantuml/{fmt}"
HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
    "Content-Type": "text/plain; charset=utf-8",
    "Accept": "*/*",
}


def render(src: bytes, fmt: str) -> bytes:
    last_err = None
    for attempt in range(3):
        try:
            req = urllib.request.Request(URL.format(fmt=fmt), data=src, headers=HEADERS)
            with urllib.request.urlopen(req, timeout=90) as resp:
                return resp.read()
        except urllib.error.HTTPError as exc:
            detail = exc.read()[:600].decode("utf-8", "replace")
            if exc.code in (400, 413):  # 语法错误等客户端错误，重试无意义
                raise RuntimeError(f"HTTP {exc.code}: {detail}") from exc
            last_err = f"HTTP {exc.code}: {detail}"
        except Exception as exc:  # noqa: BLE001
            last_err = f"{type(exc).__name__}: {exc}"
        time.sleep(2 * (attempt + 1))
    raise RuntimeError(f"render failed after retries: {last_err}")


def main() -> int:
    only = sys.argv[1] if len(sys.argv) > 1 else None
    files = sorted(ROOT.rglob("*.puml"))
    if not files:
        print("no .puml files found under", ROOT)
        return 1
    failures = []
    for path in files:
        if only and only not in path.name:
            continue
        text = path.read_text(encoding="utf-8")
        for fmt, ext, marker in (("svg", ".svg", b"<svg"), ("png", ".png", b"\x89PNG")):
            out = path.with_suffix(ext)
            try:
                data = render(text.encode("utf-8"), fmt)
            except RuntimeError as exc:
                failures.append((path.name, fmt, str(exc)[:300]))
                print("FAIL", path.name, fmt, str(exc)[:300])
                continue
            lowered = data.lower()
            if marker not in data[:64] or b"syntax error" in lowered:
                err_file = out.with_suffix(out.suffix + ".err")
                err_file.write_bytes(data[:4000])
                failures.append((path.name, fmt, "syntax error / bad payload, see " + err_file.name))
                print("FAIL", path.name, fmt, "syntax error (saved", err_file.name + ")")
                continue
            out.write_bytes(data)
            print("OK  ", path.name, "->", out.name, len(data), "bytes")
    print("=" * 60)
    print("rendered:", len(files) - len({f[0] for f in failures}), "files;", "failures:", len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
