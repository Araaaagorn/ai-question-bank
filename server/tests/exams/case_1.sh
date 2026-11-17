#!/usr/bin/env bash
# 试卷接口回归：列表/详情/角色过滤/401/403/404/405/归档可见性
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tests/exams/check_exams.py list
