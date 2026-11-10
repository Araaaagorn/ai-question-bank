#!/usr/bin/env bash
# 学生浏览路径回归：列表过滤、archived 404、字段完整性、空卷、写 403、teacher 读他人
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tests/exams/check_exams.py student
