#ifndef AIQB_EXAMS_H
#define AIQB_EXAMS_H

#include <sqlite3.h>

#include "cJSON.h"

/**
 * @file exams.h
 * @brief 试卷存储层（F03 · 题目的试卷形式组织）。
 *
 * 存储走 kv_store（见 docs/kvstore.md）：
 *   namespace "edata"，key 为试卷 id 的十进制字符串；
 *   value_name 为 name/description/teacher_id/status/created_at/updated_at/questions。
 *
 * 本模块只做数据存取与校验，不构造 HTTP 响应、不打印用户内容到日志。
 * 错误语义由调用方（路由层）映射为 HTTP 状态码，见 docs/exams.md 第 8 节。
 */

typedef enum {
    EXAMS_OK = 0,
    EXAMS_NOT_FOUND,
    EXAMS_INVALID_REQUEST,
    EXAMS_INVALID_LAYOUT,
    EXAMS_INVALID_QUESTION,
    EXAMS_ARCHIVED,
    EXAMS_STORE_FAILED
} ExamsError;

/**
 * @brief 初始化试卷命名空间表（幂等）。
 *        显式建表是为了让 kv_list_keys 在尚无任何试卷时也能正常列举（空表）。
 * @return EXAMS_OK / EXAMS_STORE_FAILED
 */
int exams_init(sqlite3 *db);

/**
 * @brief 创建试卷（status=active，questions 为空）。
 * @param out_id 输出新试卷 id。
 */
int exams_create(sqlite3 *db, const char *name, const char *description,
                 int teacher_id, int *out_id);

/**
 * @brief 读取试卷详情（内联题目展示字段），输出 cJSON 对象。
 *        调用者 cJSON_Delete(*out)。
 */
int exams_get(sqlite3 *db, int id, cJSON **out);

/**
 * @brief 部分更新元数据。name/description 传 NULL 表示不修改该字段；
 *        显式传空串 description 可清空。archived → EXAMS_ARCHIVED。
 */
int exams_update_meta(sqlite3 *db, int id, const char *name,
                      const char *description);

/**
 * @brief 状态切换（"active" / "archived"）。
 */
int exams_set_status(sqlite3 *db, int id, const char *status);

/**
 * @brief 整体替换题目集合；以数组顺序归一化 position 为 1..n。
 */
int exams_set_questions(sqlite3 *db, int id, const cJSON *questions);

/**
 * @brief 追加单题到末尾（position = n+1）。
 */
int exams_append_question(sqlite3 *db, int id, int question_id, double score);

/**
 * @brief 移除单题并重排 position 为 1..n。
 */
int exams_remove_question(sqlite3 *db, int id, int question_id);

/**
 * @brief 删除试卷（不影响 qdata）。
 */
int exams_delete(sqlite3 *db, int id);

/**
 * @brief 列出可见试卷的 id 字符串数组（NULL 哨兵）。
 *        admin 全部；teacher 仅本人创建；student 仅 active。
 *        调用者用 kv_free_str_array(*out_ids) 释放。
 */
int exams_list(sqlite3 *db, int viewer_id, const char *viewer_role,
               int *out_count, char ***out_ids);

/**
 * @brief 题目删除联动：从所有试卷中移除该题目引用并重排 position。
 */
int exams_purge_question(sqlite3 *db, int question_id);

/** @brief 错误枚举的可读名（用于日志/测试，不含用户内容）。 */
const char *exams_error_name(ExamsError error);

#endif /* AIQB_EXAMS_H */
