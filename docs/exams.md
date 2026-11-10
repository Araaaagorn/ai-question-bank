# 试卷模块后端契约（F03 · 试卷形式组织）

> 版本：v1.0。对应功能单元 F03（基础试卷管理，见 `docs/项目开发时间轴.md`）。
> 本阶段只实现「题目的试卷形式组织」：试卷 CRUD、题目编排（增/删/排序/分值）、学生浏览。
> 导出（F09）不在此契约内，见「本阶段不做」。
> 接口范式、字段命名与错误码语义对齐 `docs/questions.md`；题目对象字段以当前实现为准。

## 1. 范围与依据

- `docs/软件需求说明书.md` F-07 / UC-03：试卷 CRUD、试卷浏览。
- `docs/AI题库项目整体预期功能说明书.md` §3.2 / §3.3 / §4.2 / §5.1：试卷仅负责组织题目顺序；题目可独立存在、可复用于多份试卷。
- 注意：SRS F-07 中的题目字段（`title` / `difficulty` / `tags`）与实现不符，一律以 `docs/questions.md` 的字段为准。

「试卷」是**对题目 id 的引用式组织**，不复制题目内容。试卷详情按需内联题目展示字段（见 §3），但题目本体仍存储在 `qdata`，删除试卷不影响题库。

## 2. 数据存储

存储走 `kv_store`（见 `docs/kvstore.md`）：

| 层级 | 值 |
| --- | --- |
| namespace | `edata` |
| key | 试卷 id 的十进制字符串（如 `"3"`） |
| value_name | `name`、`description`、`teacher_id`、`status`、`created_at`、`updated_at`、`questions` |

- `questions` 的 value 是 JSON 数组字符串，按 `position` 升序：`[{"question_id":1,"position":1,"score":5}]`。
- `created_at` / `updated_at` 存储为 **Unix 秒** 的十进制字符串（与 `teacher_ai_credentials.updated_at` 的 `unixepoch()` 口径一致），接口响应中以 JSON 数字返回。
- 试卷 id 服务端分配：扫描 `edata` 现有 key，取最大数字 id + 1（沿用 `routes.c` 中 `next_question_id` 的分配方式）。

## 3. 试卷对象与视图

### 3.1 列表项（`GET /api/v1/exams`）

```json
{
  "id": 1,
  "name": "期中数学测验",
  "description": "覆盖一元一次方程与导数",
  "teacher_id": 2,
  "status": "active",
  "created_at": 1730000000,
  "updated_at": 1730000000,
  "question_count": 2
}
```

### 3.2 详情对象（其余接口的 `exam` 字段）

```json
{
  "id": 1,
  "name": "期中数学测验",
  "description": "覆盖一元一次方程与导数",
  "teacher_id": 2,
  "status": "active",
  "created_at": 1730000000,
  "updated_at": 1730000000,
  "questions": [
    {
      "question_id": 1,
      "position": 1,
      "score": 5,
      "content": "解方程：2x + 3 = 11，x 的值是多少？",
      "type": "single_choice",
      "options": [{"label": "A", "text": "2"}],
      "knowledge_points": ["一元一次方程"],
      "answer": "C",
      "analysis": "两边减去 3 得到 2x = 8，再除以 2，得到 x = 4。"
    }
  ]
}
```

- `questions` 内联题目展示字段：`question_id`、`position`、`score`、`content`、`type`、`options`、`knowledge_points`、`answer`、`analysis`。
- 内联题目的 `content` / `type` / `options` / `knowledge_points` / `answer` / `analysis` 的取值规则与 `docs/questions.md` 的 `question` 对象完全一致（`type ∈ {single_choice, short_answer}`；单选题 `answer` 为选项 label，简答题 `answer` 为文本；简答题 `options` 为 `[]`；`knowledge_points` 恒为字符串数组）。
- `question_id` 即被引用题目的 `id`；`position` 为 1..n 连续序号；`score` 为分值。
- 内联题目中**不返回**题目的 `status`、`teacher_id`、`seed_key` 等内部字段（仅上述 9 个字段）。

## 4. 接口契约

所有接口均需登录并携带 `Authorization: Bearer <token>`。未登录、无效或已登出 token 返回 401（优先于任何参数校验）。

### 4.1 `GET /api/v1/exams` — 列表

- 可见范围：
  - `admin`：全部试卷。
  - `teacher`：仅自己创建（`teacher_id == 本人 user_id`）的试卷。
  - `student`：仅 `status == active` 的试卷（不按教师过滤）。
- 成功：`200` + `{"exams":[列表项, ...]}`，按 `id` 升序。
- 空列表返回 `{"exams":[]}`。
- 本阶段无分页、无检索，查询参数不过滤列表。

### 4.2 `GET /api/v1/exams/{id}` — 详情

- 可见范围与 4.1 一致；不可见（含 `archived` 对 `student`）一律 `404 EXAM_NOT_FOUND`，不泄露存在性。
- 成功：`200` + `{"exam":{详情对象}}`。
- `id` 必须是正的十进制整数；不是正整数、溢出或含额外路径段 → `404 EXAM_NOT_FOUND`。

### 4.3 `POST /api/v1/exams` — 创建

- 权限：`teacher` / `admin`；`student` → `403 EXAM_FORBIDDEN`（先于任何字段校验）。
- 请求体（仅允许 `name`、`description`）：

```json
{"name": "期中数学测验", "description": "覆盖一元一次方程与导数"}
```

- `name` 必填非空；`description` 可选，缺省为空字符串。
- 请求体出现 `id`、`teacher_id`、`status`、`questions` 等字段 → `400 EXAM_INVALID_REQUEST`。
- 成功：`201` + `{"exam":{详情对象}}`，新试卷 `status=active`、`questions=[]`、`teacher_id=当前会话 user_id`。

### 4.4 `PUT /api/v1/exams/{id}` — 修改元数据

- 权限：`teacher`（仅本人） / `admin`（全部）；写他人试卷 → `403 EXAM_FORBIDDEN`。
- 请求体（仅允许 `name`、`description`），部分更新：出现的字段覆盖旧值，未出现的保留。
- 空对象（不含任何可更新字段）→ `400 EXAM_INVALID_REQUEST`。
- `status=archived` → `409 EXAM_ARCHIVED`。
- 成功：`200` + `{"exam":{详情对象}}`。

### 4.5 `PATCH /api/v1/exams/{id}` — 状态切换

- 权限：`teacher`（仅本人） / `admin`（全部）。
- 请求体（仅允许 `status`）：

```json
{"status": "archived"}
```

- `status` 必须为 `"active"` 或 `"archived"`，否则 `400 EXAM_INVALID_REQUEST`。
- 成功：`200` + `{"exam":{详情对象}}`。
- `archived → active` 后恢复可编辑；`active → archived` 后按 §7 限制编辑。

### 4.6 `PUT /api/v1/exams/{id}/questions` — 整体替换题目集合

- 权限：`teacher`（仅本人） / `admin`（全部）；`student` → `403`；写他人 → `403`。
- 请求体（仅允许 `questions`）：

```json
{"questions": [{"question_id": 1, "position": 1, "score": 5}, {"question_id": 7, "position": 2, "score": 3}]}
```

- 语义：以请求数组顺序为权威，整体替换试卷题目集合。
  - 显式 `position` 必须构成 1..n 的排列，否则 `400 EXAM_INVALID_LAYOUT`。
  - 服务端按数组顺序归一化 `position` 为 1..n，并回填到响应。
- `question_id` 必须存在且 `status=active`，否则 `400 EXAM_INVALID_QUESTION`；同一试卷内 `question_id` 不得重复，否则 `400 EXAM_INVALID_LAYOUT`。
- `status=archived` → `409 EXAM_ARCHIVED`。
- 成功：`200` + `{"exam":{详情对象}}`。

### 4.7 `POST /api/v1/exams/{id}/questions` — 追加单题

- 权限：`teacher`（仅本人） / `admin`（全部）。
- 请求体（仅允许 `question_id`、`score`）：

```json
{"question_id": 3, "score": 8}
```

- 语义：将题目追加到试卷末尾，`position` 取 `n+1`；不接受、不校验 `position` 字段。
- `question_id` 存在且 `status=active`，否则 `400 EXAM_INVALID_QUESTION`；已在该试卷内则 `400 EXAM_INVALID_LAYOUT`（重复）。
- 追加后题目数不得超过 200，否则 `400 EXAM_INVALID_REQUEST`。
- `status=archived` → `409 EXAM_ARCHIVED`。
- 成功：`201` + `{"exam":{详情对象}}`。

### 4.8 `DELETE /api/v1/exams/{id}/questions/{qid}` — 移除单题

- 权限：`teacher`（仅本人） / `admin`（全部）。
- 语义：从试卷移除 `question_id == qid` 的条目，并将后续题目 `position` 自动补全为 1..n。
- `qid` 不是试卷内的 `question_id` → `404 EXAM_NOT_FOUND`。
- `status=archived` → `409 EXAM_ARCHIVED`。
- 成功：`200` + `{"exam":{详情对象}}`。

### 4.9 `DELETE /api/v1/exams/{id}` — 删除试卷

- 权限：`teacher`（仅本人） / `admin`（全部）。
- 不受 `archived` 限制。
- 成功：`200` + `{"status":"ok"}`；此后详情 404、列表不再出现。
- 重复删除 → `404 EXAM_NOT_FOUND`。
- 删除试卷**不影响** `qdata` 中的题目。

## 5. 字段校验（任一失败不得部分写入）

| 字段 | 规则 |
| --- | --- |
| `name` | 非空，1–100 个 Unicode 字符 |
| `description` | 可为空，≤1000 个 Unicode 字符 |
| 单卷题目数 | ≤200 |
| `question_id` | 必须存在于 `qdata` 且 `status=active`；不校验 `teacher_id`（本阶段不做题库隔离，与现有 `GET /api/v1/questions` 语义一致） |
| `position` | 构成 1..n 的排列；服务端响应始终回填归一化后的 1..n |
| `score` | 数值，`0 < score ≤ 1000`，允许小数；负数、0、NaN、Infinity → 400 |
| 只读字段 | 请求体出现 `id`、`teacher_id`、`status`（PATCH 除外）等 → 400 |

- 整体替换与追加、移除操作必须**先全量校验，再一次性写入**，失败不留下半套数据。

## 6. 权限矩阵

| 操作 | admin | teacher | student |
| --- | --- | --- | --- |
| 读列表 / 详情 | 全部 | 仅本人创建 | 仅 active |
| 创建 / 改元数据 / 编排 / 删除 | 全部 | 仅本人创建 | 403 |
| 状态切换（PATCH） | 全部 | 仅本人创建 | 403 |

- `teacher_id` 一律从登录会话（`session->user_id`）取，不接受客户端传入。
- `teacher` 写他人试卷 → `403 EXAM_FORBIDDEN`；`student` 任何写操作 → `403 EXAM_FORBIDDEN`（先于资源存在性校验，与 `docs/questions.md` 一致）。

## 7. 状态与生命周期

- `archived` 试卷禁止一切编辑：`PUT /exams/{id}`、`PUT /exams/{id}/questions`、`POST /exams/{id}/questions`、`DELETE /exams/{id}/questions/{qid}` 一律 `409 EXAM_ARCHIVED`。
- `PATCH` 改回 `active` 后恢复可编辑。
- `DELETE /exams/{id}` 不受 `archived` 限制。
- `archived` 对 `student`：列表不出现、详情 `404`（不泄露存在性）。
- 题目被删（`DELETE /api/v1/questions/{id}`）时，必须从所有试卷中移除该引用并重排 `position`，保证写入侧不产生悬空引用（见 §11）。

## 8. 错误码总表

| 状态码 | code | 场景 |
| --- | --- | --- |
| 200 | — | 读取 / 更新 / 状态切换 / 整体替换 / 移除 / 删除成功 |
| 201 | — | 创建试卷、追加单题成功 |
| 400 | `EXAM_INVALID_REQUEST` | 请求体缺失 / JSON 非法 / 字段校验失败 / 只读字段 / 题目数超限 |
| 400 | `EXAM_INVALID_LAYOUT` | `position` 非排列 / 重复 `question_id` |
| 400 | `EXAM_INVALID_QUESTION` | 引用不存在或非 active 的题目 |
| 401 | `UNAUTHORIZED` | 未登录或 token 已过期（优先于一切） |
| 403 | `EXAM_FORBIDDEN` | `student` 写操作 / `teacher` 写他人试卷 |
| 404 | `EXAM_NOT_FOUND` | 试卷不存在 / 非法 id / 越权不可见 / 移除不存在的引用 |
| 405 | `METHOD_NOT_ALLOWED` | 方法不允许，响应含正确的 `Allow` 头 |
| 409 | `EXAM_ARCHIVED` | `archived` 试卷上的编辑操作 |
| 500 | `EXAM_STORE_FAILED` | 存储失败 |

错误响应统一格式：

```json
{"error": "可读说明", "code": "EXAM_INVALID_REQUEST"}
```

所有 JSON 响应带 `Cache-Control: no-store`。

`Allow` 头约定：

| 路径 | Allow |
| --- | --- |
| `/api/v1/exams` | `GET, POST` |
| `/api/v1/exams/{id}` | `GET, PUT, PATCH, DELETE` |
| `/api/v1/exams/{id}/questions` | `PUT, POST` |
| `/api/v1/exams/{id}/questions/{qid}` | `DELETE` |

## 9. 示例

### 9.1 创建含单选 + 简答混排的试卷

```http
POST /api/v1/exams
Authorization: Bearer <teacher token>
Content-Type: application/json

{"name": "期中数学测验", "description": "覆盖一元一次方程与导数"}
```

```http
HTTP/1.1 201 Created
Content-Type: application/json; charset=utf-8

{
  "exam": {
    "id": 6,
    "name": "期中数学测验",
    "description": "覆盖一元一次方程与导数",
    "teacher_id": 2,
    "status": "active",
    "created_at": 1730000000,
    "updated_at": 1730000000,
    "questions": []
  }
}
```

随后追加两道题（单选 #1 与简答 #2）：

```http
POST /api/v1/exams/6/questions
{"question_id": 1, "score": 5}

POST /api/v1/exams/6/questions
{"question_id": 2, "score": 10}
```

### 9.2 整体替换并重排

请求数组顺序决定最终顺序；显式 `position` 只被校验为 1..2 的排列：

```http
PUT /api/v1/exams/6/questions
Content-Type: application/json

{"questions": [{"question_id": 2, "position": 2, "score": 10}, {"question_id": 1, "position": 1, "score": 5}]}
```

服务端以数组顺序为权威归一化：`question_id=2` 变为 `position=1`，`question_id=1` 变为 `position=2`：

```http
HTTP/1.1 200 OK

{
  "exam": {
    "id": 6,
    "name": "期中数学测验",
    "description": "覆盖一元一次方程与导数",
    "teacher_id": 2,
    "status": "active",
    "created_at": 1730000000,
    "updated_at": 1730000000,
    "questions": [
      {"question_id": 2, "position": 1, "score": 10, "content": "求函数 f(x) = x^2 的导数。", "type": "short_answer", "options": [], "knowledge_points": ["导数"], "answer": "f'(x) = 2x", "analysis": "由幂函数求导公式……"},
      {"question_id": 1, "position": 2, "score": 5, "content": "解方程：2x + 3 = 11，x 的值是多少？", "type": "single_choice", "options": [{"label": "A", "text": "2"}, {"label": "B", "text": "3"}, {"label": "C", "text": "4"}, {"label": "D", "text": "5"}], "knowledge_points": ["一元一次方程", "等式性质"], "answer": "C", "analysis": "两边减去 3 得到 2x = 8……"}
    ]
  }
}
```

## 10. 本阶段不做

- 分页、检索、排序参数。
- 试卷总分校验（分值只做单题范围校验，不做总分约束）。
- 题目导出、试卷导出（F09）。
- 异步任务 / 导出异步化。
- 跨教师题库隔离（选题来源为全库 active 题目）。
- 删除恢复 / 回收站。
- 师生 / 班级 / 课程关系（沿用 `docs/teacher-api.md` 的边界）。

## 11. 集成点与实现约定

- 存储层独立为 `server/src/exams.c` + `server/include/exams.h`，复用 `AppConfig.db` 单一连接，不在路由层写 SQL。
- 路由层新增 `handle_exams`，在 `route_dispatch` 中于 `/api/` 兜底 404 之前按 `/api/v1/exams` 前缀分发。
- `position` 归一化、题目引用校验、`archived` 校验均在 `exams.c` 内完成，路由只做参数抽取与状态码映射。
- 题目删除联动：`handle_question_delete` 成功后调用 `exams_purge_question(db, qid)`，从所有试卷移除该引用并重排 `position`。清理失败不回滚已成功的题目删除、不返回 500：只记录固定分类日志 `[exams] purge_question_failed (question_id=N)`（不含用户内容），仍返回 200。因为题目已删除，试卷侧悬空引用是可修复的最终一致数据。
- 本阶段不在文档中出现实现中不存在的字段；新接口「先落模块文档再写代码」（见 `docs/AGENT.md` §7.3）。
