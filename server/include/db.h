#ifndef AIQB_DB_H
#define AIQB_DB_H

/* 初始化数据库：确保数据目录存在、打开数据库并创建基础表。
 * 返回 0 成功；非 0 失败（SQLite 错误码或 -1）。 */
int db_init(const char *path);

#endif /* AIQB_DB_H */
