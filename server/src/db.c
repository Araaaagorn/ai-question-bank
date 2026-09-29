#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <sqlite3.h>

/* OpenSSL SHA-256（通过 -lcrypto 链接；libcurl-dev 已间接依赖 libssl-dev） */
#include <openssl/sha.h>

#include "cJSON.h"
#include "db.h"

/* ── SHA-256 工具 ── */

/* 计算 SHA-256(salt || password) 的十六进制字符串。
 * 返回值由 malloc 分配，调用者 free()。 */
static char *hash_password(const char *password, const char *salt) {
    if (password == NULL) password = "";
    if (salt == NULL) salt = "";

    size_t plen = strlen(password);
    size_t slen = strlen(salt);
    size_t total = plen + slen;
    char *input = malloc(total + 1);
    if (input == NULL) return NULL;

    memcpy(input, salt, slen);
    memcpy(input + slen, password, plen);
    input[total] = '\0';

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)input, total, digest);
    free(input);

    char *hex = malloc(SHA256_DIGEST_LENGTH * 2 + 1);
    if (hex == NULL) return NULL;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hex + i * 2, "%02x", digest[i]);
    }
    hex[SHA256_DIGEST_LENGTH * 2] = '\0';
    return hex;
}

/* ── 确保目录存在 ── */

static int ensure_parent_dir(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bs = strrchr(path, '\\');
    const char *sep = (slash != NULL && bs != NULL) ? (slash > bs ? slash : bs)
                    : (slash != NULL ? slash : bs);
    if (sep == NULL || sep == path) return 0;

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

/* ── 插入 demo 账号（密码来自环境变量，有默认值） ── */

static int seed_demo_accounts(sqlite3 *db) {
    /* 密码从环境变量读取，不在源码中硬编码明文敏感值 */
    const char *admin_pw   = getenv("DEMO_ADMIN_PW");
    const char *teacher_pw = getenv("DEMO_TEACHER_PW");
    const char *student_pw = getenv("DEMO_STUDENT_PW");
    if (admin_pw == NULL)   admin_pw   = "admin123";
    if (teacher_pw == NULL) teacher_pw = "teacher123";
    if (student_pw == NULL) student_pw = "student123";

    /* 固定 salt 用于 demo（对抗彩虹表，salt 不要求保密） */
    const char *salt = "ai-question-bank-demo-salt-2026";

    struct {
        const char *username;
        const char *name;
        const char *role;
        const char *pw;
    } demos[] = {
        {"admin",   "系统管理员", "admin",   admin_pw},
        {"teacher", "张老师",     "teacher", teacher_pw},
        {"student", "李同学",     "student", student_pw},
    };
    int count = sizeof(demos) / sizeof(demos[0]);

    const char *sql = "INSERT OR IGNORE INTO users "
                      "(username, name, role, password_hash) "
                      "VALUES (?, ?, ?, ?)";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "✘ 预编译 demo 插入语句失败: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    for (int i = 0; i < count; i++) {
        char *hash = hash_password(demos[i].pw, salt);
        if (hash == NULL) {
            fprintf(stderr, "✘ 密码哈希分配失败\n");
            sqlite3_finalize(stmt);
            return -1;
        }

        sqlite3_bind_text(stmt, 1, demos[i].username, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, demos[i].name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, demos[i].role, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, hash, -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "⚠ 插入 demo 账号 '%s' 失败: %s\n",
                    demos[i].username, sqlite3_errmsg(db));
        }

        sqlite3_reset(stmt);
        free(hash);
    }

    sqlite3_finalize(stmt);
    return 0;
}

/* ── 公开 API ── */

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

    /* 框架阶段基础表 */
    static const char *schema =
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  role TEXT NOT NULL DEFAULT 'student',"
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
        sqlite3_close(db);
        return rc;
    }

    /* 插入演示账号 */
    rc = seed_demo_accounts(db);
    if (rc != 0) {
        fprintf(stderr, "✘ 插入演示账号失败\n");
        sqlite3_close(db);
        return rc;
    }

    sqlite3_close(db);
    return 0;
}

char *db_query_user_by_username(const char *username) {
    if (username == NULL) return NULL;

    sqlite3 *db = NULL;
    const char *path = getenv("DATABASE_PATH");
    if (path == NULL) path = "data/ai_question_bank.db";

    if (sqlite3_open(path, &db) != SQLITE_OK) {
        fprintf(stderr, "✘ 打开数据库失败（查询用户）: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }

    const char *sql = "SELECT id, username, name, role FROM users "
                      "WHERE username = ? AND status = 'active' LIMIT 1";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "✘ 预编译查询用户语句失败\n");
        sqlite3_close(db);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    char *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int id          = sqlite3_column_int(stmt, 0);
        const char *u   = (const char *)sqlite3_column_text(stmt, 1);
        const char *n   = (const char *)sqlite3_column_text(stmt, 2);
        const char *r   = (const char *)sqlite3_column_text(stmt, 3);

        cJSON *obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "id", id);
        cJSON_AddStringToObject(obj, "username", u ? u : "");
        cJSON_AddStringToObject(obj, "name", n ? n : "");
        cJSON_AddStringToObject(obj, "role", r ? r : "");
        result = cJSON_PrintUnformatted(obj);
        cJSON_Delete(obj);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}

char *db_query_password_hash(const char *username) {
    if (username == NULL) return NULL;

    sqlite3 *db = NULL;
    const char *path = getenv("DATABASE_PATH");
    if (path == NULL) path = "data/ai_question_bank.db";

    if (sqlite3_open(path, &db) != SQLITE_OK) {
        fprintf(stderr, "✘ 打开数据库失败（查询密码）: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }

    const char *sql = "SELECT password_hash FROM users "
                      "WHERE username = ? AND status = 'active' LIMIT 1";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "✘ 预编译查询密码语句失败\n");
        sqlite3_close(db);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    char *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *hash = (const char *)sqlite3_column_text(stmt, 0);
        if (hash) result = strdup(hash);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}