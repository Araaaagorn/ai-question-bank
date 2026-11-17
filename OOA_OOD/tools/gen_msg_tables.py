#!/usr/bin/env python3
"""从 diagrams/ 下的顺序图 .puml 源文件生成《面向对象分析设计文档》模板要求的表格。

生成两类表格（模板 §2.3.2 / §2.3.3、§3.2.2 / §3.2.3）：
  1) 顺序图中的对象与参与者描述：对象类型 | 对象名称 | 是否为主动对象 | 其他相关信息
  2) 对象接收发送消息的描述：编号 | 消息名称 | 是发送还是接收 | 消息类型 |
     直接触发的消息名称列表 | 是否为自接收消息 | 发送对象 | 接收对象
     —— 按对象分组（生命线声明顺序），组内按 autonumber 时间顺序。

这样可保证“图 ↔ 表”严格一致：表格由 .puml 源文件机械导出，而非手工誊写。

约定（与《02》§2.3.3 开头的说明一致）：
  · 编号与 PlantUML autonumber 一致，即所有消息（含返回消息）按出现顺序统一编号；
  · 消息类型：`->` 同步调用；`-->` 返回消息；`->>` 异步消息；`-->>` 异步返回；
    发送者与接收者相同者为自消息（并保留其同步/异步属性）；
  · “直接触发的消息”＝该消息的接收者在其后发出的第一条消息（同一控制流内的紧邻后继）；
    若接收者其后不再发出消息，则记“无”。

用法：
    py -u tools/gen_msg_tables.py seq1_login
    py -u tools/gen_msg_tables.py            # 全部顺序图
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent / "diagrams"

# 生命线声明：actor/participant/boundary/control/entity/database [<<stereotype>>] "label" as ALIAS
DECL = re.compile(
    r"^\s*(actor|participant|boundary|control|entity|database)\s+"
    r"(?P<label>\"[^\"]*\"|[\w\u4e00-\u9fff]+)\s+"
    r"(?:as\s+(?P<alias>\w+)|(?P<alias2>\w+))?\s*"
    r"(?:<<\s*(?P<stereo>[^>]*?)\s*>>)?\s*$"
)
DECL2 = re.compile(
    r"^\s*(actor|participant|boundary|control|entity|database)\s+"
    r"(?P<label>\"[^\"]*\")\s+as\s+(?P<alias>\w+)\s*(?:<<\s*(?P<stereo>[^>]*?)\s*>>)?\s*$"
)
MSG = re.compile(
    r"^\s*(?P<src>[\w\u4e00-\u9fff]+)\s*"
    r"(?P<arrow>-{1,2}>{1,2}|-{1,2}\\\\|<{1,2}-{1,2})"
    r"\s*(?P<dst>[\w\u4e00-\u9fff]+)\s*(?::\s*(?P<text>.*))?$"
)

ARROW_KIND = {
    "->": "同步调用",
    "->>": "异步消息",
    "-->": "返回消息",
    "-->>": "异步返回",
    "<-": "同步调用",
    "<<-": "返回消息",
    "<-<": "异步消息",
}

ACTIVE_STEREO = ("主动类", "thread", "process", "active")


def parse(path: pathlib.Path):
    lines = path.read_text(encoding="utf-8").splitlines()
    order: list[str] = []
    labels: dict[str, str] = {}
    kinds: dict[str, str] = {}
    stereos: dict[str, str] = {}
    msgs: list[dict] = []
    sections: list[str] = []
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("'"):
            continue
        if line.startswith("==") and line.endswith("=="):
            sections.append(line.strip("= ").strip())
            continue
        m = DECL2.match(raw)
        if not m:
            m = DECL.match(raw)
        if m and not MSG.match(raw):
            alias = m.group("alias") or m.group("alias2")
            label = m.group("label").strip('"')
            if alias:
                if alias not in labels:
                    order.append(alias)
                labels[alias] = label
                kinds[alias] = m.group(1)
                stereos[alias] = (m.group("stereo") or "").strip()
            continue
        mm = MSG.match(raw)
        if mm and mm.group("src") in labels and mm.group("dst") in labels:
            msgs.append({
                "src": mm.group("src"),
                "dst": mm.group("dst"),
                "arrow": mm.group("arrow"),
                "text": (mm.group("text") or "").replace("\\n", " ").strip(),
                "section": sections[-1] if sections else "",
            })
    for i, m in enumerate(msgs, 1):
        m["no"] = i
    return order, labels, kinds, stereos, msgs, sections


def triggered(msgs, idx):
    """该消息的接收者在其后发出的第一条消息编号。"""
    cur = msgs[idx]
    for nxt in msgs[idx + 1:]:
        if nxt["src"] == cur["dst"]:
            return str(nxt["no"])
    return "无"


def arrow_kind(arrow, self_msg):
    base = ARROW_KIND.get(arrow, "同步调用")
    return f"自消息（{base}）" if self_msg and base != "自消息" else base


def _name(alias, labels):
    """生命线显示名：去掉 PlantUML 换行标记，取首行作为短名。"""
    return labels[alias].replace("\\n", " ").replace("\n", " ").strip()


def emit(path: pathlib.Path) -> str:
    order, labels, kinds, stereos, msgs, sections = parse(path)
    out = [f"#### {path.stem}", ""]

    out.append("**对象与参与者描述**")
    out.append("")
    out.append("| 对象类型 | 对象名称 | 是否为主动对象 | 其他相关信息 |")
    out.append("| --- | --- | --- | --- |")
    for a in order:
        label = _name(a, labels)
        is_actor = kinds[a] == "actor"
        otype = "参与者" if is_actor else "对象（类实例）"
        stereo = stereos.get(a, "")
        if is_actor:
            active = "—（参与者不适用）"
        else:
            active = "**是**" if any(s in stereo for s in ACTIVE_STEREO) else "否"
        extra = f"生命线关键字 `{kinds[a]}`" + (f"；构造型 «{stereo}»" if stereo else "")
        out.append(f"| {otype} | {label} | {active} | {extra} |")
    out.append("")

    out.append("**对象接收发送消息的描述**（按对象分组，组内按时间顺序）")
    out.append("")
    out.append("| 编号 | 消息名称 | 是发送还是接收 | 消息类型 | 直接触发的消息 | 是否为自接收消息 | 发送对象 | 接收对象 |")
    out.append("| --- | --- | --- | --- | --- | --- | --- | --- |")
    for a in order:
        rows = []
        for i, m in enumerate(msgs):
            if m["src"] == a or m["dst"] == a:
                rows.append((m["no"], m["text"], "发送" if m["src"] == a else "接收",
                             arrow_kind(m["arrow"], m["src"] == m["dst"]),
                             triggered(msgs, i), "是" if m["src"] == m["dst"] else "否",
                             _name(m["src"], labels), _name(m["dst"], labels)))
        if not rows:
            continue
        out.append(f"| **{_name(a, labels)}** |  |  |  |  |  |  |  |")
        for no, text, direction, kind, trg, selfm, sname, dname in sorted(rows, key=lambda r: r[0]):
            safe = text.replace("|", "\\|") or "（无消息名）"
            out.append(f"| {no} | {safe} | {direction} | {kind} | {trg} | {selfm} | {sname} | {dname} |")
    out.append("")
    return "\n".join(out)


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out_file = None
    for a in sys.argv[1:]:
        if a.startswith("--out="):
            out_file = pathlib.Path(a.split("=", 1)[1])
    only = args[0] if args else None
    files = sorted(ROOT.rglob("*.puml"))
    picked = []
    for p in files:
        text = p.read_text(encoding="utf-8")
        if "->" not in text:
            continue
        if "@startuml" in text and ("participant" in text or "actor" in text):
            if only and only not in p.name:
                continue
            picked.append(p)
    if not picked:
        print("no sequence diagrams found", file=sys.stderr)
        return 1
    chunks = []
    for p in picked:
        chunks.append(emit(p))
        print("generated:", p.name, file=sys.stderr)
    body = "\n---\n\n".join(chunks)
    if out_file:
        out_file.write_text(body, encoding="utf-8")
        print("written:", out_file, len(body), "bytes", file=sys.stderr)
    else:
        sys.stdout.buffer.write(body.encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
