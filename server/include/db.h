#ifndef AIQB_DB_H
#define AIQB_DB_H

/* 初始化数据库：确保数据目录存在、打开数据库并创建基础表。
 * 返回 0 成功；非 0 失败（SQLite 错误码或 -1）。 */
int db_init(const char *path);

/* 按用户名查询用户，返回 json 字符串（如 {"id":1,"role":"admin","name":"管理员"}），
 * 调用者负责 free()。查不到返回 NULL。 */
char *db_query_user_by_username(const char *username);

/* 按用户名查询密码 hash，返回 hash 字符串，调用者负责 free()。
 * 查不到返回 NULL。 */
char *db_query_password_hash(const char *username);

#endif /* AIQB_DB_H */
