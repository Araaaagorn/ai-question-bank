#!/usr/bin/env bash
# 试卷生命周期回归：归档往返、archived 可删除、重复删除 404、删除题目联动
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tests/exams/check_exams.py lifecycle
