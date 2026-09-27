# 后端（Python / FastAPI）构建规则
# 目标（与根 Makefile 分派约定一致）：install lint test build run clean

SHELL := /bin/bash

# venv 解释器路径（Windows 与 Linux 兼容）
ifeq ($(OS),Windows_NT)
PY := .venv/Scripts/python.exe
PIP := .venv/Scripts/pip.exe
else
PY := .venv/bin/python
PIP := .venv/bin/pip
endif

.PHONY: install lint test build run clean

install: ## 创建虚拟环境并安装依赖
	python -m venv .venv
	$(PIP) install -U pip
	$(PIP) install -r requirements-dev.txt

lint: ## 静态检查（ruff）
	$(PY) -m ruff check app tests
	$(PY) -m ruff format --check app tests

test: ## 运行测试（pytest）
	$(PY) -m pytest tests -v

build: ## Python 无编译产物，占位以保持模块目标一致
	@echo "backend: 解释型语言，无构建产物"

run: ## 开发服务器
	$(PY) -m uvicorn app.main:app --reload --host 0.0.0.0 --port 8000

clean: ## 清理缓存与虚拟环境
	rm -rf .venv .pytest_cache .ruff_cache
	find . -name __pycache__ -type d -exec rm -rf {} + 2>/dev/null || true
