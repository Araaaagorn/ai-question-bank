# 试卷导出后端契约（F09）

> 版本：v1.0。对应功能单元 F09（试卷导出，见 `docs/项目开发时间轴.md`）。
> 本阶段只实现**单份试卷**导出：PDF / Markdown / LaTeX / DOCX。
> 批量导出、异步导出、导出文件持久化、旧版 `.doc` 不在本契约内。
> 接口范式、鉴权和错误响应对齐 `docs/exams.md` 与 `docs/questions.md`。

## 1. 范围与依据

- `docs/软件需求说明书.md` UC-08 / F-08：试卷导出，格式 PDF、Word、Markdown、LaTeX。
- `docs/AI题库项目整体预期功能说明书.md` §8：可选择题干、选项、答案、解析、AI 官方解答；教师可配置是否允许导出、是否允许带答案导出。
- `docs/项目开发时间轴.md` F09：Sprint 3 先做 PDF/Markdown，Sprint 6 完善 Word/LaTeX；I04 再做导出异步化。

本阶段实现：

- 单份试卷导出为 `pdf`、`markdown`、`latex`、`docx`。
- 题干始终输出；选项、答案、解析可独立开关。
- AI 解答只保留请求参数和响应能力位，当前没有数据源，不输出内容。
- 教师/管理员可配置单卷的导出策略；学生导出 `active` 试卷时受策略约束。

本阶段不做：

- 批量导出、打包下载。
- 异步导出、任务队列、导出结果持久化。
- `.doc` 旧格式；Word 只输出 `.docx`。
- AI 解答生成或读取；`ai=1` 明确返回不支持。

## 2. 接口总览

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| `GET` | `/api/v1/exams/{id}/export/{format}` | 导出单份试卷文件 |
| `GET` | `/api/v1/exams/{id}/export-policy` | 读取导出策略与能力 |
| `PUT` | `/api/v1/exams/{id}/export-policy` | 部分更新导出策略 |

三个接口都要求 `Authorization: Bearer <token>`；未登录返回 401 `UNAUTHORIZED`。

`format` 必须是小写全名：

| format | 扩展名 | Content-Type |
| --- | --- | --- |
| `pdf` | `pdf` | `application/pdf` |
| `markdown` | `md` | `text/markdown; charset=utf-8` |
| `latex` | `tex` | `application/x-tex; charset=utf-8` |
| `docx` | `docx` | `application/vnd.openxmlformats-officedocument.wordprocessingml.document` |

格式名大小写敏感；`PDF`、`Markdown` 等大写形式返回 400 `EXPORT_UNSUPPORTED_FORMAT`。

## 3. 导出接口

### 3.1 请求

```http
GET /api/v1/exams/12/export/pdf?options=1&answers=1&analysis=0&ai=0
Authorization: Bearer <token>
```

查询参数均为可选，只接受 `0` / `1` / `true` / `false`（大小写不敏感）：

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| `options` | `1` | 是否输出选项 |
| `answers` | `1` | 是否输出答案 |
| `analysis` | `1` | 是否输出解析 |
| `ai` | `0` | 是否输出 AI 解答；当前恒不支持 `1` |

非法值（如 `yes`、空串、`2`）返回 400 `EXPORT_INVALID_REQUEST`。
重复查询参数按服务端当前实现取其中一个值，不承诺顺序语义。

### 3.2 内容规则

- 试卷标题始终输出。
- 题干始终输出；没有单独的“题干开关”。
- `options=1` 时输出选项；简答题选项为空数组时不额外输出。
- `answers=1` 时输出答案；`analysis=1` 时输出解析。两者互相独立。
- `knowledge_points` 作为题干辅助信息始终输出，不受 `options` / `answers` / `analysis` 控制。
- `ai=1` 且 AI 数据源未就绪时返回 400 `EXPORT_OPTION_UNSUPPORTED`。
- 空答案或空解析不输出对应区块。
- 渲染只消费 `exams_get()` 返回的试卷详情，不重复读取题目存储。

### 3.3 权限与策略

可见性沿用 `docs/exams.md`：

- `admin`：可导出所有试卷。
- `teacher`：只能导出自己创建的试卷；自己的归档卷也可导出。
- `student`：只能导出 `status=active` 的试卷；归档卷返回 404。
- 其他角色按 `docs/exams.md` 的可见性规则处理。

导出策略：

- `allow_export`：非归属方（典型为 student）是否允许导出；默认 `true`。
- `allow_answers`：非归属方是否允许带答案/解析；默认 `true`。
- `admin` 和试卷创建者属于归属方，始终不受这两个开关限制。
- `allow_export=false` 时，非归属方导出返回 403 `EXPORT_FORBIDDEN`。
- `allow_answers=false` 时，非归属方请求中的 `answers` / `analysis` 被静默忽略（不报错，响应中不出现答案/解析）。

策略只影响导出，不改变试卷详情接口。

### 3.4 响应

成功返回文件内容，状态码 200：

```http
Content-Type: application/pdf
Content-Disposition: attachment; filename="exam-12.pdf"; filename*=UTF-8''exam-12-%E6%9C%9F%E4%B8%AD%E5%8D%B7.pdf
Cache-Control: no-store
X-Content-Type-Options: nosniff
```

- ASCII `filename` 固定为 `exam-{id}.{ext}`，作为旧客户端兜底。
- `filename*` 使用 RFC 5987 UTF-8 百分号编码；其中包含清洗后的试卷名。
- 试卷名中的控制字符、`"`、`\`、`/`、`;`、`%` 替换为 `_`，并按 UTF-8 边界截断到 60 字节。
- 单份文件上限 8 MiB；超过返回 413 `EXPORT_TOO_LARGE`。

错误响应沿用统一格式：

```json
{
  "error": "该试卷不允许导出",
  "code": "EXPORT_FORBIDDEN"
}
```

### 3.5 错误码

| 状态码 | code | 场景 |
| --- | --- | --- |
| 400 | `EXPORT_UNSUPPORTED_FORMAT` | 格式不在四种受支持格式内 |
| 400 | `EXPORT_INVALID_REQUEST` | 查询参数非法、请求体非法 |
| 400 | `EXPORT_OPTION_UNSUPPORTED` | `ai=1` 但 AI 数据源未就绪 |
| 401 | `UNAUTHORIZED` | 未登录或 token 失效 |
| 403 | `EXPORT_FORBIDDEN` | 非归属方被策略禁止导出，或非归属方修改策略 |
| 404 | `EXPORT_NOT_FOUND` | 试卷不存在、不可见、路径不合法 |
| 405 | `METHOD_NOT_ALLOWED` | 方法不支持，响应带 `Allow` |
| 413 | `EXPORT_TOO_LARGE` | 渲染结果超过 8 MiB |
| 500 | `EXPORT_RENDER_FAILED` | 渲染失败 |
| 500 | `EXPORT_STORE_FAILED` | 策略读写或响应构造失败 |

## 4. 导出策略接口

### 4.1 `GET /api/v1/exams/{id}/export-policy`

有试卷可见性的用户可以读取。成功返回 200：

```json
{
  "export_policy": {
    "allow_export": true,
    "allow_answers": true
  },
  "capabilities": {
    "formats": ["pdf", "markdown", "latex", "docx"],
    "ai_answer": false
  }
}
```

- `ai_answer` 当前恒为 `false`，表示 AI 解答数据源未接入。
- 未设置策略时返回默认值 `allow_export=true`、`allow_answers=true`。

### 4.2 `PUT /api/v1/exams/{id}/export-policy`

仅试卷创建者或管理员可写；其他角色返回 403 `EXPORT_FORBIDDEN`。请求体必须是 JSON 对象，且只能包含：

```json
{
  "allow_export": false,
  "allow_answers": true
}
```

- 两个字段都可选，但至少提供一个。
- 字段必须是布尔值；否则返回 400 `EXPORT_INVALID_REQUEST`。
- 未提供的字段保持当前值。
- 成功返回与 GET 相同的完整策略响应。
- 不修改试卷 `updated_at`。

## 5. 数据存储

策略复用 `kv_store` 的 `edata` 命名空间：

| 层级 | 值 |
| --- | --- |
| namespace | `edata` |
| key | 试卷 id 的十进制字符串 |
| value_name | `export_policy` |

`value` 为 JSON 对象字符串：

```json
{"allow_export":true,"allow_answers":true}
```

缺失或损坏时按默认值处理；读取存储本身失败（SQLite 准备/执行错误、OOM）返回 500 `EXPORT_STORE_FAILED`，不得 fail-open。损坏包括非对象 JSON、非布尔字段以及 JSON 之后的尾随垃圾。不新增表，不复制题目内容。

## 6. 实现约定

- 渲染模块独立为 `server/include/export.h` + `server/src/export.c`，不接触 HTTP。
- 路由层只做鉴权、可见性、策略、参数解析、状态码映射和响应头。
- 渲染层不打印用户内容到日志；错误日志只允许固定分类和试卷/题目 id。
- 四种格式均不引入第三方渲染库：PDF 手工拼装 PDF 1.4，DOCX 手工拼装无压缩 OOXML ZIP。
- Markdown / LaTeX / DOCX 的文本内容使用 UTF-8；PDF 使用 PDF 1.4 结构，文本以 UTF-16BE 十六进制串写入，并使用 `UniGB-UCS2-H` / `STSong-Light` 支持中文。

## 7. 验收要点

- 四种格式均可下载且文件结构可被对应解析器打开；PDF 校验 xref 偏移、对象头和 stream `/Length`，DOCX 校验 ZIP 内全部 OOXML/XML 部件可解析。
- Markdown / LaTeX 内容包含题干、选项、答案、解析，且开关生效。
- PDF 包含 `%PDF-1.4`、`%%EOF`、`/UniGB-UCS2-H`、`/STSong-Light`，且 xref 指向的对象偏移与 stream 长度自洽。
- DOCX 可被 Python `zipfile` 打开，包含 `[Content_Types].xml`、`_rels/.rels`、`word/document.xml`、`word/_rels/document.xml.rels`，且所有 XML/rels 部件均可被 XML 解析器解析。
- 权限矩阵、策略默认值、部分更新、归档卷行为、401/403/404/405/413 均通过 HTTP 回归。
- 现有 F03 试卷测试不回归。
