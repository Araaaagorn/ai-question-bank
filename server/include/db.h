#ifndef AIQB_DB_H
#define AIQB_DB_H

#include <stdint.h>

/* 初始化数据库：确保数据目录存在、打开数据库并创建基础表。
 * 返回 0 成功；非 0 失败（SQLite 错误码或 -1）。 */
int db_init(const char *path);

/* 按用户名查询用户，返回 json 字符串（如 {"id":1,"role":"admin","name":"管理员"}），
 * 调用者负责 free()。查不到返回 NULL。 */
char *db_query_user_by_username(const char *username);

/* 按用户名查询密码 hash，返回 hash 字符串，调用者负责 free()。
 * 查不到返回 NULL。 */
char *db_query_password_hash(const char *username);

/* 固定题目查询。连接来自传入的配置路径，不依赖共享可变状态。
 * 成功时 out_json 指向 cJSON 分配的完整响应，调用者 cJSON_free()。
 * 不存在与数据库/JSON 分配失败分别返回 NOT_FOUND 与 ERROR。 */
typedef enum {
    DB_QUESTION_OK = 0,
    DB_QUESTION_NOT_FOUND,
    DB_QUESTION_ERROR
} DbQuestionResult;

DbQuestionResult db_list_questions(const char *path, char **out_json);
DbQuestionResult db_get_question(const char *path, int64_t id, char **out_json);

#endif /* AIQB_DB_H */
