# 环境部署指南（课程服务器）

以 **Ubuntu/Debian** 为例；CentOS（dnf/yum）等价命令见 `scripts/install_env.sh`。

## 1. 系统级依赖

```bash
# 检查当前环境缺什么（只读）
bash scripts/check_env.sh

# 安装系统依赖（需 sudo；会装 git / make / python3+venv+pip / node+npm）
sudo bash scripts/install_env.sh
```

要求的最低版本：**Python ≥ 3.10，Node ≥ 18，make，git**。
若 `check_env.sh` 提示 node 版本过低，用 NodeSource 安装 Node 20：

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs
```

## 2. 克隆与初始化

```bash
git clone <远程仓库地址> ai-question-bank
cd ai-question-bank

# 安装全部模块依赖（backend venv + frontend npm）
make setup
```

## 3. 配置后端

```bash
cd backend
cp .env.example .env
# 编辑 .env：DATABASE_URL / SECRET_KEY / AI_API_KEY 等
```

`.env` 已被 `.gitignore` 排除，不会入库。生产环境务必：
- 生成随机 `SECRET_KEY`（如 `python -c "import secrets; print(secrets.token_hex(32))"`）；
- 数据库建议切换 PostgreSQL（SQLAlchemy URL 直接替换即可），SQLite 仅供开发。

## 4. 初始化数据库与运行

```bash
cd backend
.venv/bin/python scripts/init_db.py     # 建表

# 验证后端
.venv/bin/python -m pytest tests -v
.venv/bin/python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
curl http://<服务器IP>:8000/api/v1/health   # 应返回 {"status":"ok"}
```

## 5. 生产部署（systemd）

示例服务文件 `deploy/systemd/ai-question-bank.service`，按实际路径调整后：

```bash
sudo cp deploy/systemd/ai-question-bank.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now ai-question-bank
```

前端生产构建：`cd frontend && npm run build`，产物在 `frontend/dist/`，
由 Nginx 托管并反代 `/api` 到后端 8000 端口。

## 6. 常见问题

| 现象 | 处理 |
| --- | --- |
| `make` 不存在 | `sudo apt install make` |
| venv 创建失败（ensurepip） | `sudo apt install python3-venv` |
| npm 安装慢 | 配置镜像：`npm config set registry https://registry.npmmirror.com` |
| 服务器无法访问 8000 端口 | 检查防火墙/安全组放行 |
| CI 检查名与分支保护 contexts 不一致 | `gh pr checks <PR号>` 查看后用实际名更新 `scripts/branch_protection.json` |
