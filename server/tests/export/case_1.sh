#!/usr/bin/env bash
# 试卷导出接口回归：四种格式、内容开关、策略、权限、错误码
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tests/export/check_export.py
