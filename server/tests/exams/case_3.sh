#!/usr/bin/env bash
# 试卷题目编排回归：整体替换/追加/移除、校验矩阵、archived 409、403、405
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tests/exams/check_exams.py questions
