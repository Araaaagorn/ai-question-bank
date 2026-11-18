#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sqlite3.h>

#include "cJSON.h"
#include "kv_store.h"
#include "exams.h"

#define EXAMS_NS "edata"
#define EXAMS_MAX_NAME 100
#define EXAMS_MAX_DESCRIPTION 1000
#define EXAMS_MAX_QUESTIONS 200
#define EXAMS_MAX_SCORE 1000.0

/* ── 工具 ── */

static int key_buf(int id, char *buf, size_t size) {
    return snprintf(buf, size, "%d", id) >= 0;
}

/* 统计 UTF-8 码点数（与 chat_text_valid 的字符计数方式一致）。 */
static size_t utf8_length(const char *s) {
    size_t count = 0;
    for (const unsigned char *p = (const unsigned char *)s; p != NULL && *p != '\0'; p++) {
        if ((*p & 0xc0) != 0x80) count++;
    }
    return count;
}

static int valid_name(const char *name) {
    if (name == NULL || name[0] == '\0') return 0;
    return utf8_length(name) <= EXAMS_MAX_NAME;
}

static int valid_description(const char *description) {
    if (description == NULL) return 1; /* 视为空 */
    return utf8_length(description) <= EXAMS_MAX_DESCRIPTION;
}

static int valid_score(double score) {
    return isfinite(score) && score > 0.0 && score <= EXAMS_MAX_SCORE;
}

/* 题目存在且 status=active 才可被引用。 */
static int question_active(sqlite3 *db, int qid) {
    char key[32];
    if (qid <= 0) return 0;
    snprintf(key, sizeof(key), "%d", qid);

    char *content = kv_get(db, "qdata", key, "content");
    if (content == NULL) return 0;
    free(content);

    char *status = kv_get(db, "qdata", key, "status");
    int active = (status != NULL && strcmp(status, "active") == 0);
    free(status);
    return active;
}

/* 读取试卷存在性与 status（不存在返回 EXAMS_NOT_FOUND）。 */
static int exam_meta(sqlite3 *db, int id, char *status, size_t status_sz,
                     int *teacher_out) {
    char key[32];
    snprintf(key, sizeof(key), "%d", id);

    char *name = kv_get(db, EXAMS_NS, key, "name");
    if (name == NULL) return EXAMS_NOT_FOUND;
    free(name);

    char *s = kv_get(db, EXAMS_NS, key, "status");
    snprintf(status, status_sz, "%s", (s != NULL && s[0] != '\0') ? s : "active");
    free(s);

    if (teacher_out != NULL) {
        int tid = 0;
        char *t = kv_get(db, EXAMS_NS, key, "teacher_id");
        if (t != NULL) {
            if (kv_str_to_int(t, &tid) != 0) tid = 0;
            free(t);
        }
        *teacher_out = tid;
    }
    return EXAMS_OK;
}

static int set_updated(sqlite3 *db, int id) {
    char key[32];
    char now[32];
    snprintf(key, sizeof(key), "%d", id);
    snprintf(now, sizeof(now), "%lld", (long long)time(NULL));
    return kv_set(db, EXAMS_NS, key, "updated_at", now) == 0 ? EXAMS_OK
                                                               : EXAMS_STORE_FAILED;
}

static int next_exam_id(sqlite3 *db, int *out) {
    int count = 0;
    char **keys = kv_list_keys(db, EXAMS_NS, &count);
    if (keys == NULL) return EXAMS_STORE_FAILED;

    int max_id = 0;
    for (int i = 0; i < count; i++) {
        int id = 0;
        if (kv_str_to_int(keys[i], &id) == 0 && id > max_id) max_id = id;
    }
    kv_free_str_array(keys);
    *out = max_id + 1;
    return EXAMS_OK;
}

/* 读取已存储的 questions JSON，始终返回数组（失败/OOM 返回 NULL）。 */
static cJSON *load_questions(sqlite3 *db, int id) {
    char key[32];
    cJSON *arr = NULL;
    snprintf(key, sizeof(key), "%d", id);

    char *raw = kv_get(db, EXAMS_NS, key, "questions");
    if (raw != NULL) {
        arr = cJSON_Parse(raw);
        free(raw);
    }
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        arr = cJSON_CreateArray();
    }
    return arr;
}

/* 校验整体替换的题目集合：字段形态、position 排列、题目引用、score。 */
static int validate_questions(sqlite3 *db, const cJSON *questions) {
    if (!cJSON_IsArray(questions)) return EXAMS_INVALID_REQUEST;

    int n = cJSON_GetArraySize(questions);
    if (n > EXAMS_MAX_QUESTIONS) return EXAMS_INVALID_REQUEST;

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(questions, i);
        if (!cJSON_IsObject(item)) return EXAMS_INVALID_REQUEST;

        cJSON *qid = cJSON_GetObjectItem(item, "question_id");
        cJSON *score = cJSON_GetObjectItem(item, "score");
        cJSON *pos = cJSON_GetObjectItem(item, "position");

        if (!cJSON_IsNumber(qid) || qid->valueint <= 0 ||
            (double)qid->valueint != qid->valuedouble)
            return EXAMS_INVALID_REQUEST;
        if (!cJSON_IsNumber(score) || !valid_score(score->valuedouble))
            return EXAMS_INVALID_REQUEST;
        if (!cJSON_IsNumber(pos) || pos->valueint < 1 || pos->valueint > n ||
            (double)pos->valueint != pos->valuedouble)
            return EXAMS_INVALID_LAYOUT;
        if (!question_active(db, qid->valueint))
            return EXAMS_INVALID_QUESTION;

        for (int j = 0; j < i; j++) {
            cJSON *prev = cJSON_GetArrayItem(questions, j);
            cJSON *pq = cJSON_GetObjectItem(prev, "question_id");
            cJSON *pp = cJSON_GetObjectItem(prev, "position");
            if (pq != NULL && pq->valueint == qid->valueint)
                return EXAMS_INVALID_LAYOUT;
            if (pp != NULL && pp->valueint == pos->valueint)
                return EXAMS_INVALID_LAYOUT;
        }
    }
    return EXAMS_OK;
}

/* 内联一道题目的展示字段（不含 status/teacher_id/seed_key）。 */
static cJSON *inline_question(sqlite3 *db, int qid, int position, double score) {
    char key[32];
    snprintf(key, sizeof(key), "%d", qid);

    char *content = kv_get(db, "qdata", key, "content");
    char *type = kv_get(db, "qdata", key, "type");
    char *options_raw = kv_get(db, "qdata", key, "options");
    char *kp_raw = kv_get(db, "qdata", key, "knowledge_points");
    char *answer = kv_get(db, "qdata", key, "answer");
    char *analysis = kv_get(db, "qdata", key, "analysis");

    /* 悬空引用兜底（docs/exams.md §11）：正常流程由 exams_purge_question 清理，
     * 此处只会因手工改库出现。不返回半截数据，记脱敏日志（不含用户内容）。 */
    if (content == NULL) {
        fprintf(stderr, "[exams] dangling_question_ref (question_id=%d)\n", qid);
        free(content);
        free(type);
        free(options_raw);
        free(kp_raw);
        free(answer);
        free(analysis);
        return NULL;
    }

    cJSON *options = NULL;
    if (options_raw != NULL) options = cJSON_Parse(options_raw);
    if (!cJSON_IsArray(options)) {
        cJSON_Delete(options);
        options = NULL;
    }

    cJSON *kp = NULL;
    if (kp_raw != NULL) kp = cJSON_Parse(kp_raw);
    if (!cJSON_IsArray(kp)) {
        cJSON_Delete(kp);
        kp = NULL;
    }

    cJSON *q = cJSON_CreateObject();
    int ok = (q != NULL);
    if (ok) ok = cJSON_AddNumberToObject(q, "question_id", qid) != NULL;
    if (ok) ok = cJSON_AddNumberToObject(q, "position", position) != NULL;
    if (ok) ok = cJSON_AddNumberToObject(q, "score", score) != NULL;
    if (ok) ok = cJSON_AddStringToObject(q, "content", content ? content : "") != NULL;
    if (ok) ok = cJSON_AddStringToObject(q, "type", type ? type : "") != NULL;
    if (ok) {
        cJSON *opts = options ? options : cJSON_CreateArray();
        options = NULL;
        if (opts == NULL || cJSON_AddItemToObject(q, "options", opts) == 0) {
            cJSON_Delete(opts);
            ok = 0;
        }
    }
    if (ok) {
        cJSON *kps = kp ? kp : cJSON_CreateArray();
        kp = NULL;
        if (kps == NULL || cJSON_AddItemToObject(q, "knowledge_points", kps) == 0) {
            cJSON_Delete(kps);
            ok = 0;
        }
    }
    if (ok) ok = cJSON_AddStringToObject(q, "answer", answer ? answer : "") != NULL;
    if (ok) ok = cJSON_AddStringToObject(q, "analysis", analysis ? analysis : "") != NULL;

    free(content);
    free(type);
    free(options_raw);
    free(kp_raw);
    free(answer);
    free(analysis);
    cJSON_Delete(options);
    cJSON_Delete(kp);

    if (!ok) {
        cJSON_Delete(q);
        return NULL;
    }
    return q;
}

/* 把 questions 数组序列化为字符串（cJSON_free 释放）。 */
static char *questions_to_json(const cJSON *questions) {
    return cJSON_PrintUnformatted(questions);
}

/* ── 公开 API ── */

int exams_init(sqlite3 *db) {
    if (db == NULL) return EXAMS_STORE_FAILED;
    /* kv_store 的 kv_set 依赖 kv_tables 元信息表；kv_init 幂等，这里先确保其存在，
     * 使 exams 模块不依赖调用方的前置初始化顺序（main.c 也会调用，重复无害）。 */
    if (kv_init(db) != 0) return EXAMS_STORE_FAILED;
    const char *schema =
        "CREATE TABLE IF NOT EXISTS edata ("
        "  key_name TEXT NOT NULL,"
        "  value_name TEXT NOT NULL,"
        "  value TEXT DEFAULT '',"
        "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "  updated_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "  PRIMARY KEY (key_name, value_name)"
        ")";
    return sqlite3_exec(db, schema, NULL, NULL, NULL) == SQLITE_OK ? EXAMS_OK
                                                                    : EXAMS_STORE_FAILED;
}

int exams_create(sqlite3 *db, const char *name, const char *description,
                 int teacher_id, int *out_id) {
    if (out_id != NULL) *out_id = 0;
    if (db == NULL || out_id == NULL || teacher_id <= 0) return EXAMS_INVALID_REQUEST;
    if (!valid_name(name)) return EXAMS_INVALID_REQUEST;
    if (!valid_description(description)) return EXAMS_INVALID_REQUEST;

    int id = 0;
    int rc = next_exam_id(db, &id);
    if (rc != EXAMS_OK) return rc;

    char key[32];
    char tid[32];
    char now[32];
    key_buf(id, key, sizeof(key));
    snprintf(tid, sizeof(tid), "%d", teacher_id);
    snprintf(now, sizeof(now), "%lld", (long long)time(NULL));

    const char *desc = description != NULL ? description : "";
    if (kv_set(db, EXAMS_NS, key, "name", name) != 0 ||
        kv_set(db, EXAMS_NS, key, "description", desc) != 0 ||
        kv_set(db, EXAMS_NS, key, "teacher_id", tid) != 0 ||
        kv_set(db, EXAMS_NS, key, "status", "active") != 0 ||
        kv_set(db, EXAMS_NS, key, "created_at", now) != 0 ||
        kv_set(db, EXAMS_NS, key, "updated_at", now) != 0 ||
        kv_set(db, EXAMS_NS, key, "questions", "[]") != 0) {
        return EXAMS_STORE_FAILED;
    }
    *out_id = id;
    return EXAMS_OK;
}

int exams_get(sqlite3 *db, int id, cJSON **out) {
    if (out != NULL) *out = NULL;
    if (db == NULL || out == NULL || id <= 0) return EXAMS_INVALID_REQUEST;

    char key[32];
    key_buf(id, key, sizeof(key));

    char *name = kv_get(db, EXAMS_NS, key, "name");
    if (name == NULL) return EXAMS_NOT_FOUND;

    char *description = kv_get(db, EXAMS_NS, key, "description");
    char *status = kv_get(db, EXAMS_NS, key, "status");
    char *teacher = kv_get(db, EXAMS_NS, key, "teacher_id");
    char *created = kv_get(db, EXAMS_NS, key, "created_at");
    char *updated = kv_get(db, EXAMS_NS, key, "updated_at");
    cJSON *questions = load_questions(db, id);

    int teacher_id = 0;
    if (teacher != NULL) {
        if (kv_str_to_int(teacher, &teacher_id) != 0) teacher_id = 0;
    }
    long long created_at = created != NULL ? strtoll(created, NULL, 10) : 0;
    long long updated_at = updated != NULL ? strtoll(updated, NULL, 10) : 0;

    cJSON *exam = cJSON_CreateObject();
    int ok = (exam != NULL && questions != NULL);
    if (ok) ok = cJSON_AddNumberToObject(exam, "id", id) != NULL;
    if (ok) ok = cJSON_AddStringToObject(exam, "name", name) != NULL;
    if (ok) ok = cJSON_AddStringToObject(exam, "description",
                                         description ? description : "") != NULL;
    if (ok) ok = cJSON_AddNumberToObject(exam, "teacher_id", teacher_id) != NULL;
    if (ok) ok = cJSON_AddStringToObject(exam, "status",
                                         (status && status[0]) ? status : "active") != NULL;
    if (ok) ok = cJSON_AddNumberToObject(exam, "created_at", (double)created_at) != NULL;
    if (ok) ok = cJSON_AddNumberToObject(exam, "updated_at", (double)updated_at) != NULL;

    cJSON *qarr = ok ? cJSON_AddArrayToObject(exam, "questions") : NULL;
    if (ok && qarr == NULL) ok = 0;

    int n = ok ? cJSON_GetArraySize(questions) : 0;
    for (int i = 0; ok && i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(questions, i);
        cJSON *qi = cJSON_GetObjectItem(item, "question_id");
        cJSON *pi = cJSON_GetObjectItem(item, "position");
        cJSON *si = cJSON_GetObjectItem(item, "score");
        if (!cJSON_IsNumber(qi) || !cJSON_IsNumber(pi) || !cJSON_IsNumber(si)) {
            ok = 0;
            break;
        }
        cJSON *inline_q = inline_question(db, qi->valueint, pi->valueint,
                                          si->valuedouble);
        if (inline_q == NULL) {
            ok = 0;
            break;
        }
        cJSON_AddItemToArray(qarr, inline_q);
    }

    free(name);
    free(description);
    free(status);
    free(teacher);
    free(created);
    free(updated);
    cJSON_Delete(questions);

    if (!ok) {
        cJSON_Delete(exam);
        return EXAMS_STORE_FAILED;
    }
    *out = exam;
    return EXAMS_OK;
}

int exams_update_meta(sqlite3 *db, int id, const char *name,
                      const char *description) {
    if (db == NULL || id <= 0) return EXAMS_INVALID_REQUEST;
    if (name == NULL && description == NULL) return EXAMS_INVALID_REQUEST;
    if (name != NULL && !valid_name(name)) return EXAMS_INVALID_REQUEST;
    if (description != NULL && !valid_description(description))
        return EXAMS_INVALID_REQUEST;

    char status[16];
    int rc = exam_meta(db, id, status, sizeof(status), NULL);
    if (rc != EXAMS_OK) return rc;
    if (strcmp(status, "active") != 0) return EXAMS_ARCHIVED;

    char key[32];
    key_buf(id, key, sizeof(key));
    if (name != NULL && kv_set(db, EXAMS_NS, key, "name", name) != 0)
        return EXAMS_STORE_FAILED;
    if (description != NULL &&
        kv_set(db, EXAMS_NS, key, "description", description) != 0)
        return EXAMS_STORE_FAILED;
    return set_updated(db, id);
}

int exams_set_status(sqlite3 *db, int id, const char *status) {
    if (db == NULL || id <= 0 || status == NULL) return EXAMS_INVALID_REQUEST;
    if (strcmp(status, "active") != 0 && strcmp(status, "archived") != 0)
        return EXAMS_INVALID_REQUEST;

    char cur[16];
    int rc = exam_meta(db, id, cur, sizeof(cur), NULL);
    if (rc != EXAMS_OK) return rc;

    char key[32];
    key_buf(id, key, sizeof(key));
    if (kv_set(db, EXAMS_NS, key, "status", status) != 0)
        return EXAMS_STORE_FAILED;
    return set_updated(db, id);
}

int exams_set_questions(sqlite3 *db, int id, const cJSON *questions) {
    if (db == NULL || id <= 0 || questions == NULL) return EXAMS_INVALID_REQUEST;

    char status[16];
    int rc = exam_meta(db, id, status, sizeof(status), NULL);
    if (rc != EXAMS_OK) return rc;
    if (strcmp(status, "active") != 0) return EXAMS_ARCHIVED;

    rc = validate_questions(db, questions);
    if (rc != EXAMS_OK) return rc;

    cJSON *normalized = cJSON_CreateArray();
    if (normalized == NULL) return EXAMS_STORE_FAILED;

    int n = cJSON_GetArraySize(questions);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(questions, i);
        cJSON *qi = cJSON_GetObjectItem(item, "question_id");
        cJSON *si = cJSON_GetObjectItem(item, "score");

        cJSON *entry = cJSON_CreateObject();
        int ok = (entry != NULL);
        if (ok) ok = cJSON_AddNumberToObject(entry, "question_id", qi->valueint) != NULL;
        if (ok) ok = cJSON_AddNumberToObject(entry, "position", i + 1) != NULL;
        if (ok) ok = cJSON_AddNumberToObject(entry, "score", si->valuedouble) != NULL;
        if (ok) {
            cJSON_AddItemToArray(normalized, entry);
        } else {
            cJSON_Delete(entry);
            cJSON_Delete(normalized);
            return EXAMS_STORE_FAILED;
        }
    }

    char *json = questions_to_json(normalized);
    cJSON_Delete(normalized);
    if (json == NULL) return EXAMS_STORE_FAILED;

    char key[32];
    key_buf(id, key, sizeof(key));
    if (kv_set(db, EXAMS_NS, key, "questions", json) != 0) {
        cJSON_free(json);
        return EXAMS_STORE_FAILED;
    }
    cJSON_free(json);
    return set_updated(db, id);
}

int exams_append_question(sqlite3 *db, int id, int question_id, double score) {
    if (db == NULL || id <= 0) return EXAMS_INVALID_REQUEST;
    if (question_id <= 0) return EXAMS_INVALID_QUESTION;
    if (!valid_score(score)) return EXAMS_INVALID_REQUEST;

    char status[16];
    int rc = exam_meta(db, id, status, sizeof(status), NULL);
    if (rc != EXAMS_OK) return rc;
    if (strcmp(status, "active") != 0) return EXAMS_ARCHIVED;
    if (!question_active(db, question_id)) return EXAMS_INVALID_QUESTION;

    cJSON *questions = load_questions(db, id);
    if (questions == NULL) return EXAMS_STORE_FAILED;

    int n = cJSON_GetArraySize(questions);
    if (n >= EXAMS_MAX_QUESTIONS) {
        cJSON_Delete(questions);
        return EXAMS_INVALID_REQUEST;
    }
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(questions, i);
        cJSON *qi = cJSON_GetObjectItem(item, "question_id");
        if (cJSON_IsNumber(qi) && qi->valueint == question_id) {
            cJSON_Delete(questions);
            return EXAMS_INVALID_LAYOUT;
        }
    }

    cJSON *entry = cJSON_CreateObject();
    int ok = (entry != NULL);
    if (ok) ok = cJSON_AddNumberToObject(entry, "question_id", question_id) != NULL;
    if (ok) ok = cJSON_AddNumberToObject(entry, "position", n + 1) != NULL;
    if (ok) ok = cJSON_AddNumberToObject(entry, "score", score) != NULL;
    if (ok) {
        cJSON_AddItemToArray(questions, entry);
    } else {
        cJSON_Delete(entry);
        cJSON_Delete(questions);
        return EXAMS_STORE_FAILED;
    }

    char *json = questions_to_json(questions);
    cJSON_Delete(questions);
    if (json == NULL) return EXAMS_STORE_FAILED;

    char key[32];
    key_buf(id, key, sizeof(key));
    if (kv_set(db, EXAMS_NS, key, "questions", json) != 0) {
        cJSON_free(json);
        return EXAMS_STORE_FAILED;
    }
    cJSON_free(json);
    return set_updated(db, id);
}

int exams_remove_question(sqlite3 *db, int id, int question_id) {
    if (db == NULL || id <= 0 || question_id <= 0) return EXAMS_INVALID_REQUEST;

    char status[16];
    int rc = exam_meta(db, id, status, sizeof(status), NULL);
    if (rc != EXAMS_OK) return rc;
    if (strcmp(status, "active") != 0) return EXAMS_ARCHIVED;

    cJSON *questions = load_questions(db, id);
    if (questions == NULL) return EXAMS_STORE_FAILED;

    int n = cJSON_GetArraySize(questions);
    int found = -1;
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(questions, i);
        cJSON *qi = cJSON_GetObjectItem(item, "question_id");
        if (cJSON_IsNumber(qi) && qi->valueint == question_id) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        cJSON_Delete(questions);
        return EXAMS_NOT_FOUND;
    }

    cJSON_DeleteItemFromArray(questions, found);
    n = cJSON_GetArraySize(questions);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(questions, i);
        cJSON *pos = cJSON_GetObjectItem(item, "position");
        if (cJSON_IsNumber(pos)) cJSON_SetNumberValue(pos, i + 1);
    }

    char *json = questions_to_json(questions);
    cJSON_Delete(questions);
    if (json == NULL) return EXAMS_STORE_FAILED;

    char key[32];
    key_buf(id, key, sizeof(key));
    if (kv_set(db, EXAMS_NS, key, "questions", json) != 0) {
        cJSON_free(json);
        return EXAMS_STORE_FAILED;
    }
    cJSON_free(json);
    return set_updated(db, id);
}

int exams_delete(sqlite3 *db, int id) {
    if (db == NULL || id <= 0) return EXAMS_INVALID_REQUEST;

    char key[32];
    key_buf(id, key, sizeof(key));

    char *name = kv_get(db, EXAMS_NS, key, "name");
    if (name == NULL) return EXAMS_NOT_FOUND;
    free(name);

    return kv_delete_key(db, EXAMS_NS, key) == 0 ? EXAMS_OK : EXAMS_STORE_FAILED;
}

int exams_list(sqlite3 *db, int viewer_id, const char *viewer_role,
               int *out_count, char ***out_ids) {
    if (out_count != NULL) *out_count = 0;
    if (out_ids != NULL) *out_ids = NULL;
    if (db == NULL || viewer_role == NULL || out_count == NULL || out_ids == NULL)
        return EXAMS_INVALID_REQUEST;

    int is_admin = strcmp(viewer_role, "admin") == 0;
    int is_teacher = strcmp(viewer_role, "teacher") == 0;
    int is_student = strcmp(viewer_role, "student") == 0;
    if (!is_admin && !is_teacher && !is_student) return EXAMS_INVALID_REQUEST;

    int total = 0;
    char **keys = kv_list_keys(db, EXAMS_NS, &total);
    if (keys == NULL) return EXAMS_STORE_FAILED;

    char **result = calloc((size_t)total + 1, sizeof(char *));
    if (result == NULL) {
        kv_free_str_array(keys);
        return EXAMS_STORE_FAILED;
    }

    int kept = 0;
    for (int i = 0; i < total; i++) {
        int id = 0;
        if (kv_str_to_int(keys[i], &id) != 0 || id <= 0) continue;

        char status[16];
        int teacher_id = 0;
        if (exam_meta(db, id, status, sizeof(status), &teacher_id) != EXAMS_OK)
            continue;

        int include = 0;
        if (is_admin) {
            include = 1;
        } else if (is_teacher) {
            include = (teacher_id == viewer_id);
        } else {
            include = (strcmp(status, "active") == 0);
        }
        if (include) result[kept++] = strdup(keys[i]);
    }
    result[kept] = NULL;

    kv_free_str_array(keys);
    *out_count = kept;
    *out_ids = result;
    return EXAMS_OK;
}

int exams_purge_question(sqlite3 *db, int question_id) {
    if (db == NULL || question_id <= 0) return EXAMS_INVALID_REQUEST;

    int total = 0;
    char **keys = kv_list_keys(db, EXAMS_NS, &total);
    if (keys == NULL) return EXAMS_STORE_FAILED;

    for (int i = 0; i < total; i++) {
        int id = 0;
        if (kv_str_to_int(keys[i], &id) != 0 || id <= 0) continue;

        cJSON *questions = load_questions(db, id);
        if (questions == NULL) {
            kv_free_str_array(keys);
            return EXAMS_STORE_FAILED;
        }

        int n = cJSON_GetArraySize(questions);
        int changed = 0;
        cJSON *new_arr = cJSON_CreateArray();
        if (new_arr == NULL) {
            cJSON_Delete(questions);
            kv_free_str_array(keys);
            return EXAMS_STORE_FAILED;
        }

        for (int j = 0; j < n; j++) {
            cJSON *item = cJSON_GetArrayItem(questions, j);
            cJSON *qi = cJSON_GetObjectItem(item, "question_id");
            if (cJSON_IsNumber(qi) && qi->valueint == question_id) {
                changed = 1;
                continue;
            }
            cJSON *dup = cJSON_Duplicate(item, 1);
            if (dup == NULL) {
                cJSON_Delete(new_arr);
                cJSON_Delete(questions);
                kv_free_str_array(keys);
                return EXAMS_STORE_FAILED;
            }
            cJSON_AddItemToArray(new_arr, dup);
        }

        if (changed) {
            int m = cJSON_GetArraySize(new_arr);
            for (int j = 0; j < m; j++) {
                cJSON *item = cJSON_GetArrayItem(new_arr, j);
                cJSON *pos = cJSON_GetObjectItem(item, "position");
                if (cJSON_IsNumber(pos)) cJSON_SetNumberValue(pos, j + 1);
            }
            char *json = questions_to_json(new_arr);
            cJSON_Delete(new_arr);
            cJSON_Delete(questions);
            if (json == NULL) {
                kv_free_str_array(keys);
                return EXAMS_STORE_FAILED;
            }
            char key[32];
            key_buf(id, key, sizeof(key));
            if (kv_set(db, EXAMS_NS, key, "questions", json) != 0) {
                cJSON_free(json);
                kv_free_str_array(keys);
                return EXAMS_STORE_FAILED;
            }
            cJSON_free(json);
        } else {
            cJSON_Delete(new_arr);
            cJSON_Delete(questions);
        }
    }
    kv_free_str_array(keys);
    return EXAMS_OK;
}

const char *exams_error_name(ExamsError error) {
    switch (error) {
        case EXAMS_OK: return "EXAMS_OK";
        case EXAMS_NOT_FOUND: return "EXAMS_NOT_FOUND";
        case EXAMS_INVALID_REQUEST: return "EXAMS_INVALID_REQUEST";
        case EXAMS_INVALID_LAYOUT: return "EXAMS_INVALID_LAYOUT";
        case EXAMS_INVALID_QUESTION: return "EXAMS_INVALID_QUESTION";
        case EXAMS_ARCHIVED: return "EXAMS_ARCHIVED";
        case EXAMS_STORE_FAILED: return "EXAMS_STORE_FAILED";
        default: return "EXAMS_UNKNOWN";
    }
}
