/*
 * exams 存储层单元测试（Phase 1，不依赖 server 进程）。
 * 使用 :memory: SQLite，验证创建/读取/更新/状态切换/编排/列表过滤/删除/题目删除联动。
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include "cJSON.h"
#include "exams.h"
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

/* 把一道题写入 qdata（status 默认 active）。 */
static void seed_question(sqlite3 *db, int id, const char *status) {
    char key[16];
    snprintf(key, sizeof(key), "%d", id);
    kv_set(db, "qdata", key, "content", "题目内容");
    kv_set(db, "qdata", key, "type", "short_answer");
    kv_set(db, "qdata", key, "options", "[]");
    kv_set(db, "qdata", key, "answer", "答案");
    kv_set(db, "qdata", key, "analysis", "解析");
    kv_set(db, "qdata", key, "knowledge_points", "[]");
    kv_set(db, "qdata", key, "status", status ? status : "active");
}

static cJSON *questions_of(int a, double sa, int b, double sb) {
    cJSON *arr = cJSON_CreateArray();
    cJSON *e1 = cJSON_CreateObject();
    cJSON_AddNumberToObject(e1, "question_id", a);
    cJSON_AddNumberToObject(e1, "position", 1);
    cJSON_AddNumberToObject(e1, "score", sa);
    cJSON_AddItemToArray(arr, e1);
    cJSON *e2 = cJSON_CreateObject();
    cJSON_AddNumberToObject(e2, "question_id", b);
    cJSON_AddNumberToObject(e2, "position", 2);
    cJSON_AddNumberToObject(e2, "score", sb);
    cJSON_AddItemToArray(arr, e2);
    return arr;
}

static void test_init(sqlite3 *db) {
    TEST("exams_init 幂等");
    ASSERT(exams_init(db) == EXAMS_OK, "首次初始化");
    ASSERT(exams_init(db) == EXAMS_OK, "重复初始化");
    PASS();
}

static void test_create_get(sqlite3 *db) {
    TEST("创建与读取");
    int id = 0;
    ASSERT(exams_create(db, "期中卷", "描述", 2, &id) == EXAMS_OK, "创建");
    ASSERT(id == 1, "第一个 id 应为 1");

    cJSON *exam = NULL;
    ASSERT(exams_get(db, id, &exam) == EXAMS_OK, "读取");
    cJSON *n = cJSON_GetObjectItem(exam, "name");
    cJSON *s = cJSON_GetObjectItem(exam, "status");
    cJSON *t = cJSON_GetObjectItem(exam, "teacher_id");
    cJSON *q = cJSON_GetObjectItem(exam, "questions");
    ASSERT(cJSON_IsString(n) && strcmp(n->valuestring, "期中卷") == 0, "name");
    ASSERT(cJSON_IsString(s) && strcmp(s->valuestring, "active") == 0, "status=active");
    ASSERT(cJSON_IsNumber(t) && t->valueint == 2, "teacher_id=2");
    ASSERT(cJSON_IsArray(q) && cJSON_GetArraySize(q) == 0, "questions 为空");
    cJSON_Delete(exam);
    PASS();
}

static void test_create_invalid(sqlite3 *db) {
    TEST("创建校验：空名 / 超长");
    int id = 0;
    ASSERT(exams_create(db, "", "", 2, &id) == EXAMS_INVALID_REQUEST, "空名拒绝");
    ASSERT(exams_create(db, NULL, "", 2, &id) == EXAMS_INVALID_REQUEST, "NULL 名拒绝");
    char long_name[120];
    memset(long_name, 'a', 101);
    long_name[101] = '\0';
    ASSERT(exams_create(db, long_name, "", 2, &id) == EXAMS_INVALID_REQUEST, "超长名拒绝");
    PASS();
}

static void test_update_meta(sqlite3 *db) {
    TEST("部分更新元数据");
    int id = 0;
    exams_create(db, "A", "old", 2, &id);
    ASSERT(exams_update_meta(db, id, "B", NULL) == EXAMS_OK, "只改名");
    cJSON *exam = NULL;
    exams_get(db, id, &exam);
    cJSON *n = cJSON_GetObjectItem(exam, "name");
    cJSON *d = cJSON_GetObjectItem(exam, "description");
    ASSERT(cJSON_IsString(n) && strcmp(n->valuestring, "B") == 0, "name=B");
    ASSERT(cJSON_IsString(d) && strcmp(d->valuestring, "old") == 0, "desc 保留");
    cJSON_Delete(exam);

    ASSERT(exams_update_meta(db, id, NULL, NULL) == EXAMS_INVALID_REQUEST, "空更新拒绝");
    ASSERT(exams_update_meta(db, id, NULL, "new") == EXAMS_OK, "只改 desc");
    exams_get(db, id, &exam);
    d = cJSON_GetObjectItem(exam, "description");
    ASSERT(cJSON_IsString(d) && strcmp(d->valuestring, "new") == 0, "desc=new");
    cJSON_Delete(exam);
    PASS();
}

static void test_status_lifecycle(sqlite3 *db) {
    TEST("状态切换与 archived 编辑封锁");
    int id = 0;
    exams_create(db, "S", "", 2, &id);

    ASSERT(exams_set_status(db, id, "archived") == EXAMS_OK, "归档");
    ASSERT(exams_update_meta(db, id, "X", NULL) == EXAMS_ARCHIVED, "archived 禁止改元数据");
    cJSON *tmp = questions_of(1, 5, 2, 10);
    ASSERT(exams_set_questions(db, id, tmp) == EXAMS_ARCHIVED, "archived 禁止编排");
    cJSON_Delete(tmp);
    ASSERT(exams_set_status(db, id, "bogus") == EXAMS_INVALID_REQUEST, "非法状态");

    ASSERT(exams_set_status(db, id, "active") == EXAMS_OK, "恢复 active");
    ASSERT(exams_update_meta(db, id, "S2", NULL) == EXAMS_OK, "active 后可改");
    PASS();
}

static void test_set_questions_normalize(sqlite3 *db) {
    TEST("整体替换并归一化 position");
    seed_question(db, 1, "active");
    seed_question(db, 2, "active");
    int id = 0;
    exams_create(db, "Q", "", 2, &id);

    cJSON *arr = cJSON_CreateArray();
    cJSON *e1 = cJSON_CreateObject();
    cJSON_AddNumberToObject(e1, "question_id", 2);
    cJSON_AddNumberToObject(e1, "position", 2);
    cJSON_AddNumberToObject(e1, "score", 10);
    cJSON_AddItemToArray(arr, e1);
    cJSON *e2 = cJSON_CreateObject();
    cJSON_AddNumberToObject(e2, "question_id", 1);
    cJSON_AddNumberToObject(e2, "position", 1);
    cJSON_AddNumberToObject(e2, "score", 5);
    cJSON_AddItemToArray(arr, e2);

    ASSERT(exams_set_questions(db, id, arr) == EXAMS_OK, "整体替换");
    cJSON_Delete(arr);

    cJSON *exam = NULL;
    exams_get(db, id, &exam);
    cJSON *qs = cJSON_GetObjectItem(exam, "questions");
    ASSERT(cJSON_IsArray(qs) && cJSON_GetArraySize(qs) == 2, "两道题");
    cJSON *first = cJSON_GetArrayItem(qs, 0);
    cJSON *second = cJSON_GetArrayItem(qs, 1);
    cJSON *q1 = cJSON_GetObjectItem(first, "question_id");
    cJSON *p1 = cJSON_GetObjectItem(first, "position");
    cJSON *q2 = cJSON_GetObjectItem(second, "question_id");
    cJSON *p2 = cJSON_GetObjectItem(second, "position");
    ASSERT(q1->valueint == 2 && p1->valueint == 1, "数组序权威：id2 变 position1");
    ASSERT(q2->valueint == 1 && p2->valueint == 2, "id1 变 position2");
    cJSON_Delete(exam);
    PASS();
}

static void test_validate_questions(sqlite3 *db) {
    TEST("非法题目集合：不存在/非active/重复/position跳号/score越界");
    seed_question(db, 1, "active");
    seed_question(db, 2, "archived");
    int id = 0;
    exams_create(db, "V", "", 2, &id);

    cJSON *missing = cJSON_CreateArray();
    cJSON *me = cJSON_CreateObject();
    cJSON_AddNumberToObject(me, "question_id", 99);
    cJSON_AddNumberToObject(me, "position", 1);
    cJSON_AddNumberToObject(me, "score", 5);
    cJSON_AddItemToArray(missing, me);
    ASSERT(exams_set_questions(db, id, missing) == EXAMS_INVALID_QUESTION, "不存在题目");
    cJSON_Delete(missing);

    cJSON *arch = cJSON_CreateArray();
    cJSON *ae = cJSON_CreateObject();
    cJSON_AddNumberToObject(ae, "question_id", 2);
    cJSON_AddNumberToObject(ae, "position", 1);
    cJSON_AddNumberToObject(ae, "score", 5);
    cJSON_AddItemToArray(arch, ae);
    ASSERT(exams_set_questions(db, id, arch) == EXAMS_INVALID_QUESTION, "非active题目");
    cJSON_Delete(arch);

    cJSON *dup = questions_of(1, 5, 1, 10);
    ASSERT(exams_set_questions(db, id, dup) == EXAMS_INVALID_LAYOUT, "重复题目");
    cJSON_Delete(dup);

    cJSON *gap = cJSON_CreateArray();
    cJSON *ge = cJSON_CreateObject();
    cJSON_AddNumberToObject(ge, "question_id", 1);
    cJSON_AddNumberToObject(ge, "position", 2);
    cJSON_AddNumberToObject(ge, "score", 5);
    cJSON_AddItemToArray(gap, ge);
    ASSERT(exams_set_questions(db, id, gap) == EXAMS_INVALID_LAYOUT, "position 跳号");
    cJSON_Delete(gap);

    cJSON *badscore = cJSON_CreateArray();
    cJSON *be = cJSON_CreateObject();
    cJSON_AddNumberToObject(be, "question_id", 1);
    cJSON_AddNumberToObject(be, "position", 1);
    cJSON_AddNumberToObject(be, "score", 0);
    cJSON_AddItemToArray(badscore, be);
    ASSERT(exams_set_questions(db, id, badscore) == EXAMS_INVALID_REQUEST, "score=0 拒绝");
    cJSON_Delete(badscore);

    cJSON *nan = cJSON_CreateArray();
    cJSON *ne = cJSON_CreateObject();
    cJSON_AddNumberToObject(ne, "question_id", 1);
    cJSON_AddNumberToObject(ne, "position", 1);
    cJSON_AddNumberToObject(ne, "score", (double)NAN);
    cJSON_AddItemToArray(nan, ne);
    ASSERT(exams_set_questions(db, id, nan) == EXAMS_INVALID_REQUEST, "NaN 拒绝");
    cJSON_Delete(nan);
    PASS();
}

static void test_append_and_remove(sqlite3 *db) {
    TEST("追加与移除后重排");
    seed_question(db, 1, "active");
    seed_question(db, 2, "active");
    seed_question(db, 3, "active");
    int id = 0;
    exams_create(db, "AR", "", 2, &id);

    ASSERT(exams_append_question(db, id, 1, 5) == EXAMS_OK, "追加1");
    ASSERT(exams_append_question(db, id, 2, 10) == EXAMS_OK, "追加2");
    ASSERT(exams_append_question(db, id, 1, 5) == EXAMS_INVALID_LAYOUT, "重复追加拒绝");
    ASSERT(exams_append_question(db, id, 99, 5) == EXAMS_INVALID_QUESTION, "不存在题目");

    cJSON *exam = NULL;
    exams_get(db, id, &exam);
    cJSON *qs = cJSON_GetObjectItem(exam, "questions");
    ASSERT(cJSON_GetArraySize(qs) == 2, "两道题");
    cJSON *second = cJSON_GetArrayItem(qs, 1);
    ASSERT(cJSON_GetObjectItem(second, "position")->valueint == 2, "追加到末尾");
    cJSON_Delete(exam);

    ASSERT(exams_remove_question(db, id, 1) == EXAMS_OK, "移除1");
    ASSERT(exams_remove_question(db, id, 1) == EXAMS_NOT_FOUND, "再移除404");
    exams_get(db, id, &exam);
    qs = cJSON_GetObjectItem(exam, "questions");
    ASSERT(cJSON_GetArraySize(qs) == 1, "剩一道");
    cJSON *first = cJSON_GetArrayItem(qs, 0);
    ASSERT(cJSON_GetObjectItem(first, "question_id")->valueint == 2, "剩余 id2");
    ASSERT(cJSON_GetObjectItem(first, "position")->valueint == 1, "重排为 position1");
    cJSON_Delete(exam);
    PASS();
}

static void test_list_filter(sqlite3 *db) {
    TEST("列表按角色过滤");
    /* 前面用例共用同一 db，先清空 edata，避免残留试卷污染绝对计数断言。 */
    (void)kv_drop_namespace(db, "edata");
    ASSERT(exams_init(db) == EXAMS_OK, "清空后重新初始化");
    int a = 0, b = 0, c = 0;
    exams_create(db, "t1", "", 2, &a);       /* teacher 2 */
    exams_create(db, "t2", "", 3, &b);       /* teacher 3 */
    exams_create(db, "t3", "", 2, &c);       /* teacher 2 */
    exams_set_status(db, b, "archived");      /* teacher 3 归档 */

    int count = 0;
    char **ids = NULL;

    ASSERT(exams_list(db, 2, "teacher", &count, &ids) == EXAMS_OK, "teacher 列表");
    ASSERT(count == 2, "teacher2 应看到自己 2 卷");
    kv_free_str_array(ids);

    ASSERT(exams_list(db, 1, "admin", &count, &ids) == EXAMS_OK, "admin 列表");
    ASSERT(count == 3, "admin 应看到 3 卷");
    kv_free_str_array(ids);

    ASSERT(exams_list(db, 9, "student", &count, &ids) == EXAMS_OK, "student 列表");
    ASSERT(count == 2, "student 只看到 active 的 2 卷");
    kv_free_str_array(ids);
    PASS();
}

static void test_delete(sqlite3 *db) {
    TEST("删除试卷");
    int id = 0;
    exams_create(db, "D", "", 2, &id);
    ASSERT(exams_delete(db, id) == EXAMS_OK, "删除成功");
    ASSERT(exams_delete(db, id) == EXAMS_NOT_FOUND, "重复删除404");
    cJSON *exam = NULL;
    ASSERT(exams_get(db, id, &exam) == EXAMS_NOT_FOUND, "删除后读取404");
    PASS();
}

static void test_purge_question(sqlite3 *db) {
    TEST("题目删除联动：purge 清理多卷引用并重排");
    seed_question(db, 1, "active");
    seed_question(db, 2, "active");
    seed_question(db, 3, "active");

    int e1 = 0, e2 = 0;
    exams_create(db, "P1", "", 2, &e1);
    exams_create(db, "P2", "", 2, &e2);
    cJSON *q1 = questions_of(1, 5, 2, 10);
    cJSON *q2 = questions_of(2, 3, 3, 7);
    ASSERT(exams_set_questions(db, e1, q1) == EXAMS_OK, "卷1 编排");
    ASSERT(exams_set_questions(db, e2, q2) == EXAMS_OK, "卷2 编排");
    cJSON_Delete(q1);
    cJSON_Delete(q2);

    ASSERT(exams_purge_question(db, 2) == EXAMS_OK, "purge id2");

    cJSON *exam = NULL;
    exams_get(db, e1, &exam);
    cJSON *qs = cJSON_GetObjectItem(exam, "questions");
    ASSERT(cJSON_GetArraySize(qs) == 1, "卷1 剩 1 题");
    ASSERT(cJSON_GetObjectItem(cJSON_GetArrayItem(qs, 0), "question_id")->valueint == 1, "卷1 剩 id1");
    cJSON_Delete(exam);

    exams_get(db, e2, &exam);
    qs = cJSON_GetObjectItem(exam, "questions");
    ASSERT(cJSON_GetArraySize(qs) == 1, "卷2 剩 1 题");
    cJSON *f = cJSON_GetArrayItem(qs, 0);
    ASSERT(cJSON_GetObjectItem(f, "question_id")->valueint == 3, "卷2 剩 id3");
    ASSERT(cJSON_GetObjectItem(f, "position")->valueint == 1, "卷2 重排 position1");
    cJSON_Delete(exam);
    PASS();
}

static void test_not_found(sqlite3 *db) {
    TEST("不存在的试卷在各操作返回 NOT_FOUND");
    ASSERT(exams_get(db, 999, NULL) == EXAMS_INVALID_REQUEST, "get NULL 输出返回 INVALID_REQUEST");
    cJSON *exam = NULL;
    ASSERT(exams_get(db, 999, &exam) == EXAMS_NOT_FOUND, "get 404");
    ASSERT(exams_update_meta(db, 999, "x", NULL) == EXAMS_NOT_FOUND, "update 404");
    ASSERT(exams_set_status(db, 999, "active") == EXAMS_NOT_FOUND, "status 404");
    cJSON *tmp = questions_of(1, 5, 2, 10);
    ASSERT(exams_set_questions(db, 999, tmp) == EXAMS_NOT_FOUND, "set 404");
    cJSON_Delete(tmp);
    ASSERT(exams_remove_question(db, 999, 1) == EXAMS_NOT_FOUND, "remove 404");
    PASS();
}

int main(void) {
    sqlite3 *db = NULL;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        fprintf(stderr, "无法打开 :memory: 数据库\n");
        return 1;
    }

    printf("=== exams 存储层单元测试 ===\n\n");

    test_init(db);
    test_create_get(db);
    test_create_invalid(db);
    test_update_meta(db);
    test_status_lifecycle(db);
    test_set_questions_normalize(db);
    test_validate_questions(db);
    test_append_and_remove(db);
    test_list_filter(db);
    test_delete(db);
    test_purge_question(db);
    test_not_found(db);

    sqlite3_close(db);

    printf("\n=======================\n");
    printf("  通过: %d   失败: %d\n", total_pass, total_fail);
    printf("=======================\n");
    return total_fail > 0 ? 1 : 0;
}
