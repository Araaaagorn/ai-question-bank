#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <sqlite3.h>

#include "db.h"

/* 确保数据库文件的父目录存在（SQLite 不会自动建目录）。
 * 框架阶段路径固定为单层（如 data/xxx.db）。 */
static int ensure_parent_dir(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bs = strrchr(path, '\\');
    const char *sep = (slash != NULL && bs != NULL) ? (slash > bs ? slash : bs)
                    : (slash != NULL ? slash : bs);
    if (sep == NULL || sep == path) return 0; /* 无目录部分 */

    size_t len = (size_t)(sep - path);
    if (len >= 512) return -1;
    char dir[512];
    memcpy(dir, path, len);
    dir[len] = '\0';

    struct stat st;
    if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    if (errno != ENOENT) return -1;
    return mkdir(dir, 0755);
}

int db_init(const char *path) {
    if (ensure_parent_dir(path) != 0) {
        fprintf(stderr, "✘ 无法创建数据库目录（%s）\n", path);
        return -1;
    }

    sqlite3 *db = NULL;
    int rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "✘ 打开数据库失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }

    /* 框架阶段基础表（字段随功能迭代补充，对应功能说明书第 3 章数据模型） */
    static const char *schema =
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  role TEXT NOT NULL DEFAULT 'student'," /* admin / teacher / student */
        "  name TEXT NOT NULL,"
        "  username TEXT NOT NULL UNIQUE,"
        "  password_hash TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'active',"
        "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
        ");"
        "CREATE TABLE IF NOT EXISTS questions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  teacher_id INTEGER NOT NULL,"
        "  content TEXT NOT NULL,"
        "  answer TEXT DEFAULT '',"
        "  analysis TEXT DEFAULT '',"
        "  tags TEXT DEFAULT '',"
        "  status TEXT NOT NULL DEFAULT 'active',"
        "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
        ");";

    char *err = NULL;
    rc = sqlite3_exec(db, schema, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "✘ 建表失败: %s\n", err != NULL ? err : "unknown");
        sqlite3_free(err);
    }
    sqlite3_close(db);
    return rc;
}
