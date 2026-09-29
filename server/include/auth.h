#ifndef AIQB_AUTH_H
#define AIQB_AUTH_H

#include <stdbool.h>
#include <time.h>

#define SESSION_TOKEN_LEN 64
#define MAX_SESSIONS 128

/* 会话结构体（内存存储，仅 demo/单线程使用） */
typedef struct {
    char token[SESSION_TOKEN_LEN + 1];
    int  user_id;
    char role[16];
    char name[64];
    time_t expires_at;
    bool   active;
} Session;

/* 创建一个新的 session，返回 token 字符串指针（内部静态缓冲区，每次调用覆盖）。
 * 失败返回 NULL。 */
const char *session_create(int user_id, const char *role, const char *name);

/* 验证 token 是否有效，返回对应 Session 指针；无效/过期返回 NULL。 */
Session *session_validate(const char *token);

/* 销毁指定 token 的 session。 */
void session_destroy(const char *token);

/* 从 "Authorization: Bearer <token>" 头中提取 token 字符串。
 * 直接返回 header 内的指针偏移，不分配内存。不存在或格式错误返回 NULL。 */
const char *extract_bearer_token(const char *auth_header);

#endif /* AIQB_AUTH_H */