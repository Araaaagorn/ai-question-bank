# 环境部署指南（课程服务器，C 技术栈）

以 **Ubuntu/Debian** 为例；CentOS（dnf/yum）等价命令见 `scripts/install_env.sh`。

## 1. 系统级依赖

```bash
# 检查当前环境缺什么（只读）
bash scripts/check_env.sh

# 安装系统依赖（需 sudo）：gcc / make / libmicrohttpd-dev / libcurl4-openssl-dev / libsqlite3-dev
sudo bash scripts/install_env.sh
```

要求：**gcc（支持 C11）、make、git、pkg-config**，以及三个开发库：
`libmicrohttpd`（HTTP 服务）、`libcurl`（AI API 调用）、`sqlite3`（存储）。

## 2. 克隆与构建

```bash
git clone <远程仓库地址> ai-question-bank
cd ai-question-bank

make build          # 编译 server/ai-question-bank-server
make test           # 冒烟测试（自动启动服务并验证健康检查 + 静态页）
```

## 3. 配置服务端

服务端通过环境变量读取配置，默认值见 `server/src/config.c`：

```bash
# 方式一：export（开发调试）
export PORT=8000
export AI_API_KEY=sk-xxxx
make -C server run

# 方式二：.env 文件（生产，配合 systemd EnvironmentFile）
cp server/.env.example server/.env
# 编辑 server/.env：PORT / DATABASE_PATH / AI_API_BASE_URL / AI_API_KEY / AI_MODEL
```

生产环境务必：

- 填写真实 `AI_API_KEY`（仅后端持有，不暴露给前端）；
- 数据库文件（`server/data/`）已被 `.gitignore` 排除，注意定期备份。

## 4. 运行与验证

```bash
make -C server run    # 前台运行（Ctrl+C 停止）
curl http://<服务器IP>:8000/api/v1/health   # 应返回 {"status":"ok"}
curl http://<服务器IP>:8000/                # 应返回前端首页 HTML
```

## 5. 生产部署（systemd）

示例服务文件 `deploy/systemd/ai-question-bank.service`，按实际路径调整后：

```bash
sudo cp deploy/systemd/ai-question-bank.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now ai-question-bank
systemctl status ai-question-bank          # 查看运行状态
journalctl -u ai-question-bank -f          # 查看日志
```

## 6. 常见问题

| 现象 | 处理 |
| --- | --- |
| `gcc`/`make` 不存在 | `sudo apt install build-essential` |
| `microhttpd.h` 找不到 | `sudo apt install libmicrohttpd-dev` |
| `curl/curl.h` 找不到 | `sudo apt install libcurl4-openssl-dev` |
| `sqlite3.h` 找不到 | `sudo apt install libsqlite3-dev` |
| 编译报错含 `undeclared` | 检查 `server/Makefile` 的 `SRCS` 是否登记了新源文件 |
| 服务器无法访问 8000 端口 | 检查防火墙/安全组放行 |
| 前端页面 404 | 确认 `WEB_ROOT` 指向 `web/`（默认 `../web`，需在 `server/` 目录下运行） |
| CI 检查名与分支保护 contexts 不一致 | `gh pr checks <PR号>` 查看后用实际名更新 `scripts/branch_protection.json` |
