#!/usr/bin/env bash
# 试卷接口回归：创建校验矩阵、201、PUT 部分更新、PATCH 状态切换、archived 409
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tests/exams/check_exams.py crud
