#!/usr/bin/env bash
# 试卷异常数据回归：悬空引用详情 500、列表隔离、写路径拒绝、所有者可删除修复
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tests/exams/check_exams.py dangling
