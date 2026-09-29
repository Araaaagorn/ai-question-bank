# 用户认证模块文档

> 对应提交：`327360ed` feat(server): 初步实现用户功能

---

## 1. 概述

本模块实现基于 Token 的用户认证系统，包括：

- 登录（密码校验 + 创建 Session）
- 登出（销毁 Session）
- 当前用户信息查询（Token 校验）
- 预设 Demo 账号（三角色：admin / teacher / student）

认证方式为 **Bearer Token**（`Authorization: Bearer <token>`），Session 存储在服务端内存中（单线程 demo 模式）。

---

## 2. API 接口

### 2.1 登录

```
POST /api/v1/auth/login
Content-Type: application/json

{"username": "admin", "password": "admin123"}
```

**成功响应（200）：**

```json
{
  "token": "a1b2c3d4e5f6...64位十六进制",
  "user": {
    "id": 1,
    "username": "admin",
    "name": "系统管理员",
    "role": "admin"
  }
}
```

**失败响应：**

| 状态码 | 说明 |
| --- | --- |
| 400 | 请求体为空 / JSON 解析失败 / 缺少 username 或 password |
| 401 | 用户名或密码错误（不区分是用户不存在还是密码错误，防枚举） |
| 500 | 查询用户信息失败 / 创建会话失败（OOM 等） |

### 2.2 登出

```
POST /api/v1/auth/logout
Authorization: Bearer <token>
```

**成功响应（200）：** `{"status":"ok"}`

说明：无论 token 是否有效，均返回 200。仅销毁服务端内存中对应的 Session。

### 2.3 当前用户信息

```
GET /api/v1/auth/me
Authorization: Bearer <token>
```

**成功响应（200）：**

```json
{
  "user": {
    "id": 1,
    "name": "系统管理员",
    "role": "admin"
  }
}
```

**失败响应：**

| 状态码 | 说明 |
| --- | --- |
| 401 | 未登录或 token 已过期 |

---

## 3. Session 管理

### 3.1 存储方式

- 内存固定数组（`Session g_sessions[MAX_SESSIONS]`），最多 **128** 个并发 Session。
- 仅适用于单线程 demo 模式。

### 3.2 Token 生成

- 64 位随机十六进制字符串（`rand() % 16`，demo 够用）。
- 有碰撞重试机制（最多 5 次）。

### 3.3 Token 有效期

- 创建时设置 `expires_at = now + 86400`（24 小时）。
- 过期后自动标记失效，需重新登录。

### 3.4 Token 提取

- 从 `Authorization` 请求头中提取 `Bearer <token>`。
- 不区分大小写匹配 `Bearer ` 前缀。

---

## 4. Demo 账号

系统启动时自动插入三个演示账号（由 `seed_demo_accounts()` 初始化）：

| 用户名 | 角色 | 默认密码 | 姓名 |
| --- | --- | --- | --- |
| `admin` | admin | admin123 | 系统管理员 |
| `teacher` | teacher | teacher123 | 张老师 |
| `student` | student | student123 | 李同学 |

密码可在 `server/.env` 中通过环境变量覆盖：

```
DEMO_ADMIN_PW=admin123
DEMO_TEACHER_PW=teacher123
DEMO_STUDENT_PW=student123
```

### 4.1 密码哈希

- 算法：`SHA-256(salt || password)`
- 固定 salt：`ai-question-bank-demo-salt-2026`（demo 阶段使用固定 salt，对抗彩虹表但不要求保密）
- 依赖：OpenSSL `libcrypto`（通过 `-lcrypto` 链接，`libcurl-dev` 间接依赖 `libssl-dev`）

---

## 5. 数据库

### 5.1 users 表

```sql
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  role TEXT NOT NULL DEFAULT 'student',   -- admin / teacher / student
  name TEXT NOT NULL,
  username TEXT NOT NULL UNIQUE,
  password_hash TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'active',  -- active / disabled
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
```

### 5.2 新增查询函数

| 函数 | 返回值 | 说明 |
| --- | --- | --- |
| `db_query_user_by_username(username)` | JSON 字符串或 NULL | 查询用户信息（id / username / name / role） |
| `db_query_password_hash(username)` | 哈希字符串或 NULL | 查询密码哈希，用于登录校验 |

两种查询均限制 `status = 'active'`。

---

## 6. 测试

### 6.1 测试用例

| 文件 | 用例说明 |
| --- | --- |
| `server/tests/auth/case_1.sh` | 三个预设账号登录成功（admin/teacher/student） |
| `server/tests/auth/case_2.sh` | 登录失败场景（错误密码 / 不存在用户 / 缺少字段） |
| `server/tests/auth/case_3.sh` | 当前用户信息（有效 token / 无 token / 无效 token） |
| `server/tests/auth/case_4.sh` | 登出（登出返回 200 / 登出后 token 失效） |

### 6.2 运行测试

```bash
# 全部测试
make -C server test

# 仅认证测试
bash server/tests/run_tests.sh --dir=server/tests/auth
```

测试框架（`server/tests/run_tests.sh`）自动管理服务生命周期（启动 → 运行测试 → 关闭）。

---

## 7. 代码结构

```
server/
├── include/
│   ├── auth.h          # Session 结构体定义、函数声明
│   ├── db.h            # db_query_user_by_username / db_query_password_hash
│   └── routes.h        # route_dispatch 新增 body 参数
├── src/
│   ├── auth.c          # Session 内存管理（create/validate/destroy/extract）
│   ├── db.c            # 哈希工具、seed_demo_accounts、用户/密码查询
│   ├── http_server.c   # POST 请求体累积（RequestCtx）、route_dispatch 调用
│   └── routes.c        # 路由处理（handle_login/handle_logout/handle_me）
└── tests/
    └── auth/
        ├── case_1.sh   # 登录成功
        ├── case_2.sh   # 登录失败
        ├── case_3.sh   # 当前用户
        └── case_4.sh   # 登出
```

### 7.1 路由注册方式

在 `route_dispatch()` 中按顺序匹配：

```c
/* GET /api/v1/health */
if (GET && /api/v1/health?) → handle_health()

/* POST /api/v1/auth/login */
if (POST && /api/v1/auth/login) → handle_login(body)

/* POST /api/v1/auth/logout */
if (POST && /api/v1/auth/logout) → handle_logout()

/* GET /api/v1/auth/me */
if (GET && /api/v1/auth/me) → handle_me()

/* 其他 /api/ 路径 */
→ 404

/* 非 /api/ 路径 */
→ serve_static()
```

---

## 8. 安全说明

- **密码不存明文**：数据库只存 SHA-256 哈希值（加固定 salt）。
- **登录防枚举**：用户名不存在与密码错误均返回 401 + `"用户名或密码错误"`。
- **API Key 不暴露**：认证仅涉及用户名/密码与 Session token，不涉及 AI API Key。
- **Session 有效期**：24 小时过期，过期后自动失效。
- **登出失效**：登出后原 token 立即失效，不可复用。
- **当前为 demo 实现**：Session 存储在内存固定数组中，不支持多进程/多实例/持久化，生产环境需替换为 Redis 或数据库存储。