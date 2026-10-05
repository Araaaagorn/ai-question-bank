#!/usr/bin/env bash
# 存储回归使用独立数据库，覆盖旧表升级与重复启动。
set -euo pipefail
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iinclude -Ithird_party \
    tests/questions/db_test.c src/db.c third_party/cJSON.c \
    -lsqlite3 -lcrypto -o "$test_dir/db_test"
"$test_dir/db_test" "$test_dir/fresh.db" "$test_dir/legacy.db"
