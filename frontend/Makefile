# 前端（Vue3 + Vite）构建规则
# 目标（与根 Makefile 分派约定一致）：install lint test build run clean

NPM ?= npm

.PHONY: install lint test build run clean

install: ## 安装依赖
	$(NPM) install

lint: ## 静态检查（eslint）
	$(NPM) run lint

test: ## 单元测试（vitest）
	$(NPM) run test

build: ## 生产构建（产物输出到 dist/）
	$(NPM) run build

run: ## 开发服务器（http://localhost:5173，/api 代理到后端 8000）
	$(NPM) run dev

clean: ## 清理产物与依赖
	rm -rf node_modules dist
