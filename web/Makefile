# =============================================================================
# web —— 前端静态页（纯 HTML/CSS/JS，无构建工具链）
#
# 模块目标说明：
#   lint   node --check 全部页面脚本（语法级静态检查）
#   test   运行 Node stub 测试（tests/test-*.cjs，无外部依赖，需 Node >= 18）
#   build/install/clean  静态资源无产物，仅提示
# =============================================================================

NODE := node

.PHONY: lint test build install clean

lint:
	@for f in js/*.js; do $(NODE) --check "$$f" || exit 1; done
	@echo "✔ web lint 通过（node --check js/*.js）"

test:
	@for f in tests/test-*.cjs; do [ -e "$$f" ] || continue; $(NODE) "$$f" || exit 1; done

build:
	@echo "web 为纯静态资源，由 C 服务端直接托管，无需构建"

install:
	@echo "web 无外部依赖需要安装"

clean:
	@echo "web 无构建产物需要清理"
