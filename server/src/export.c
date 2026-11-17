#define _POSIX_C_SOURCE 200809L

/*
 * 试卷导出（F09）渲染层。
 *
 * 设计约束：
 *   - 不引入第三方库：PDF 手工拼装 1.4 对象表，DOCX 手工拼装无压缩 ZIP；
 *   - 只读试卷对象（exams_get 的结果），不重复查询 qdata；
 *   - 渲染层不接触 HTTP、不向日志打印用户内容；
 *   - AI 解答本期无数据源，接线点集中在 export_ai_answer()。
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "exams.h"
#include "export.h"
#include "kv_store.h"

/* 试卷数据所在的 kv_store namespace（与 exams.c 一致）与策略 value_name。 */
#define EXPORT_NAMESPACE "edata"
#define EXPORT_POLICY_VALUE "export_policy"

/* A4 版面（点）：左右边距 56，正文可用宽度 483。 */
#define PDF_LEFT_X 56
#define PDF_TOP_Y 786
#define PDF_BOTTOM_Y 56
#define PDF_TEXT_WIDTH 483.0

#define PDF_BODY_SIZE 11
#define PDF_TITLE_SIZE 18

/* ── 可增长输出缓冲（二进制安全） ── */

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
    int oom;      /* 分配失败 */
    int overflow; /* 超过 EXPORT_MAX_BYTES */
} Buf;

static void buf_init(Buf *b) { memset(b, 0, sizeof *b); }

static void buf_reset(Buf *b) {
    if (b == NULL) return;
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->oom = 0;
    b->overflow = 0;
}

static int buf_reserve(Buf *b, size_t extra) {
    if (b->oom || b->overflow) return -1;
    if (extra > EXPORT_MAX_BYTES || b->len > EXPORT_MAX_BYTES - extra) {
        b->overflow = 1;
        return -1;
    }
    size_t need = b->len + extra + 1; /* +1 保证 NUL 结尾，便于按字符串拼接 */
    if (need <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 512;
    while (cap < need) {
        if (cap > EXPORT_MAX_BYTES) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    if (cap < need) cap = need;
    unsigned char *grown = realloc(b->data, cap);
    if (grown == NULL) {
        b->oom = 1;
        return -1;
    }
    b->data = grown;
    b->cap = cap;
    return 0;
}

static int buf_append(Buf *b, const void *src, size_t n) {
    if (b->oom || b->overflow) return -1;
    if (n == 0) return 0;
    if (buf_reserve(b, n) != 0) return -1;
    memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int buf_puts(Buf *b, const char *s) { return buf_append(b, s, strlen(s)); }

static int buf_putc(Buf *b, char c) { return buf_append(b, &c, 1); }

static int buf_printf(Buf *b, const char *fmt, ...) {
    if (b->oom || b->overflow) return -1;
    va_list ap;
    va_list copy;
    va_start(ap, fmt);
    va_copy(copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        va_end(copy);
        b->oom = 1;
        return -1;
    }
    if (buf_reserve(b, (size_t)needed) != 0) {
        va_end(copy);
        return -1;
    }
    (void)vsnprintf((char *)b->data + b->len, (size_t)needed + 1, fmt, copy);
    va_end(copy);
    b->len += (size_t)needed;
    return 0;
}

static int buf_put_u16(Buf *b, unsigned value) {
    unsigned char bytes[2] = { (unsigned char)(value & 0xFFu),
                               (unsigned char)((value >> 8) & 0xFFu) };
    return buf_append(b, bytes, sizeof bytes);
}

static int buf_put_u32(Buf *b, unsigned long value) {
    unsigned char bytes[4] = { (unsigned char)(value & 0xFFul),
                               (unsigned char)((value >> 8) & 0xFFul),
                               (unsigned char)((value >> 16) & 0xFFul),
                               (unsigned char)((value >> 24) & 0xFFul) };
    return buf_append(b, bytes, sizeof bytes);
}

/* ── UTF-8 工具 ── */

/** 解码一个码点；非法序列按 U+FFFD、长度 1 处理，绝不越过 NUL。 */
static size_t utf8_decode(const char *s, uint32_t *out) {
    const unsigned char *p = (const unsigned char *)s;
    size_t len;
    uint32_t cp;
    if (p[0] < 0x80) {
        *out = p[0];
        return 1;
    }
    if ((p[0] & 0xE0) == 0xC0) {
        len = 2;
        cp = p[0] & 0x1Fu;
    } else if ((p[0] & 0xF0) == 0xE0) {
        len = 3;
        cp = p[0] & 0x0Fu;
    } else if ((p[0] & 0xF8) == 0xF0) {
        len = 4;
        cp = p[0] & 0x07u;
    } else {
        *out = 0xFFFD;
        return 1;
    }
    for (size_t i = 1; i < len; i++) {
        if (p[i] == '\0' || (p[i] & 0xC0) != 0x80) {
            *out = 0xFFFD;
            return 1;
        }
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    *out = cp;
    return len;
}

/** 显示宽度（单位：半角）。CJK / 全角标点 / 谚文 / 假名记 2，其余记 1。 */
static int cp_width(uint32_t cp) {
    if (cp == 0 || cp < 0x20) return 0;
    if (cp < 0x7F) return 1;
    if (cp >= 0x1100 &&
        (cp <= 0x115F || (cp >= 0x2E80 && cp <= 0xA4CF) ||
         (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
         (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x20000 && cp <= 0x3FFFD))) {
        return 2;
    }
    return 1;
}

static char *str_copy_range(const char *s, size_t n) {
    char *copy = malloc(n + 1);
    if (copy == NULL) return NULL;
    if (n > 0) memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

/** 按 PDF Type0 + UniGB-UCS2-H 编码输出十六进制串 <...>。 */
static int buf_put_utf16be_hex(Buf *b, const char *text) {
    if (buf_putc(b, '<') != 0) return -1;
    for (const char *p = text; *p != '\0';) {
        uint32_t cp = 0;
        size_t n = utf8_decode(p, &cp);
        if (cp < 0x20 && cp != '\t') {
            p += n;
            continue;
        }
        if (cp == '\t') cp = ' ';
        if (cp <= 0xFFFFu) {
            if (buf_printf(b, "%04X", (unsigned)cp) != 0) return -1;
        } else {
            uint32_t v = cp - 0x10000u;
            unsigned high = 0xD800u + (unsigned)(v >> 10);
            unsigned low = 0xDC00u + (unsigned)(v & 0x3FFu);
            if (buf_printf(b, "%04X%04X", high, low) != 0) return -1;
        }
        p += n;
    }
    return buf_putc(b, '>');
}

/* ── 字段读取与数值格式化 ── */

static const char *json_str(const cJSON *obj, const char *key) {
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsString(item) && item->valuestring != NULL) ? item->valuestring : "";
}

static double json_number(const cJSON *obj, const char *key) {
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : 0.0;
}

/** 分值格式化：整数不带小数点，小数去掉尾随零。 */
static void format_score(double score, char *buffer, size_t size) {
    if (buffer == NULL || size == 0) return;
    (void)snprintf(buffer, size, "%.12f", score);
    char *dot = strchr(buffer, '.');
    if (dot != NULL) {
        char *end = buffer + strlen(buffer) - 1;
        while (end > dot && *end == '0') {
            *end = '\0';
            end--;
        }
        if (end == dot) *end = '\0';
    }
    /* 极小但合法的正分值不能显示成 0；回退到有效数字表示。 */
    if (score > 0.0 && strcmp(buffer, "0") == 0)
        (void)snprintf(buffer, size, "%.12g", score);
}

/* ── 文本转义 ── */

/** Markdown 转义：换行压成空格（避免破坏标题/列表结构），行首列表标记加反斜杠。 */
static int md_puts(Buf *b, const char *text) {
    int at_start = 1;
    for (const char *p = text; *p != '\0';) {
        uint32_t cp = 0;
        size_t n = utf8_decode(p, &cp);
        if (cp == '\r') {
            p += n;
            continue;
        }
        if (cp == '\n' || cp == '\t') {
            if (buf_putc(b, ' ') != 0) return -1;
            p += n;
            at_start = 0;
            continue;
        }
        if (cp < 0x20) {
            p += n;
            continue;
        }
        int needs_escape = 0;
        switch (cp) {
            case '\\': case '`': case '*': case '_': case '[': case ']':
            case '<': case '>': case '|':
                needs_escape = 1;
                break;
            case '#': case '-': case '+': case '=':
                needs_escape = at_start;
                break;
            default:
                break;
        }
        if (needs_escape && buf_putc(b, '\\') != 0) return -1;
        if (buf_append(b, p, n) != 0) return -1;
        if (cp != ' ') at_start = 0;
        p += n;
    }
    return 0;
}

/** LaTeX 转义：换行压成空格；保留语义需要显式 \par 的地方由调用方补。 */
static int tex_puts(Buf *b, const char *text) {
    for (const char *p = text; *p != '\0';) {
        uint32_t cp = 0;
        size_t n = utf8_decode(p, &cp);
        if (cp == '\r') {
            p += n;
            continue;
        }
        if (cp == '\n' || cp == '\t') {
            if (buf_putc(b, ' ') != 0) return -1;
            p += n;
            continue;
        }
        if (cp < 0x20) {
            p += n;
            continue;
        }
        switch (cp) {
            case '\\':
                if (buf_puts(b, "\\textbackslash{}") != 0) return -1;
                break;
            case '&': case '%': case '$': case '#': case '_': case '{': case '}':
                if (buf_putc(b, '\\') != 0) return -1;
                if (buf_append(b, p, n) != 0) return -1;
                break;
            case '~':
                if (buf_puts(b, "\\textasciitilde{}") != 0) return -1;
                break;
            case '^':
                if (buf_puts(b, "\\textasciicircum{}") != 0) return -1;
                break;
            default:
                if (buf_append(b, p, n) != 0) return -1;
                break;
        }
        p += n;
    }
    return 0;
}

/** XML 文本转义（WordprocessingML 用）；控制字符丢弃，制表符转空格。 */
static int xml_puts_escaped_range(Buf *b, const char *text, size_t len) {
    const char *end = text + len;
    for (const char *p = text; p < end;) {
        uint32_t cp = 0;
        size_t n = utf8_decode(p, &cp);
        if (n > (size_t)(end - p)) n = (size_t)(end - p);
        if (cp < 0x20 && cp != '\t') {
            p += n;
            continue;
        }
        switch (cp) {
            case '&':
                if (buf_puts(b, "&amp;") != 0) return -1;
                break;
            case '<':
                if (buf_puts(b, "&lt;") != 0) return -1;
                break;
            case '>':
                if (buf_puts(b, "&gt;") != 0) return -1;
                break;
            case '\t':
                if (buf_putc(b, ' ') != 0) return -1;
                break;
            default:
                if (buf_append(b, p, n) != 0) return -1;
                break;
        }
        p += n;
    }
    return 0;
}

/* ── 格式表 ── */

int export_format_supported(const char *format) {
    if (format == NULL) return 0;
    return strcmp(format, "pdf") == 0 || strcmp(format, "markdown") == 0 ||
           strcmp(format, "latex") == 0 || strcmp(format, "docx") == 0;
}

const char *export_format_extension(const char *format) {
    if (format == NULL) return NULL;
    if (strcmp(format, "pdf") == 0) return "pdf";
    if (strcmp(format, "markdown") == 0) return "md";
    if (strcmp(format, "latex") == 0) return "tex";
    if (strcmp(format, "docx") == 0) return "docx";
    return NULL;
}

const char *export_format_content_type(const char *format) {
    if (format == NULL) return NULL;
    if (strcmp(format, "pdf") == 0) return "application/pdf";
    if (strcmp(format, "markdown") == 0) return "text/markdown; charset=utf-8";
    if (strcmp(format, "latex") == 0) return "application/x-tex; charset=utf-8";
    if (strcmp(format, "docx") == 0)
        return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    return NULL;
}

const char *export_error_code(ExportError error) {
    switch (error) {
        case EXPORT_NOT_FOUND: return "EXPORT_NOT_FOUND";
        case EXPORT_INVALID_REQUEST: return "EXPORT_INVALID_REQUEST";
        case EXPORT_UNSUPPORTED_FORMAT: return "EXPORT_UNSUPPORTED_FORMAT";
        case EXPORT_OPTION_UNSUPPORTED: return "EXPORT_OPTION_UNSUPPORTED";
        case EXPORT_TOO_LARGE: return "EXPORT_TOO_LARGE";
        case EXPORT_STORE_FAILED: return "EXPORT_STORE_FAILED";
        case EXPORT_RENDER_FAILED:
        default: return "EXPORT_RENDER_FAILED";
    }
}

void export_buffer_free(ExportBuffer *buffer) {
    if (buffer == NULL) return;
    free(buffer->data);
    buffer->data = NULL;
    buffer->len = 0;
}

/* ── AI 解答预留位 ──
 *
 * 本期无 AI 解答数据源。接线时只需：
 *   1) 在 qdata 的题目 key 下新增 value_name "ai_answer"（JSON 文本或纯文本），
 *      这样 DELETE /api/v1/questions/{id} 的 kv_delete_key 会连带清理，不留孤儿；
 *   2) 实现下面的 export_ai_answer()；
 *   3) 把 export_ai_available() 改为 return 1。
 * 四种渲染器已经写好 ai_answer 的输出分支，届时无需改动渲染代码。
 */

static char *export_ai_answer(sqlite3 *db, int question_id) {
    (void)db;
    (void)question_id;
    return NULL;
}

int export_ai_available(void) { return 0; }

/** 把 AI 解答注入试卷对象的题目副本（仅当数据源可用时调用）。 */
static void export_attach_ai_answers(sqlite3 *db, cJSON *exam) {
    cJSON *questions = cJSON_GetObjectItem(exam, "questions");
    if (!cJSON_IsArray(questions)) return;
    int count = cJSON_GetArraySize(questions);
    for (int i = 0; i < count; i++) {
        cJSON *question = cJSON_GetArrayItem(questions, i);
        cJSON *qid = cJSON_GetObjectItem(question, "question_id");
        if (!cJSON_IsNumber(qid)) continue;
        char *answer = export_ai_answer(db, qid->valueint);
        if (answer == NULL) continue;
        if (cJSON_GetObjectItem(question, "ai_answer") == NULL) {
            (void)cJSON_AddStringToObject(question, "ai_answer", answer);
        }
        free(answer);
    }
}

/* ── 导出策略 ──
 * 与试卷同 key 存放，value_name = "export_policy"。不新增表、不改 F03 的
 * 7 个 value_name（exams_get 按名读取，不受影响）；老试卷缺该值即走默认策略。 */

static int policy_key(int exam_id, char *buffer, size_t size) {
    if (exam_id <= 0) return -1;
    int written = snprintf(buffer, size, "%d", exam_id);
    return (written > 0 && (size_t)written < size) ? 0 : -1;
}

/* 在既有 kv_get 接口上区分“未找到”和“存储错误”：
 * kv_get 找不到值时返回 NULL，随后用 value_name 列表确认该值是否真的不存在。 */
static int export_read_value(sqlite3 *db, const char *key, const char *value_name,
                             char **out) {
    *out = NULL;
    char *value = kv_get(db, EXPORT_NAMESPACE, key, value_name);
    if (value != NULL) {
        *out = value;
        return 0;
    }

    int count = 0;
    char **names = kv_list_value_names(db, EXPORT_NAMESPACE, key, &count);
    if (names == NULL) return -1;
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], value_name) == 0) {
            found = 1;
            break;
        }
    }
    kv_free_str_array(names);
    return found ? -1 : 1;
}

int export_policy_get(sqlite3 *db, int exam_id, int *allow_export, int *allow_answers) {
    if (allow_export != NULL) *allow_export = 1;
    if (allow_answers != NULL) *allow_answers = 1;
    if (db == NULL || exam_id <= 0) return EXPORT_INVALID_REQUEST;

    char key[32];
    if (policy_key(exam_id, key, sizeof key) != 0) return EXPORT_INVALID_REQUEST;

    char *raw = NULL;
    int read_status = export_read_value(db, key, EXPORT_POLICY_VALUE, &raw);
    if (read_status < 0) return EXPORT_STORE_FAILED;
    if (read_status == 1) return EXPORT_OK; /* 未设置 → 默认允许导出、允许带答案 */

    cJSON *policy = cJSON_ParseWithOpts(raw, NULL, 1);
    free(raw);
    if (!cJSON_IsObject(policy)) {
        cJSON_Delete(policy);
        return EXPORT_OK; /* 值损坏时退回默认，避免把试卷永久锁死 */
    }
    const cJSON *export_flag = cJSON_GetObjectItem(policy, "allow_export");
    const cJSON *answer_flag = cJSON_GetObjectItem(policy, "allow_answers");
    if (allow_export != NULL && cJSON_IsBool(export_flag)) {
        *allow_export = cJSON_IsTrue(export_flag) ? 1 : 0;
    }
    if (allow_answers != NULL && cJSON_IsBool(answer_flag)) {
        *allow_answers = cJSON_IsTrue(answer_flag) ? 1 : 0;
    }
    cJSON_Delete(policy);
    return EXPORT_OK;
}

int export_policy_set(sqlite3 *db, int exam_id, int allow_export, int allow_answers) {
    if (db == NULL || exam_id <= 0) return EXPORT_INVALID_REQUEST;
    char key[32];
    if (policy_key(exam_id, key, sizeof key) != 0) return EXPORT_INVALID_REQUEST;

    char *name = NULL;
    int read_status = export_read_value(db, key, "name", &name);
    if (read_status < 0) return EXPORT_STORE_FAILED;
    if (read_status == 1) return EXPORT_NOT_FOUND;
    free(name);

    cJSON *policy = cJSON_CreateObject();
    if (policy == NULL) return EXPORT_STORE_FAILED;
    int ok = cJSON_AddBoolToObject(policy, "allow_export", allow_export ? 1 : 0) != NULL &&
             cJSON_AddBoolToObject(policy, "allow_answers", allow_answers ? 1 : 0) != NULL;
    char *json = ok ? cJSON_PrintUnformatted(policy) : NULL;
    cJSON_Delete(policy);
    if (json == NULL) return EXPORT_STORE_FAILED;

    int rc = kv_set(db, EXPORT_NAMESPACE, key, EXPORT_POLICY_VALUE, json);
    cJSON_free(json);
    return rc == 0 ? EXPORT_OK : EXPORT_STORE_FAILED;
}

/* ── 行块模型 ──
 * 四种格式共用“行 + 行类型”：题目内容的抽取逻辑只写一遍，
 * 渲染器只负责把行类型映射成各自的语法，避免四种格式内容不一致。 */

typedef enum {
    LINE_TITLE,   /* 试卷标题 */
    LINE_META,    /* 试卷信息（题数/总分/描述） */
    LINE_HEADING, /* 题号 + 分值 */
    LINE_BODY,    /* 题干 */
    LINE_OPTION,  /* 选项 */
    LINE_NOTE,    /* 知识点等小字 */
    LINE_ANSWER,  /* 答案 / 解析 / AI 解答 */
    LINE_BLANK    /* 视觉间隔 */
} LineKind;

typedef struct {
    LineKind kind;
    char *text; /* 由 Doc 持有 */
} DocLine;

typedef struct {
    DocLine *items;
    int count;
    int cap;
    int failed;
} Doc;

static void doc_init(Doc *doc) { memset(doc, 0, sizeof *doc); }

static void doc_free(Doc *doc) {
    if (doc == NULL) return;
    for (int i = 0; i < doc->count; i++) free(doc->items[i].text);
    free(doc->items);
    doc->items = NULL;
    doc->count = 0;
    doc->cap = 0;
}

/** 追加一行；text 所有权移交 Doc。 */
static int doc_add(Doc *doc, LineKind kind, char *text) {
    if (doc->failed) {
        free(text);
        return -1;
    }
    if (text == NULL) {
        doc->failed = 1;
        return -1;
    }
    if (doc->count == doc->cap) {
        int cap = doc->cap ? doc->cap * 2 : 32;
        DocLine *grown = realloc(doc->items, (size_t)cap * sizeof *grown);
        if (grown == NULL) {
            free(text);
            doc->failed = 1;
            return -1;
        }
        doc->items = grown;
        doc->cap = cap;
    }
    doc->items[doc->count].kind = kind;
    doc->items[doc->count].text = text;
    doc->count++;
    return 0;
}

/** 取走 Buf 里的字符串（Buf 必须已 NUL 结尾）；失败返回 NULL 并释放。 */
static char *buf_take(Buf *b) {
    if (b->oom || b->overflow || b->data == NULL) {
        buf_reset(b);
        return NULL;
    }
    char *text = (char *)b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return text;
}

static char *text_dup(const char *s) {
    if (s == NULL) s = "";
    return str_copy_range(s, strlen(s));
}

/** 拼接：任意段 NULL 视为空串。 */
static char *text_join(const char *first, ...) {
    Buf b = { 0 };
    va_list ap;
    va_start(ap, first);
    for (const char *part = first; part != NULL; part = va_arg(ap, const char *)) {
        if (buf_puts(&b, part) != 0) break;
    }
    va_end(ap);
    return buf_take(&b);
}

/* ── 试卷对象 → Doc ── */

static void doc_add_options(Doc *doc, const cJSON *question) {
    const cJSON *options = cJSON_GetObjectItem(question, "options");
    if (!cJSON_IsArray(options)) return;
    int count = cJSON_GetArraySize(options);
    for (int i = 0; i < count; i++) {
        const cJSON *option = cJSON_GetArrayItem(options, i);
        const char *label = json_str(option, "label");
        const char *text = json_str(option, "text");
        char *line = text_join(label, label[0] && text[0] ? ". " : "", text, NULL);
        if (doc_add(doc, LINE_OPTION, line) != 0) return;
    }
}

static void doc_add_answer_block(Doc *doc, const cJSON *question,
                                 const ExportOptions *options) {
    if (options->include_answers) {
        const char *answer = json_str(question, "answer");
        if (answer[0]) {
            if (doc_add(doc, LINE_ANSWER, text_join("答案：", answer, NULL)) != 0) return;
        }
    }
    if (options->include_analysis) {
        const char *analysis = json_str(question, "analysis");
        if (analysis[0]) {
            if (doc_add(doc, LINE_ANSWER, text_join("解析：", analysis, NULL)) != 0) return;
        }
    }
    if (options->include_ai) {
        const cJSON *ai = cJSON_GetObjectItem(question, "ai_answer");
        if (cJSON_IsString(ai) && ai->valuestring != NULL && ai->valuestring[0]) {
            if (doc_add(doc, LINE_ANSWER,
                        text_join("AI 解答：", (const char *)ai->valuestring, NULL)) != 0) {
                return;
            }
        }
    }
}

static void doc_add_knowledge_points(Doc *doc, const cJSON *question) {
    const cJSON *points = cJSON_GetObjectItem(question, "knowledge_points");
    if (!cJSON_IsArray(points)) return;
    Buf b = { 0 };
    int count = cJSON_GetArraySize(points);
    int first = 1;
    for (int i = 0; i < count; i++) {
        const cJSON *point = cJSON_GetArrayItem(points, i);
        if (!cJSON_IsString(point) || point->valuestring == NULL || !point->valuestring[0]) continue;
        if (buf_puts(&b, first ? "知识点：" : "、") != 0) break;
        if (buf_puts(&b, point->valuestring) != 0) break;
        first = 0;
    }
    if (b.len > 0) (void)doc_add(doc, LINE_NOTE, buf_take(&b));
    else buf_reset(&b);
}

/** 组装导出文档；questions 缺失或结构异常时返回 -1。 */
static int doc_build(const cJSON *exam, const ExportOptions *options, Doc *doc) {
    const cJSON *questions = cJSON_GetObjectItem(exam, "questions");
    if (!cJSON_IsArray(questions)) return -1;

    if (doc_add(doc, LINE_TITLE, text_dup(json_str(exam, "name"))) != 0) return -1;

    const cJSON *description = cJSON_GetObjectItem(exam, "description");
    if (cJSON_IsString(description) && description->valuestring != NULL &&
        description->valuestring[0]) {
        if (doc_add(doc, LINE_META, text_dup(description->valuestring)) != 0) return -1;
    }

    int question_count = cJSON_GetArraySize(questions);
    double total = 0.0;
    for (int i = 0; i < question_count; i++) {
        total += json_number(cJSON_GetArrayItem(questions, i), "score");
    }
    char score_text[32];
    format_score(total, score_text, sizeof score_text);
    char count_text[64];
    (void)snprintf(count_text, sizeof count_text, "共 %d 题 · 总分 %s 分", question_count,
                   score_text);
    if (doc_add(doc, LINE_META, text_dup(count_text)) != 0) return -1;
    if (doc_add(doc, LINE_BLANK, text_dup("")) != 0) return -1;

    for (int i = 0; i < question_count; i++) {
        const cJSON *question = cJSON_GetArrayItem(questions, i);
        char score[32];
        format_score(json_number(question, "score"), score, sizeof score);
        char heading[64];
        (void)snprintf(heading, sizeof heading, "第 %d 题（%s 分）", i + 1, score);
        if (doc_add(doc, LINE_HEADING, text_dup(heading)) != 0) return -1;

        const char *content = json_str(question, "content");
        if (doc_add(doc, LINE_BODY, text_dup(content)) != 0) return -1;

        if (options->include_options) doc_add_options(doc, question);
        doc_add_knowledge_points(doc, question);
        doc_add_answer_block(doc, question, options);
        if (doc_add(doc, LINE_BLANK, text_dup("")) != 0) return -1;
    }
    return doc->failed ? -1 : 0;
}

/* ── Markdown 渲染 ── */

static int render_markdown(const Doc *doc, Buf *out) {
    LineKind prev = LINE_BLANK;
    for (int i = 0; i < doc->count; i++) {
        const DocLine *line = &doc->items[i];
        /* 选项组结束后需要空行，否则下一个小标题会被当作列表延续。 */
        if (prev == LINE_OPTION && line->kind != LINE_OPTION) {
            if (buf_putc(out, '\n') != 0) return -1;
        }
        switch (line->kind) {
            case LINE_TITLE:
                if (buf_puts(out, "# ") != 0 || md_puts(out, line->text) != 0) return -1;
                if (buf_puts(out, "\n\n") != 0) return -1;
                break;
            case LINE_META:
                if (buf_puts(out, "> ") != 0 || md_puts(out, line->text) != 0) return -1;
                if (buf_putc(out, '\n') != 0) return -1;
                break;
            case LINE_HEADING:
                if (buf_puts(out, "## ") != 0 || md_puts(out, line->text) != 0) return -1;
                if (buf_puts(out, "\n\n") != 0) return -1;
                break;
            case LINE_BODY:
                if (md_puts(out, line->text) != 0) return -1;
                if (buf_puts(out, "\n\n") != 0) return -1;
                break;
            case LINE_OPTION:
                if (buf_puts(out, "- ") != 0 || md_puts(out, line->text) != 0) return -1;
                if (buf_putc(out, '\n') != 0) return -1;
                break;
            case LINE_NOTE:
                if (buf_putc(out, '*') != 0 || md_puts(out, line->text) != 0) return -1;
                if (buf_puts(out, "*\n\n") != 0) return -1;
                break;
            case LINE_ANSWER:
                if (buf_puts(out, "**") != 0 || md_puts(out, line->text) != 0) return -1;
                if (buf_puts(out, "**\n\n") != 0) return -1;
                break;
            case LINE_BLANK:
            default:
                /* 各块结尾已带一个空行，这里不再叠加，避免出现多个连续空行。 */
                break;
        }
        prev = line->kind;
    }
    return 0;
}

/* ── LaTeX 渲染（ctexart + xelatex） ── */

static int tex_close_itemize(Buf *out, int *open) {
    if (!*open) return 0;
    *open = 0;
    return buf_puts(out, "\\end{itemize}\n\n");
}

static int render_latex(const Doc *doc, Buf *out) {
    if (buf_puts(out,
                 "% 由 AI+题库智能教学平台导出；请用 XeLaTeX 编译（中文支持）。\n"
                 "\\documentclass[11pt,a4paper]{ctexart}\n"
                 "\\usepackage[margin=2.2cm]{geometry}\n"
                 "\\usepackage{amsmath,amssymb}\n"
                 "\\usepackage{enumitem}\n"
                 "\\setlist[itemize]{leftmargin=1.8em}\n"
                 "\\begin{document}\n\n") != 0) {
        return -1;
    }

    int itemize_open = 0;
    for (int i = 0; i < doc->count; i++) {
        const DocLine *line = &doc->items[i];
        if (line->kind != LINE_OPTION && tex_close_itemize(out, &itemize_open) != 0) return -1;
        switch (line->kind) {
            case LINE_TITLE:
                if (buf_puts(out, "\\begin{center}{\\LARGE ") != 0 ||
                    tex_puts(out, line->text) != 0 ||
                    buf_puts(out, "}\\end{center}\n\n") != 0) {
                    return -1;
                }
                break;
            case LINE_META:
                if (buf_puts(out, "{\\small ") != 0 || tex_puts(out, line->text) != 0 ||
                    buf_puts(out, "}\\par\n") != 0) {
                    return -1;
                }
                break;
            case LINE_HEADING:
                if (buf_puts(out, "\\subsection*{") != 0 || tex_puts(out, line->text) != 0 ||
                    buf_puts(out, "}\n") != 0) {
                    return -1;
                }
                break;
            case LINE_BODY:
                if (tex_puts(out, line->text) != 0 || buf_puts(out, "\n\n") != 0) return -1;
                break;
            case LINE_OPTION:
                if (!itemize_open) {
                    if (buf_puts(out, "\\begin{itemize}\n") != 0) return -1;
                    itemize_open = 1;
                }
                if (buf_puts(out, "  \\item ") != 0 || tex_puts(out, line->text) != 0 ||
                    buf_putc(out, '\n') != 0) {
                    return -1;
                }
                break;
            case LINE_NOTE:
                if (buf_puts(out, "{\\small ") != 0 || tex_puts(out, line->text) != 0 ||
                    buf_puts(out, "}\\par\n") != 0) {
                    return -1;
                }
                break;
            case LINE_ANSWER:
                if (buf_puts(out, "\\noindent\\textbf{") != 0 || tex_puts(out, line->text) != 0 ||
                    buf_puts(out, "}\\par\n") != 0) {
                    return -1;
                }
                break;
            case LINE_BLANK:
            default:
                if (buf_puts(out, "\n") != 0) return -1;
                break;
        }
    }
    if (tex_close_itemize(out, &itemize_open) != 0) return -1;
    return buf_puts(out, "\\end{document}\n");
}

/* ── DOCX 渲染 ──
 * 手写 OOXML 包：ZIP 采用 stored（不压缩）+ 自算 CRC32，零第三方依赖。
 * 固定时间戳写 0，保证同样输入产出完全相同的字节（便于回归测试比对）。 */

static uint32_t crc_table[256];
static int crc_table_ready = 0;

static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t value = i;
        for (int bit = 0; bit < 8; bit++) {
            value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
        }
        crc_table[i] = value;
    }
    crc_table_ready = 1;
}

static uint32_t crc32_of(const unsigned char *data, size_t len) {
    if (!crc_table_ready) crc32_init();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

typedef struct {
    const char *name;
    const unsigned char *data;
    size_t len;
    uint32_t crc;
    size_t offset;
} ZipEntry;

static int zip_write(Buf *out, ZipEntry *entries, int count) {
    for (int i = 0; i < count; i++) {
        entries[i].crc = crc32_of(entries[i].data, entries[i].len);
        entries[i].offset = out->len;
        if (buf_put_u32(out, 0x04034b50ul) != 0) return -1;
        if (buf_put_u16(out, 20) != 0) return -1;         /* version needed */
        if (buf_put_u16(out, 0x0800) != 0) return -1;     /* flags: UTF-8 名称 */
        if (buf_put_u16(out, 0) != 0) return -1;          /* method: stored */
        if (buf_put_u16(out, 0) != 0) return -1;          /* mod time */
        if (buf_put_u16(out, 0x21) != 0) return -1;       /* mod date: 1980-01-01 */
        if (buf_put_u32(out, entries[i].crc) != 0) return -1;
        if (buf_put_u32(out, (unsigned long)entries[i].len) != 0) return -1;
        if (buf_put_u32(out, (unsigned long)entries[i].len) != 0) return -1;
        size_t name_len = strlen(entries[i].name);
        if (buf_put_u16(out, (unsigned)name_len) != 0) return -1;
        if (buf_put_u16(out, 0) != 0) return -1;          /* extra length */
        if (buf_append(out, entries[i].name, name_len) != 0) return -1;
        if (buf_append(out, entries[i].data, entries[i].len) != 0) return -1;
    }

    size_t central_offset = out->len;
    for (int i = 0; i < count; i++) {
        if (buf_put_u32(out, 0x02014b50ul) != 0) return -1;
        if (buf_put_u16(out, 20) != 0) return -1;         /* version made by */
        if (buf_put_u16(out, 20) != 0) return -1;         /* version needed */
        if (buf_put_u16(out, 0x0800) != 0) return -1;
        if (buf_put_u16(out, 0) != 0) return -1;
        if (buf_put_u16(out, 0) != 0) return -1;
        if (buf_put_u16(out, 0x21) != 0) return -1;
        if (buf_put_u32(out, entries[i].crc) != 0) return -1;
        if (buf_put_u32(out, (unsigned long)entries[i].len) != 0) return -1;
        if (buf_put_u32(out, (unsigned long)entries[i].len) != 0) return -1;
        size_t name_len = strlen(entries[i].name);
        if (buf_put_u16(out, (unsigned)name_len) != 0) return -1;
        if (buf_put_u16(out, 0) != 0) return -1;          /* extra */
        if (buf_put_u16(out, 0) != 0) return -1;          /* comment */
        if (buf_put_u16(out, 0) != 0) return -1;          /* disk start */
        if (buf_put_u16(out, 0) != 0) return -1;          /* internal attrs */
        if (buf_put_u32(out, 0) != 0) return -1;          /* external attrs */
        if (buf_put_u32(out, (unsigned long)entries[i].offset) != 0) return -1;
        if (buf_append(out, entries[i].name, name_len) != 0) return -1;
    }
    size_t central_size = out->len - central_offset;

    if (buf_put_u32(out, 0x06054b50ul) != 0) return -1;
    if (buf_put_u16(out, 0) != 0) return -1;
    if (buf_put_u16(out, 0) != 0) return -1;
    if (buf_put_u16(out, (unsigned)count) != 0) return -1;
    if (buf_put_u16(out, (unsigned)count) != 0) return -1;
    if (buf_put_u32(out, (unsigned long)central_size) != 0) return -1;
    if (buf_put_u32(out, (unsigned long)central_offset) != 0) return -1;
    return buf_put_u16(out, 0); /* comment length */
}

/* Word 段落：sz 为半磅值，indent 为 twips，bold/center 为格式开关。 */
static int docx_paragraph(Buf *xml, const char *text, int size_half_points, int bold,
                          int center, int indent_twips, const char *color) {
    if (buf_puts(xml, "<w:p><w:pPr>") != 0) return -1;
    if (center) {
        if (buf_puts(xml, "<w:jc w:val=\"center\"/>") != 0) return -1;
    }
    if (indent_twips > 0) {
        if (buf_printf(xml, "<w:ind w:left=\"%d\"/>", indent_twips) != 0) return -1;
    }
    if (buf_puts(xml, "<w:spacing w:after=\"120\" w:line=\"300\" w:lineRule=\"auto\"/>"
                      "</w:pPr><w:r><w:rPr>"
                      "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:eastAsia=\"SimSun\"/>") != 0) {
        return -1;
    }
    if (bold && buf_puts(xml, "<w:b/>") != 0) return -1;
    if (buf_printf(xml, "<w:sz w:val=\"%d\"/><w:szCs w:val=\"%d\"/>", size_half_points,
                   size_half_points) != 0) {
        return -1;
    }
    if (color != NULL && buf_printf(xml, "<w:color w:val=\"%s\"/>", color) != 0) return -1;
    if (buf_puts(xml, "</w:rPr>") != 0) return -1;
    const char *p = text;
    for (;;) {
        const char *break_at = strpbrk(p, "\r\n");
        size_t len = break_at != NULL ? (size_t)(break_at - p) : strlen(p);
        if (buf_puts(xml, "<w:t xml:space=\"preserve\">") != 0) return -1;
        if (xml_puts_escaped_range(xml, p, len) != 0) return -1;
        if (buf_puts(xml, "</w:t>") != 0) return -1;
        if (break_at == NULL) break;
        const char *next = break_at + 1;
        if (*break_at == '\r' && *next == '\n') next++;
        if (buf_puts(xml, "<w:br/>") != 0) return -1;
        p = next;
    }
    return buf_puts(xml, "</w:r></w:p>");
}

static int render_docx(const Doc *doc, Buf *out, int *too_large) {
    if (too_large != NULL) *too_large = 0;
    Buf xml = { 0 };
    if (buf_puts(&xml,
                 "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                 "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
                 "<w:body>") != 0) {
        buf_reset(&xml);
        return -1;
    }

    for (int i = 0; i < doc->count; i++) {
        const DocLine *line = &doc->items[i];
        int rc;
        switch (line->kind) {
            case LINE_TITLE:
                rc = docx_paragraph(&xml, line->text, 32, 1, 1, 0, NULL);
                break;
            case LINE_META:
                rc = docx_paragraph(&xml, line->text, 18, 0, 0, 0, "595959");
                break;
            case LINE_HEADING:
                rc = docx_paragraph(&xml, line->text, 24, 1, 0, 0, NULL);
                break;
            case LINE_BODY:
                rc = docx_paragraph(&xml, line->text, 22, 0, 0, 0, NULL);
                break;
            case LINE_OPTION:
                rc = docx_paragraph(&xml, line->text, 22, 0, 0, 420, NULL);
                break;
            case LINE_NOTE:
                rc = docx_paragraph(&xml, line->text, 18, 0, 0, 0, "595959");
                break;
            case LINE_ANSWER:
                rc = docx_paragraph(&xml, line->text, 22, 1, 0, 0, "1F3864");
                break;
            case LINE_BLANK:
            default:
                rc = buf_puts(&xml, "<w:p/>");
                break;
        }
        if (rc != 0) {
            if (too_large != NULL && xml.overflow) *too_large = 1;
            buf_reset(&xml);
            return -1;
        }
    }

    if (buf_puts(&xml,
                 "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
                 "<w:pgMar w:top=\"1418\" w:right=\"1418\" w:bottom=\"1418\" w:left=\"1418\" "
                 "w:header=\"851\" w:footer=\"992\" w:gutter=\"0\"/>"
                 "</w:sectPr></w:body></w:document>") != 0) {
        if (too_large != NULL && xml.overflow) *too_large = 1;
        buf_reset(&xml);
        return -1;
    }

    static const char content_types[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" "
        "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>";
    static const char root_rels[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"word/document.xml\"/>"
        "</Relationships>";
    static const char document_rels[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"/>";

    ZipEntry entries[4] = {
        { "[Content_Types].xml", (const unsigned char *)content_types,
          sizeof content_types - 1, 0, 0 },
        { "_rels/.rels", (const unsigned char *)root_rels, sizeof root_rels - 1, 0, 0 },
        { "word/document.xml", xml.data, xml.len, 0, 0 },
        { "word/_rels/document.xml.rels", (const unsigned char *)document_rels,
          sizeof document_rels - 1, 0, 0 },
    };
    int rc = zip_write(out, entries, 4);
    buf_reset(&xml);
    return rc;
}

/* ── PDF 渲染 ──
 * 手工拼装 PDF 1.4：Type0 字体 + UniGB-UCS2-H 编码 + 标准 CJK 字体 STSong-Light。
 * 字体不嵌入（Adobe-GB1 是阅读器通用的中文标准字体），因此文件小、无需字体文件，
 * 但要求阅读器自带该字体；缺失时阅读器会回退到其它中文字体。
 * 文本一律以 UTF-16BE 十六进制串输出，天然规避 PDF 字符串转义问题。 */

typedef struct {
    Buf *pages;
    int count;
    int cap;
    Buf current;
    double y;
    int failed;
} PdfLayout;

static void pdf_layout_init(PdfLayout *layout) {
    memset(layout, 0, sizeof *layout);
    layout->y = PDF_TOP_Y;
}

static void pdf_layout_free(PdfLayout *layout) {
    for (int i = 0; i < layout->count; i++) buf_reset(&layout->pages[i]);
    free(layout->pages);
    layout->pages = NULL;
    layout->count = 0;
    layout->cap = 0;
    buf_reset(&layout->current);
}

/** 结束当前页（非空才入册），开一页新的。 */
static int pdf_new_page(PdfLayout *layout) {
    if (layout->current.len > 0) {
        if (layout->count == layout->cap) {
            int cap = layout->cap ? layout->cap * 2 : 8;
            Buf *grown = realloc(layout->pages, (size_t)cap * sizeof *grown);
            if (grown == NULL) return -1;
            layout->pages = grown;
            layout->cap = cap;
        }
        layout->pages[layout->count++] = layout->current;
        buf_init(&layout->current);
    }
    layout->y = PDF_TOP_Y;
    return 0;
}

/** 预留 height 点高度；不足则换页。 */
static int pdf_reserve(PdfLayout *layout, double height) {
    if (layout->y - height < PDF_BOTTOM_Y) return pdf_new_page(layout);
    return 0;
}

/** 输出一行（绝对定位），并下移 y。 */
static int pdf_draw(PdfLayout *layout, const char *text, size_t len, int size) {
    char *line = str_copy_range(text, len);
    if (line == NULL) return -1;
    int rc = buf_printf(&layout->current, "BT /F1 %d Tf 1 0 0 1 %d %.2f Tm ", size, PDF_LEFT_X,
                        layout->y);
    if (rc == 0) rc = buf_put_utf16be_hex(&layout->current, line);
    if (rc == 0) rc = buf_puts(&layout->current, " Tj ET\n");
    free(line);
    if (rc != 0) return -1;
    layout->y -= size * 1.55;
    return 0;
}

/** 按显示宽度折行输出；显式换行符也分行。 */
static int pdf_emit_text(PdfLayout *layout, const char *text, int size) {
    double line_height = size * 1.55;
    const char *p = text;
    int first_line = 1;

    while (first_line || *p != '\0') {
        first_line = 0;
        const char *line_start = p;
        const char *cursor = p;
        const char *soft_break = NULL;
        double width = 0.0;

        while (*cursor != '\0' && *cursor != '\n') {
            uint32_t cp = 0;
            size_t n = utf8_decode(cursor, &cp);
            if (cp < 0x20 && cp != '\t') {
                cursor += n;
                continue;
            }
            uint32_t shown = (cp == '\t') ? (uint32_t)' ' : cp;
            double w = (double)cp_width(shown) * (double)size * 0.5;
            if (width + w > PDF_TEXT_WIDTH && cursor > line_start) break;
            width += w;
            if (shown == ' ' || shown == '-' || shown == '/' || shown == ',' || shown == ';') {
                soft_break = cursor + n;
            }
            cursor += n;
        }

        const char *line_end = cursor;
        int ended_by_break = (*cursor == '\n');
        if (!ended_by_break && *cursor != '\0' && soft_break != NULL && soft_break > line_start) {
            line_end = soft_break; /* 英文按词断开，避免切词 */
        }

        if (pdf_reserve(layout, line_height) != 0) return -1;
        if (pdf_draw(layout, line_start, (size_t)(line_end - line_start), size) != 0) return -1;

        if (line_end > line_start) {
            p = line_end;
        } else if (ended_by_break || *p == '\0') {
            p += 0; /* 空行：下面统一推进 */
        } else {
            p += 1; /* 极端窄行，强制前进一格避免死循环 */
        }
        if (*p == '\n') p++;
    }
    return 0;
}

static int pdf_emit_doc(const Doc *doc, PdfLayout *layout) {
    for (int i = 0; i < doc->count; i++) {
        const DocLine *line = &doc->items[i];
        switch (line->kind) {
            case LINE_TITLE:
                if (pdf_reserve(layout, PDF_TITLE_SIZE * 2.0) != 0) return -1;
                if (pdf_emit_text(layout, line->text, PDF_TITLE_SIZE) != 0) return -1;
                layout->y -= 6;
                break;
            case LINE_META:
                if (pdf_emit_text(layout, line->text, 9) != 0) return -1;
                break;
            case LINE_HEADING:
                layout->y -= 6;
                if (pdf_reserve(layout, 24) != 0) return -1;
                if (pdf_emit_text(layout, line->text, 13) != 0) return -1;
                break;
            case LINE_BODY:
                if (pdf_emit_text(layout, line->text, PDF_BODY_SIZE) != 0) return -1;
                break;
            case LINE_OPTION:
                if (pdf_emit_text(layout, line->text, PDF_BODY_SIZE) != 0) return -1;
                break;
            case LINE_NOTE:
                if (pdf_emit_text(layout, line->text, 9) != 0) return -1;
                break;
            case LINE_ANSWER:
                if (pdf_emit_text(layout, line->text, PDF_BODY_SIZE) != 0) return -1;
                break;
            case LINE_BLANK:
            default: {
                double gap = PDF_BODY_SIZE * 1.2;
                if (pdf_reserve(layout, gap) != 0) return -1;
                layout->y -= gap;
                break;
            }
        }
    }
    return 0;
}

static int pdf_assemble(const PdfLayout *layout, Buf *out) {
    int page_count = layout->count;
    if (page_count <= 0) return -1;

    int total_objects = 5 + page_count * 2;
    size_t *offsets = calloc((size_t)total_objects + 1, sizeof *offsets);
    if (offsets == NULL) return -1;

    if (buf_puts(out, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n") != 0) {
        free(offsets);
        return -1;
    }

    const int catalog = 1, pages = 2, font = 3, cid_font = 4, descriptor = 5;
    const int first_page = 6;

    offsets[catalog] = out->len;
    if (buf_printf(out, "%d 0 obj\n<< /Type /Catalog /Pages %d 0 R >>\nendobj\n", catalog,
                   pages) != 0) {
        free(offsets);
        return -1;
    }

    Buf kids = { 0 };
    for (int i = 0; i < page_count; i++) {
        if (buf_printf(&kids, "%d 0 R ", first_page + i * 2) != 0) {
            buf_reset(&kids);
            free(offsets);
            return -1;
        }
    }
    offsets[pages] = out->len;
    if (buf_printf(out, "%d 0 obj\n<< /Type /Pages /Count %d /Kids [%s] >>\nendobj\n", pages,
                   page_count, kids.data != NULL ? (const char *)kids.data : "") != 0) {
        buf_reset(&kids);
        free(offsets);
        return -1;
    }
    buf_reset(&kids);

    offsets[font] = out->len;
    if (buf_printf(out,
                   "%d 0 obj\n<< /Type /Font /Subtype /Type0 /BaseFont /STSong-Light "
                   "/Encoding /UniGB-UCS2-H /DescendantFonts [%d 0 R] >>\nendobj\n",
                   font, cid_font) != 0) {
        free(offsets);
        return -1;
    }

    offsets[cid_font] = out->len;
    if (buf_printf(out,
                   "%d 0 obj\n<< /Type /Font /Subtype /CIDFontType0 /BaseFont /STSong-Light "
                   "/CIDSystemInfo << /Registry (Adobe) /Ordering (GB1) /Supplement 2 >> "
                   "/FontDescriptor %d 0 R /DW 1000 >>\nendobj\n",
                   cid_font, descriptor) != 0) {
        free(offsets);
        return -1;
    }

    offsets[descriptor] = out->len;
    if (buf_printf(out,
                   "%d 0 obj\n<< /Type /FontDescriptor /FontName /STSong-Light /Flags 6 "
                   "/FontBBox [-25 -254 1000 880] /ItalicAngle 0 /Ascent 880 /Descent -120 "
                   "/CapHeight 880 /StemV 93 >>\nendobj\n",
                   descriptor) != 0) {
        free(offsets);
        return -1;
    }

    for (int i = 0; i < page_count; i++) {
        int page_no = first_page + i * 2;
        int content_no = page_no + 1;
        const Buf *content = &layout->pages[i];

        offsets[page_no] = out->len;
        if (buf_printf(out,
                       "%d 0 obj\n<< /Type /Page /Parent %d 0 R /MediaBox [0 0 595 842] "
                       "/Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R >>\nendobj\n",
                       page_no, pages, font, content_no) != 0) {
            free(offsets);
            return -1;
        }

        offsets[content_no] = out->len;
        if (buf_printf(out, "%d 0 obj\n<< /Length %lu >>\nstream\n", content_no,
                       (unsigned long)content->len) != 0) {
            free(offsets);
            return -1;
        }
        if (buf_append(out, content->data, content->len) != 0) {
            free(offsets);
            return -1;
        }
        if (buf_puts(out, "endstream\nendobj\n") != 0) {
            free(offsets);
            return -1;
        }
    }

    size_t xref_offset = out->len;
    if (buf_printf(out, "xref\n0 %d\n0000000000 65535 f \n", total_objects + 1) != 0) {
        free(offsets);
        return -1;
    }
    for (int i = 1; i <= total_objects; i++) {
        if (buf_printf(out, "%010lu 00000 n \n", (unsigned long)offsets[i]) != 0) {
            free(offsets);
            return -1;
        }
    }
    int rc = buf_printf(out,
                        "trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%lu\n%%%%EOF\n",
                        total_objects + 1, catalog, (unsigned long)xref_offset);
    free(offsets);
    return rc;
}

static int render_pdf(const Doc *doc, Buf *out) {
    PdfLayout layout;
    pdf_layout_init(&layout);

    int rc = pdf_emit_doc(doc, &layout);
    /* 收尾：把最后一页也入册 */
    if (rc == 0 && pdf_new_page(&layout) != 0) rc = -1;
    if (rc == 0) rc = pdf_assemble(&layout, out);

    pdf_layout_free(&layout);
    return rc;
}

/* ── 对外入口 ── */

int export_render(sqlite3 *db, const cJSON *exam, const char *format,
                  const ExportOptions *options, ExportBuffer *out) {
    if (out == NULL) return EXPORT_INVALID_REQUEST;
    out->data = NULL;
    out->len = 0;
    if (exam == NULL || options == NULL) return EXPORT_INVALID_REQUEST;
    if (!export_format_supported(format)) return EXPORT_UNSUPPORTED_FORMAT;
    if (options->include_ai && !export_ai_available()) return EXPORT_OPTION_UNSUPPORTED;

    /* 复制一份再注入 AI 解答，绝不改动调用方持有的试卷对象。 */
    cJSON *copy = cJSON_Duplicate(exam, 1);
    if (copy == NULL) return EXPORT_RENDER_FAILED;
    if (export_ai_available()) export_attach_ai_answers(db, copy);

    Doc doc;
    doc_init(&doc);
    int built = doc_build(copy, options, &doc);
    cJSON_Delete(copy);
    if (built != 0 || doc.failed) {
        doc_free(&doc);
        return EXPORT_RENDER_FAILED;
    }

    Buf buffer = { 0 };
    int ok;
    if (strcmp(format, "markdown") == 0) {
        ok = render_markdown(&doc, &buffer) == 0;
    } else if (strcmp(format, "latex") == 0) {
        ok = render_latex(&doc, &buffer) == 0;
    } else if (strcmp(format, "docx") == 0) {
        int too_large = 0;
        ok = render_docx(&doc, &buffer, &too_large) == 0;
        if (too_large) buffer.overflow = 1;
    } else {
        ok = render_pdf(&doc, &buffer) == 0;
    }
    doc_free(&doc);

    if (buffer.overflow) {
        buf_reset(&buffer);
        return EXPORT_TOO_LARGE;
    }
    if (!ok || buffer.oom || buffer.data == NULL || buffer.len == 0) {
        buf_reset(&buffer);
        return EXPORT_RENDER_FAILED;
    }

    out->data = buffer.data;
    out->len = buffer.len;
    return EXPORT_OK;
}
