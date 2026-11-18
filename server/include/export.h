#ifndef AIQB_EXPORT_H
#define AIQB_EXPORT_H

#include <stddef.h>

#include <sqlite3.h>

#include "cJSON.h"

/**
 * @file export.h
 * @brief 试卷导出（F09）：把试卷渲染成 PDF / Markdown / LaTeX / DOCX。
 *
 * 数据来源：调用方先用 exams_get() 取到试卷详情对象（含内联题目），
 * 再交给 export_render() 渲染。本模块不重复读 qdata 的题目字段，
 * 只额外读写 edata 下的导出策略（value_name "export_policy"）。
 *
 * 渲染层不接触 HTTP、不打印用户内容；错误语义由路由层映射为状态码。
 */

/* 单份导出的字节上限，超过返回 EXPORT_TOO_LARGE（413）。 */
#define EXPORT_MAX_BYTES (8u * 1024u * 1024u)

typedef enum {
    EXPORT_OK = 0,
    EXPORT_NOT_FOUND,
    EXPORT_INVALID_REQUEST,
    EXPORT_UNSUPPORTED_FORMAT,
    EXPORT_OPTION_UNSUPPORTED,
    EXPORT_RENDER_FAILED,
    EXPORT_TOO_LARGE,
    EXPORT_STORE_FAILED
} ExportError;

/**
 * @brief 导出内容选项。
 *        题干始终输出；选项、答案、解析、AI 解答各自独立开关。
 */
typedef struct {
    int include_options;  /* 是否输出选项 */
    int include_answers;  /* 是否输出答案 */
    int include_analysis; /* 是否输出解析 */
    int include_ai;       /* 是否输出 AI 解答（当前无数据源） */
} ExportOptions;

/** 二进制安全的渲染结果，export_buffer_free() 释放。 */
typedef struct {
    unsigned char *data;
    size_t len;
} ExportBuffer;

/** @return 1 表示 format 是受支持的四格式之一（pdf / markdown / latex / docx）。 */
int export_format_supported(const char *format);

/** @return 文件扩展名（"pdf" / "md" / "tex" / "docx"）；format 非法返回 NULL。 */
const char *export_format_extension(const char *format);

/** @return Content-Type 头取值；format 非法返回 NULL。 */
const char *export_format_content_type(const char *format);

/** @return 1 表示 AI 解答数据源已就绪。当前恒为 0。 */
int export_ai_available(void);

/**
 * @brief 读取导出策略。未设置或值损坏时返回默认值（允许导出、允许带答案）。
 * @param allow_export  输出：是否允许导出（约束学生等非归属方）。
 * @param allow_answers 输出：是否允许带答案导出。
 * @return EXPORT_OK / EXPORT_INVALID_REQUEST / EXPORT_STORE_FAILED
 */
int export_policy_get(sqlite3 *db, int exam_id, int *allow_export, int *allow_answers);

/** @brief 写入导出策略（整体覆盖两项）。不修改试卷 updated_at。 */
int export_policy_set(sqlite3 *db, int exam_id, int allow_export, int allow_answers);

/**
 * @brief 渲染试卷。
 * @param db      共享 sqlite3 连接（仅 AI 解答接线后使用，可为 NULL）。
 * @param exam    exams_get() 返回的试卷详情对象，调用方持有。
 * @param format  规范格式名（pdf / markdown / latex / docx）。
 * @param options 内容选项。
 * @param out     输出缓冲，成功时由调用方 export_buffer_free()。
 * @return EXPORT_OK / EXPORT_UNSUPPORTED_FORMAT / EXPORT_OPTION_UNSUPPORTED /
 *         EXPORT_RENDER_FAILED / EXPORT_TOO_LARGE
 */
int export_render(sqlite3 *db, const cJSON *exam, const char *format,
                  const ExportOptions *options, ExportBuffer *out);

/** @brief 释放 ExportBuffer 内容（可重复调用）。 */
void export_buffer_free(ExportBuffer *buffer);

/** @brief 错误枚举 → JSON 响应里的 code 字符串。 */
const char *export_error_code(ExportError error);

#endif /* AIQB_EXPORT_H */
