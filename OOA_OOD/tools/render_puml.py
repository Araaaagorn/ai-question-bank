#!/usr/bin/env python3
"""批量渲染 diagrams/ 下的 PlantUML 源文件为 SVG（文档引用）与 PNG（位图备用）。

渲染通道（按顺序尝试，需要外网连通）：
  1) www.plantuml.com 官方服务（GET，PlantUML 压缩编码）——速度快，PNG/SVG 均稳定；
  2) kroki.io（POST 明文）——备用通道。kroki 的 SVG 很快，但 PNG 经常握手超时，
     故 PNG 优先走 plantuml.com。

用法：
    py -u tools/render_puml.py            # 渲染全部
    py -u tools/render_puml.py seq1       # 只渲染文件名包含 seq1 的图
    py -u tools/render_puml.py --svg-only # 只渲染 SVG（快速语法校验）

退出码：0 = 全部成功；1 = 存在失败（失败明细见输出与 *.svg.err / *.png.err）。
"""
import pathlib
import sys
import time
import urllib.error
import urllib.request
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent / "diagrams"
PLANTUML_URL = "https://www.plantuml.com/plantuml/{fmt}/{code}"
KROKI_URL = "https://kroki.io/plantuml/{fmt}"
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
ALPHA = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_"


# ── PlantUML 官方文本编码：UTF-8 → raw deflate → 6bit 自定义 base64 ──
def _append3(b1: int, b2: int, b3: int) -> str:
    c1 = b1 >> 2
    c2 = ((b1 & 0x3) << 4) | (b2 >> 4)
    c3 = ((b2 & 0xF) << 2) | (b3 >> 6)
    c4 = b3 & 0x3F
    return ALPHA[c1] + ALPHA[c2] + ALPHA[c3] + ALPHA[c4]


def plantuml_encode(text: str) -> str:
    data = zlib.compress(text.encode("utf-8"), 9)[2:-4]
    out = []
    for i in range(0, len(data), 3):
        chunk = data[i:i + 3]
        if len(chunk) == 3:
            out.append(_append3(chunk[0], chunk[1], chunk[2]))
        elif len(chunk) == 2:
            out.append(_append3(chunk[0], chunk[1], 0))
        else:
            out.append(_append3(chunk[0], 0, 0))
    return "".join(out)


def _get(url: str, timeout: int) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "*/*"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def _post(url: str, src: bytes, timeout: int) -> bytes:
    req = urllib.request.Request(
        url, data=src,
        headers={"User-Agent": UA, "Content-Type": "text/plain; charset=utf-8", "Accept": "*/*"},
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


class SyntaxError_(RuntimeError):
    """客户端 4xx：语法错误，重试与换通道都无意义。"""


def _err_body(exc) -> str:
    """安全读取 HTTPError 响应体（读取本身也可能超时/截断）。"""
    try:
        return exc.read()[:400].decode("utf-8", "replace")
    except Exception:  # noqa: BLE001
        return "<error body unavailable>"


def _via_plantuml(text: str, fmt: str) -> bytes:
    code = plantuml_encode(text)
    last = None
    for attempt in range(3):
        try:
            return _get(PLANTUML_URL.format(fmt=fmt, code=code), timeout=120)
        except urllib.error.HTTPError as exc:
            detail = _err_body(exc)
            low = detail.lower()
            # 真正的 PlantUML 语法错误会在响应体里给出 "[From string ...]" / "Syntax Error"；
            # 而 Tomcat 对超长 URL 返回的是 HTML 错误页（HTTP Status 400），此时应回退到 kroki。
            if exc.code in (400, 413, 414) and ("syntax error" in low or "from string" in low
                                                or "error 400" in low or "allowmixing" in low):
                raise SyntaxError_(f"plantuml.com HTTP {exc.code}: {detail}") from exc
            last = f"plantuml.com HTTP {exc.code}: {detail}"
            if exc.code in (400, 413, 414):
                break  # URL 过长等客户端错误，重试无意义，直接回退 kroki
        except Exception as exc:  # noqa: BLE001
            last = f"plantuml.com {type(exc).__name__}: {exc}"
        time.sleep(2 * (attempt + 1))
    raise RuntimeError(last or "plantuml.com unknown failure")


def _via_kroki(src: bytes, fmt: str) -> bytes:
    last = None
    for attempt in range(3):
        try:
            return _post(KROKI_URL.format(fmt=fmt), src, timeout=180)
        except urllib.error.HTTPError as exc:
            detail = _err_body(exc)
            if exc.code in (400, 413):
                raise SyntaxError_(f"kroki HTTP {exc.code}: {detail}") from exc
            last = f"kroki HTTP {exc.code}: {detail}"
        except Exception as exc:  # noqa: BLE001
            last = f"kroki {type(exc).__name__}: {exc}"
        time.sleep(2 * (attempt + 1))
    raise RuntimeError(last or "kroki unknown failure")


def render(text: str, fmt: str) -> tuple[bytes, str]:
    """返回 (payload, 通道名)。plantuml.com 优先，失败则回退 kroki。"""
    try:
        return _via_plantuml(text, fmt), "plantuml.com"
    except SyntaxError_:
        raise
    except RuntimeError as first:
        try:
            return _via_kroki(text.encode("utf-8"), fmt), "kroki"
        except SyntaxError_:
            raise
        except RuntimeError as second:
            raise RuntimeError(f"{first} | fallback {second}") from second


def main() -> int:
    args = list(sys.argv[1:])
    svg_only = "--svg-only" in args
    png_only = "--png-only" in args
    stale_only = "--stale" in args
    args = [a for a in args if not a.startswith("--")]
    only = args[0] if args else None

    files = sorted(ROOT.rglob("*.puml"))
    if only:
        files = [p for p in files if only in p.name]
    if not files:
        print("no .puml files found under", ROOT)
        return 1

    if svg_only:
        formats = [("svg", ".svg", b"<svg")]
    elif png_only:
        formats = [("png", ".png", b"\x89PNG")]
    else:
        formats = [("svg", ".svg", b"<svg"), ("png", ".png", b"\x89PNG")]

    ok = {fmt: 0 for fmt, _, _ in formats}
    skipped = 0
    failures = []
    for path in files:
        text = None
        for fmt, ext, marker in formats:
            out = path.with_suffix(ext)
            # --stale：跳过“产物已存在且不早于源文件”的项，只补渲染缺失/过期的输出
            if stale_only and out.exists() and out.stat().st_mtime >= path.stat().st_mtime:
                ok[fmt] += 1
                skipped += 1
                print("SKIP", path.name, ext, "(already up to date)", flush=True)
                continue
            if text is None:
                text = path.read_text(encoding="utf-8")
            try:
                data, channel = render(text, fmt)
            except (RuntimeError, SyntaxError_) as exc:
                failures.append((path.name, fmt, str(exc)[:300]))
                print("FAIL", path.name, fmt, str(exc)[:300], flush=True)
                continue
            if marker not in data[:64] or b"syntax error" in data.lower():
                err_file = out.with_suffix(out.suffix + ".err")
                err_file.write_bytes(data[:4000])
                failures.append((path.name, fmt, "syntax error / bad payload, see " + err_file.name))
                print("FAIL", path.name, fmt, "syntax error (saved", err_file.name + ")", flush=True)
                continue
            out.write_bytes(data)
            ok[fmt] += 1
            print("OK  ", path.name, "->", out.name, len(data), "bytes via", channel, flush=True)

    print("=" * 64, flush=True)
    print("puml files selected:", len(files), "(skipped up-to-date outputs:", skipped, ")", flush=True)
    for fmt, _, _ in formats:
        print(f"  {fmt}: {ok[fmt]}/{len(files)} ok", flush=True)
    print("failures:", len(failures), flush=True)
    for name, fmt, msg in failures:
        print("  -", name, fmt, msg[:160], flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
