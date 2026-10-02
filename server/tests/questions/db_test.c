#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "cJSON.h"
#include "db.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

static int scalar(sqlite3 *db, const char *sql) {
    sqlite3_stmt *stmt = NULL;
    int value = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

int main(int argc, char **argv) {
    CHECK(argc == 3);
    sqlite3 *db = NULL;
    CHECK(db_init(argv[1]) == SQLITE_OK);
    CHECK(db_init(argv[1]) == SQLITE_OK);
    CHECK(sqlite3_open(argv[1], &db) == SQLITE_OK);
    CHECK(scalar(db, "SELECT count(*) FROM questions") == 5);
    CHECK(scalar(db, "SELECT count(DISTINCT seed_key) FROM questions") == 5);
    CHECK(sqlite3_exec(db, "UPDATE questions SET content = 'preserved' WHERE id = 1",
                      NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    CHECK(db_init(argv[1]) == SQLITE_OK);
    char *json = NULL;
    CHECK(db_get_question(argv[1], 1, &json) == DB_QUESTION_OK);
    cJSON *root = cJSON_Parse(json);
    cJSON *question = cJSON_GetObjectItem(root, "question");
    cJSON *content = cJSON_GetObjectItem(question, "content");
    CHECK(cJSON_IsString(content) && strcmp(content->valuestring, "preserved") == 0);
    cJSON_Delete(root);
    cJSON_free(json);
    json = NULL;
    CHECK(db_get_question(argv[1], 999999, &json) == DB_QUESTION_NOT_FOUND && json == NULL);

    CHECK(sqlite3_open(argv[1], &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "UPDATE questions SET status = 'archived'", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    CHECK(db_list_questions(argv[1], &json) == DB_QUESTION_OK);
    CHECK(strcmp(json, "{\"questions\":[]}") == 0);
    cJSON_free(json);
    json = NULL;
    CHECK(db_get_question(argv[1], 1, &json) == DB_QUESTION_NOT_FOUND && json == NULL);

    /* 模拟框架版本已有的表和 id=1 的业务数据；迁移不能覆盖或删除它。 */
    CHECK(sqlite3_open(argv[2], &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db,
        "CREATE TABLE questions (id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "teacher_id INTEGER NOT NULL, content TEXT NOT NULL, answer TEXT DEFAULT '',"
        "analysis TEXT DEFAULT '', tags TEXT DEFAULT '', status TEXT DEFAULT 'active',"
        "created_at TEXT NOT NULL DEFAULT (datetime('now')));"
        "INSERT INTO questions(teacher_id, content) VALUES (42, 'legacy');",
        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    CHECK(db_init(argv[2]) == SQLITE_OK);
    CHECK(db_init(argv[2]) == SQLITE_OK);
    CHECK(sqlite3_open(argv[2], &db) == SQLITE_OK);
    CHECK(scalar(db, "SELECT count(*) FROM questions") == 6);
    CHECK(scalar(db, "SELECT count(*) FROM questions WHERE id=1 AND content='legacy' AND teacher_id=42") == 1);
    CHECK(scalar(db, "SELECT count(*) FROM questions WHERE seed_key IS NOT NULL") == 5);
    sqlite3_close(db);
    CHECK(db_get_question(argv[2], 1, &json) == DB_QUESTION_OK);
    root = cJSON_Parse(json);
    question = cJSON_GetObjectItem(root, "question");
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(question, "options")) == 0);
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(question, "knowledge_points")) == 0);
    cJSON_Delete(root);
    cJSON_free(json);
    puts("PASS: 重复 seed 无重复且保留修改；旧表增量升级保留已有记录；归档/空列表语义正确");
    return 0;
}
