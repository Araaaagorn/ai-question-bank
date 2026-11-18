#!/usr/bin/env python3
"""自检：校验 Markdown 中引用的图片路径均存在，且每个 .puml 均有对应 .svg/.png。"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
missing = []

md_files = sorted(ROOT.glob("*.md"))
ref_count = 0
for md in md_files:
    text = md.read_text(encoding="utf-8")
    for ref in re.findall(r"\]\((diagrams/[^)\s]+?\.(?:svg|png))\)", text):
        ref_count += 1
        target = ROOT / ref
        if not target.exists():
            missing.append(f"{md.name}: {ref}")

puml_files = sorted((ROOT / "diagrams").rglob("*.puml"))
for p in puml_files:
    for ext in (".svg", ".png"):
        if not p.with_suffix(ext).exists():
            missing.append(f"render missing: {p.relative_to(ROOT)}{ext}")

print(f"markdown files: {len(md_files)}; image refs: {ref_count}; puml files: {len(puml_files)}")
if missing:
    print("MISSING:")
    for m in missing:
        print(" -", m)
    sys.exit(1)
print("ALL REFERENCES OK")
