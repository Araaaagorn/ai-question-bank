#!/usr/bin/env bash
# 题目接口回归：review（学生提交待审 + 教师审核）；由 run_tests.sh 自动发现并执行。
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tests/questions/check_api.py review
