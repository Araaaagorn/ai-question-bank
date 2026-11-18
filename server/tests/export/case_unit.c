/*
 * export 渲染层单元测试（Phase 1，不依赖 server 进程）。
 * 覆盖格式元数据、四种渲染器、答案/解析独立开关、策略默认值与写入、AI 预留位。
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "cJSON.h"
#include "export.h"
#include "kv_store.h"

static int total_pass = 0;
static int total_fail = 0;

#define TEST(name)                              \
    do {                                        \
        printf("  [TEST] %s ... ", name);       \
        fflush(stdout);                         \
    } while (0)

#define PASS() do { printf("✓\n"); total_pass++; } while (0)

#define FAIL(...) do {                          \
    printf("✘  ");                              \
    printf(__VA_ARGS__);                        \
    printf("\n");                                \
    total_fail++;                                \
} while (0)

#define ASSERT(cond, ...)                       \
    do {                                        \
        if (!(cond)) { FAIL(__VA_ARGS__); return; } \
    } while (0)

static cJSON *sample_exam(void) {
    cJSON *exam = cJSON_CreateObject();
    cJSON_AddStringToObject(exam, "name", "期中导出卷");
    cJSON_AddStringToObject(exam, "description", "用于导出测试");
    cJSON *questions = cJSON_AddArrayToObject(exam, "questions");

    cJSON *q1 = cJSON_CreateObject();
    cJSON_AddStringToObject(q1, "content", "题干一");
    cJSON_AddStringToObject(q1, "type", "single_choice");
    cJSON *opts = cJSON_AddArrayToObject(q1, "options");
    cJSON *o1 = cJSON_CreateObject();
    cJSON_AddStringToObject(o1, "label", "A");
    cJSON_AddStringToObject(o1, "text", "甲");
    cJSON_AddItemToArray(opts, o1);
    cJSON *o2 = cJSON_CreateObject();
    cJSON_AddStringToObject(o2, "label", "B");
    cJSON_AddStringToObject(o2, "text", "乙");
    cJSON_AddItemToArray(opts, o2);
    cJSON_AddStringToObject(q1, "answer", "B");
    cJSON_AddStringToObject(q1, "analysis", "解析一");
    cJSON_AddNumberToObject(q1, "score", 5);
    cJSON_AddItemToArray(questions, q1);

    cJSON *q2 = cJSON_CreateObject();
    cJSON_AddStringToObject(q2, "content", "题干二");
    cJSON_AddStringToObject(q2, "type", "short_answer");
    cJSON_AddItemToObject(q2, "options", cJSON_CreateArray());
    cJSON_AddStringToObject(q2, "answer", "答案二");
    cJSON_AddStringToObject(q2, "analysis", "解析二");
    cJSON_AddNumberToObject(q2, "score", 10);
    cJSON_AddItemToArray(questions, q2);
    return exam;
}

static char *render_text(const cJSON *exam, const char *format,
                         const ExportOptions *options, ExportError *out_error) {
    ExportBuffer buffer = { NULL, 0 };
    ExportError err = export_render(NULL, exam, format, options, &buffer);
    if (out_error != NULL) *out_error = err;
    if (err != EXPORT_OK) {
        export_buffer_free(&buffer);
        return NULL;
    }
    char *text = malloc(buffer.len + 1);
    if (text != NULL) {
        memcpy(text, buffer.data, buffer.len);
        text[buffer.len] = '\0';
    }
    export_buffer_free(&buffer);
    return text;
}

static int contains(const char *text, const char *needle) {
    return text != NULL && strstr(text, needle) != NULL;
}

static int bytes_contain(const unsigned char *data, size_t len, const char *needle) {
    size_t n = strlen(needle);
    if (n == 0 || len < n) return 0;
    for (size_t i = 0; i + n <= len; i++) {
        if (memcmp(data + i, needle, n) == 0) return 1;
    }
    return 0;
}

static void test_format_metadata(void) {
    TEST("格式元数据与错误码");
    const char *formats[] = { "pdf", "markdown", "latex", "docx" };
    const char *exts[] = { "pdf", "md", "tex", "docx" };
    for (int i = 0; i < 4; i++) {
        ASSERT(export_format_supported(formats[i]) == 1, "%s 应支持", formats[i]);
        ASSERT(strcmp(export_format_extension(formats[i]), exts[i]) == 0,
               "%s 扩展名", formats[i]);
        ASSERT(export_format_content_type(formats[i]) != NULL,
               "%s Content-Type", formats[i]);
    }
    ASSERT(export_format_supported("PDF") == 0, "大小写敏感");
    ASSERT(export_format_supported("doc") == 0, "不支持旧版 doc");
    ASSERT(export_format_extension("bogus") == NULL, "非法格式扩展名");
    ASSERT(strcmp(export_error_code(EXPORT_OPTION_UNSUPPORTED),
                  "EXPORT_OPTION_UNSUPPORTED") == 0, "错误码");
    ASSERT(export_ai_available() == 0, "AI 预留位当前不可用");
    PASS();
}

static void test_markdown_defaults(sqlite3 *db) {
    (void)db;
    TEST("Markdown 默认包含题干/选项/答案/解析");
    cJSON *exam = sample_exam();
    ExportOptions options = { 1, 1, 1, 0 };
    char *text = render_text(exam, "markdown", &options, NULL);
    ASSERT(text != NULL, "渲染失败");
    ASSERT(contains(text, "# 期中导出卷"), "标题");
    ASSERT(contains(text, "共 2 题 · 总分 15 分"), "题数/总分");
    ASSERT(contains(text, "## 第 1 题（5 分）"), "题号");
    ASSERT(contains(text, "题干一"), "题干");
    ASSERT(contains(text, "- A. 甲"), "选项");
    ASSERT(contains(text, "**答案：B**"), "答案");
    ASSERT(contains(text, "**解析：解析一**"), "解析");
    ASSERT(contains(text, "**答案：答案二**"), "第二题答案");
    free(text);
    cJSON_Delete(exam);
    PASS();
}

static void test_independent_switches(sqlite3 *db) {
    (void)db;
    TEST("答案与解析独立开关");
    cJSON *exam = sample_exam();

    ExportOptions no_analysis = { 0, 1, 0, 0 };
    char *text = render_text(exam, "markdown", &no_analysis, NULL);
    ASSERT(text != NULL, "渲染失败");
    ASSERT(contains(text, "**答案：B**"), "保留答案");
    ASSERT(!contains(text, "**解析：解析一**"), "关闭解析");
    ASSERT(!contains(text, "- A. 甲"), "关闭选项");
    free(text);

    ExportOptions only_analysis = { 0, 0, 1, 0 };
    text = render_text(exam, "markdown", &only_analysis, NULL);
    ASSERT(text != NULL, "渲染失败");
    ASSERT(contains(text, "**解析：解析一**"), "保留解析");
    ASSERT(!contains(text, "**答案：B**"), "关闭答案");
    free(text);

    ExportOptions none = { 0, 0, 0, 0 };
    text = render_text(exam, "markdown", &none, NULL);
    ASSERT(text != NULL, "渲染失败");
    ASSERT(contains(text, "题干一"), "题干仍输出");
    ASSERT(!contains(text, "答案："), "无答案");
    ASSERT(!contains(text, "解析："), "无解析");
    free(text);

    cJSON_Delete(exam);
    PASS();
}

static void test_pdf(sqlite3 *db) {
    (void)db;
    TEST("PDF 结构");
    cJSON *exam = sample_exam();
    ExportOptions options = { 1, 1, 1, 0 };
    char *text = render_text(exam, "pdf", &options, NULL);
    ASSERT(text != NULL, "渲染失败");
    ASSERT(contains(text, "%PDF-1.4"), "PDF 头");
    ASSERT(contains(text, "%%EOF"), "PDF 尾");
    ASSERT(contains(text, "/UniGB-UCS2-H"), "中文字体编码");
    ASSERT(contains(text, "/STSong-Light"), "中文字体");
    free(text);
    cJSON_Delete(exam);
    PASS();
}

static void test_latex(sqlite3 *db) {
    (void)db;
    TEST("LaTeX 结构");
    cJSON *exam = sample_exam();
    ExportOptions options = { 1, 1, 1, 0 };
    char *text = render_text(exam, "latex", &options, NULL);
    ASSERT(text != NULL, "渲染失败");
    ASSERT(contains(text, "\\documentclass[11pt,a4paper]{ctexart}"), "文档类");
    ASSERT(contains(text, "\\begin{itemize}"), "选项列表");
    ASSERT(contains(text, "\\end{itemize}"), "选项列表闭合");
    ASSERT(contains(text, "\\end{document}"), "文档结束");
    free(text);
    cJSON_Delete(exam);
    PASS();
}

static void test_docx(sqlite3 *db) {
    (void)db;
    TEST("DOCX ZIP 结构");
    cJSON *exam = sample_exam();
    ExportOptions options = { 1, 1, 1, 0 };
    ExportBuffer buffer = { NULL, 0 };
    ASSERT(export_render(NULL, exam, "docx", &options, &buffer) == EXPORT_OK,
           "渲染失败");
    ASSERT(buffer.len > 4 && memcmp(buffer.data, "PK\x03\x04", 4) == 0,
           "ZIP 魔数");
    ASSERT(bytes_contain(buffer.data, buffer.len, "[Content_Types].xml"),
           "Content_Types");
    ASSERT(bytes_contain(buffer.data, buffer.len, "word/document.xml"),
           "document.xml");
    ASSERT(bytes_contain(buffer.data, buffer.len, "_rels/.rels"), "根关系");
    export_buffer_free(&buffer);
    cJSON_Delete(exam);
    PASS();
}

static void test_docx_line_breaks_and_score(void) {
    TEST("DOCX 保留换行且极小分值不丢精度");
    cJSON *exam = cJSON_CreateObject();
    cJSON_AddStringToObject(exam, "name", "换行测试卷");
    cJSON *questions = cJSON_AddArrayToObject(exam, "questions");
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "content", "第一行\n第二行");
    cJSON_AddStringToObject(q, "type", "short_answer");
    cJSON_AddItemToObject(q, "options", cJSON_CreateArray());
    cJSON_AddStringToObject(q, "answer", "");
    cJSON_AddStringToObject(q, "analysis", "");
    cJSON_AddNumberToObject(q, "score", 0.00001);
    cJSON_AddItemToArray(questions, q);

    ExportOptions options = { 0, 0, 0, 0 };
    ExportBuffer buffer = { NULL, 0 };
    ASSERT(export_render(NULL, exam, "docx", &options, &buffer) == EXPORT_OK,
           "DOCX 渲染失败");
    ASSERT(bytes_contain(buffer.data, buffer.len, "<w:br/>"), "缺少换行符");
    ASSERT(bytes_contain(buffer.data, buffer.len, "第一行"), "缺少第一行");
    ASSERT(bytes_contain(buffer.data, buffer.len, "第二行"), "缺少第二行");
    ASSERT(!bytes_contain(buffer.data, buffer.len, "第一行第二行"),
           "换行被吞掉");
    export_buffer_free(&buffer);

    char *markdown = render_text(exam, "markdown", &options, NULL);
    ASSERT(markdown != NULL, "Markdown 渲染失败");
    ASSERT(contains(markdown, "0.00001 分"), "极小分值被格式化为 0");
    free(markdown);
    cJSON_Delete(exam);
    PASS();
}

static void test_docx_too_large(void) {
    TEST("DOCX 超过 8 MiB 返回 EXPORT_TOO_LARGE");
    const size_t content_size = 9u * 1024u * 1024u;
    char *content = malloc(content_size + 1);
    ASSERT(content != NULL, "分配测试内容失败");
    memset(content, 'A', content_size);
    content[content_size] = '\0';

    cJSON *exam = cJSON_CreateObject();
    cJSON_AddStringToObject(exam, "name", "超限测试卷");
    cJSON *questions = cJSON_AddArrayToObject(exam, "questions");
    cJSON *q = cJSON_CreateObject();
    cJSON_AddStringToObject(q, "content", content);
    cJSON_AddStringToObject(q, "type", "short_answer");
    cJSON_AddItemToObject(q, "options", cJSON_CreateArray());
    cJSON_AddStringToObject(q, "answer", "");
    cJSON_AddStringToObject(q, "analysis", "");
    cJSON_AddNumberToObject(q, "score", 1);
    cJSON_AddItemToArray(questions, q);
    free(content);

    ExportOptions options = { 0, 0, 0, 0 };
    ExportBuffer buffer = { NULL, 0 };
    ExportError err = export_render(NULL, exam, "docx", &options, &buffer);
    cJSON_Delete(exam);
    ASSERT(err == EXPORT_TOO_LARGE, "应返回 EXPORT_TOO_LARGE，实际 %d", (int)err);
    ASSERT(buffer.data == NULL && buffer.len == 0, "超限不应返回半份数据");
    PASS();
}

static void test_ai_reserved(sqlite3 *db) {
    (void)db;
    TEST("AI 解答仅预留");
    cJSON *exam = sample_exam();
    ExportOptions options = { 1, 1, 1, 1 };
    ExportBuffer buffer = { NULL, 0 };
    ASSERT(export_render(NULL, exam, "markdown", &options, &buffer) ==
               EXPORT_OPTION_UNSUPPORTED, "ai=1 应拒绝");
    ASSERT(buffer.data == NULL && buffer.len == 0, "失败不返回半份数据");
    cJSON_Delete(exam);
    PASS();
}

static void test_policy(sqlite3 *db) {
    TEST("导出策略默认值与覆盖");
    ASSERT(kv_init(db) == 0, "kv_init");
    ASSERT(kv_set(db, "edata", "1", "name", "策略卷") == 0, "写入试卷名");

    int allow_export = -1;
    int allow_answers = -1;
    ASSERT(export_policy_get(db, 1, &allow_export, &allow_answers) == EXPORT_OK,
           "读取默认策略");
    ASSERT(allow_export == 1 && allow_answers == 1, "默认允许导出/答案");

    const char *corrupt[] = {
        "{bad",
        "[]",
        "{\"allow_export\":\"no\"}",
        "{\"allow_export\":false} trailing",
    };
    for (size_t i = 0; i < sizeof corrupt / sizeof corrupt[0]; i++) {
        ASSERT(kv_set(db, "edata", "1", "export_policy", corrupt[i]) == 0,
               "写入损坏策略 %zu", i);
        allow_export = -1;
        allow_answers = -1;
        ASSERT(export_policy_get(db, 1, &allow_export, &allow_answers) == EXPORT_OK,
               "损坏策略 %zu 应回退默认值", i);
        ASSERT(allow_export == 1 && allow_answers == 1,
               "损坏策略 %zu 不应改变默认值", i);
    }

    ASSERT(export_policy_set(db, 1, 0, 0) == EXPORT_OK, "写入策略");
    ASSERT(export_policy_get(db, 1, &allow_export, &allow_answers) == EXPORT_OK,
           "回读策略");
    ASSERT(allow_export == 0 && allow_answers == 0, "策略已覆盖");

    ASSERT(export_policy_set(db, 99, 1, 1) == EXPORT_NOT_FOUND,
           "不存在的试卷");
    ASSERT(export_policy_get(db, 0, NULL, NULL) == EXPORT_INVALID_REQUEST,
           "非法 id");
    PASS();
}

int main(void) {
    sqlite3 *db = NULL;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        fprintf(stderr, "无法打开内存数据库\n");
        return 1;
    }

    printf("export 单元测试\n");
    test_format_metadata();
    test_markdown_defaults(db);
    test_independent_switches(db);
    test_pdf(db);
    test_latex(db);
    test_docx(db);
    test_docx_line_breaks_and_score();
    test_docx_too_large();
    test_ai_reserved(db);
    test_policy(db);

    sqlite3_close(db);
    printf("\n通过: %d  失败: %d\n", total_pass, total_fail);
    return total_fail == 0 ? 0 : 1;
}
