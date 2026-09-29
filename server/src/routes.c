#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <microhttpd.h>

#include "auth.h"
#include "cJSON.h"
#include "config.h"
#include "db.h"
#include "routes.h"

/* ── 密码哈希（与 db.c 保持一致的算法和 salt） ── */
#include <openssl/sha.h>

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

/* ── JSON 工具 ── */

/* 从 JSON 对象中安全提取字符串值，返回 malloc 副本或 NULL */
static char *json_get_string(const cJSON *root, const char *key) {
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item == NULL || !cJSON_IsString(item)) return NULL;
    return strdup(item->valuestring);
}

/* ── 响应工具 ── */

static enum MHD_Result reply_json(struct MHD_Connection *conn, unsigned int code,
                                  const char *json) {
    struct MHD_Response *resp = MHD_create_response_from_buffer(
        strlen(json), (void *)json, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json; charset=utf-8");
    enum MHD_Result ret = MHD_queue_response(conn, code, resp);
    MHD_destroy_response(resp);
    return ret;
}

static enum MHD_Result reply_error(struct MHD_Connection *conn, unsigned int code,
                                   const char *msg) {
    cJSON *err = cJSON_CreateObject();
    if (err == NULL)
        return reply_json(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                          "{\"error\":\"oom\"}");
    cJSON_AddStringToObject(err, "error", msg);
    char *body = cJSON_PrintUnformatted(err);
    enum MHD_Result ret = reply_json(conn, code, body);
    cJSON_free(body);
    cJSON_Delete(err);
    return ret;
}

/* ── 路由处理器 ── */

/* GET /api/v1/health */
static enum MHD_Result handle_health(struct MHD_Connection *conn) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
        return reply_json(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                          "{\"error\":\"oom\"}");
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "service", "ai-question-bank");
    char *body = cJSON_PrintUnformatted(root);
    enum MHD_Result ret = reply_json(conn, MHD_HTTP_OK, body);
    cJSON_free(body);
    cJSON_Delete(root);
    return ret;
}

/* POST /api/v1/auth/login */
static enum MHD_Result handle_login(struct MHD_Connection *conn,
                                    const char *body, size_t body_len) {
    (void)body_len;
    if (body == NULL || body[0] == '\0')
        return reply_error(conn, MHD_HTTP_BAD_REQUEST, "请求体为空");

    cJSON *root = cJSON_Parse(body);
    if (root == NULL)
        return reply_error(conn, MHD_HTTP_BAD_REQUEST, "JSON 解析失败");

    char *username = json_get_string(root, "username");
    char *password = json_get_string(root, "password");
    cJSON_Delete(root);

    if (username == NULL || password == NULL) {
        free(username);
        free(password);
        return reply_error(conn, MHD_HTTP_BAD_REQUEST,
                           "缺少 username 或 password");
    }

    /* 查询数据库中存储的 password_hash */
    char *stored_hash = db_query_password_hash(username);
    if (stored_hash == NULL) {
        free(username);
        free(password);
        return reply_error(conn, MHD_HTTP_UNAUTHORIZED, "用户名或密码错误");
    }

    /* 对用户输入的密码做相同哈希计算后比较 */
    const char *salt = "ai-question-bank-demo-salt-2026";
    char *input_hash = hash_password(password, salt);

    int matched = (input_hash != NULL && strcmp(input_hash, stored_hash) == 0);

    free(stored_hash);
    free(input_hash);

    if (!matched) {
        free(username);
        free(password);
        return reply_error(conn, MHD_HTTP_UNAUTHORIZED, "用户名或密码错误");
    }

    /* 查询用户信息用于创建 session */
    char *user_json = db_query_user_by_username(username);
    if (user_json == NULL) {
        free(username);
        free(password);
        return reply_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                           "查询用户信息失败");
    }

    /* 解析 user_json 提取 user_id、role、name */
    cJSON *u_root = cJSON_Parse(user_json);
    int    user_id = 0;
    char  *role = NULL;
    char  *name = NULL;
    if (u_root) {
        cJSON *id_item = cJSON_GetObjectItem(u_root, "id");
        if (id_item && cJSON_IsNumber(id_item)) user_id = id_item->valueint;
        role = json_get_string(u_root, "role");
        name = json_get_string(u_root, "name");
    }

    const char *token = session_create(user_id, role ? role : "", name ? name : "");

    cJSON *resp = cJSON_CreateObject();
    cJSON *user_obj = cJSON_CreateObject();
    if (token && resp && user_obj) {
        cJSON_AddStringToObject(resp, "token", token);
        cJSON_AddNumberToObject(user_obj, "id", user_id);
        cJSON_AddStringToObject(user_obj, "username", username);
        cJSON_AddStringToObject(user_obj, "name", name ? name : "");
        cJSON_AddStringToObject(user_obj, "role", role ? role : "");
        cJSON_AddItemToObject(resp, "user", user_obj);

        char *out = cJSON_PrintUnformatted(resp);
        enum MHD_Result ret = reply_json(conn, MHD_HTTP_OK, out);
        cJSON_free(out);
        cJSON_Delete(resp);
        free(user_json);
        free(username);
        free(password);
        free(role);
        free(name);
        return ret;
    }

    cJSON_Delete(resp);
    free(user_json);
    free(username);
    free(password);
    free(role);
    free(name);
    return reply_error(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "创建会话失败");
}

/* POST /api/v1/auth/logout */
static enum MHD_Result handle_logout(struct MHD_Connection *conn) {
    const char *auth = MHD_lookup_connection_value(conn, MHD_HEADER_KIND,
                                                     "Authorization");
    const char *token = extract_bearer_token(auth);
    if (token != NULL) {
        session_destroy(token);
    }
    return reply_json(conn, MHD_HTTP_OK, "{\"status\":\"ok\"}");
}

/* GET /api/v1/auth/me */
static enum MHD_Result handle_me(struct MHD_Connection *conn) {
    const char *auth = MHD_lookup_connection_value(conn, MHD_HEADER_KIND,
                                                     "Authorization");
    const char *token = extract_bearer_token(auth);
    Session *s = session_validate(token);
    if (s == NULL) {
        return reply_error(conn, MHD_HTTP_UNAUTHORIZED, "未登录或 token 已过期");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON *user_obj = cJSON_CreateObject();
    if (resp == NULL || user_obj == NULL) {
        cJSON_Delete(resp);
        cJSON_Delete(user_obj);
        return reply_json(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                          "{\"error\":\"oom\"}");
    }
    cJSON_AddNumberToObject(user_obj, "id", s->user_id);
    cJSON_AddStringToObject(user_obj, "name", s->name);
    cJSON_AddStringToObject(user_obj, "role", s->role);
    cJSON_AddItemToObject(resp, "user", user_obj);

    char *out = cJSON_PrintUnformatted(resp);
    enum MHD_Result ret = reply_json(conn, MHD_HTTP_OK, out);
    cJSON_free(out);
    cJSON_Delete(resp);
    return ret;
}

/* ── 静态文件服务 ── */

static const char *content_type_for(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext == NULL) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    return "application/octet-stream";
}

static enum MHD_Result serve_static(struct MHD_Connection *conn, const char *url,
                                    const AppConfig *cfg) {
    if (strstr(url, "..") != NULL) {
        return reply_json(conn, MHD_HTTP_FORBIDDEN, "{\"error\":\"forbidden\"}");
    }

    char rel[512];
    const char *p = (*url == '/') ? url + 1 : url;
    if (p[0] == '\0') p = "index.html";
    snprintf(rel, sizeof(rel), "%s", p);
    /* 去掉可能存在的 query 参数 */
    char *q = strchr(rel, '?');
    if (q != NULL) *q = '\0';

    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", cfg->web_root, rel);

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return reply_json(conn, MHD_HTTP_NOT_FOUND, "{\"error\":\"not found\"}");
    }
    long sz;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0) {
        fclose(f);
        return reply_json(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                          "{\"error\":\"read failed\"}");
    }
    rewind(f);
    char *data = malloc((size_t)sz + 1);
    if (data == NULL) {
        fclose(f);
        return reply_json(conn, MHD_HTTP_INTERNAL_SERVER_ERROR,
                          "{\"error\":\"oom\"}");
    }
    size_t rd = fread(data, 1, (size_t)sz, f);
    fclose(f);
    data[rd] = '\0';

    struct MHD_Response *resp = MHD_create_response_from_buffer(rd, data,
                                         MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", content_type_for(path));
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    free(data);
    return ret;
}

/* ── 路由分发入口 ── */

enum MHD_Result route_dispatch(struct MHD_Connection *conn, const char *url,
                               const char *method, const char *body,
                               size_t body_len, const AppConfig *cfg) {
    /* GET /api/v1/health */
    if (strcmp(method, "GET") == 0 &&
        strncmp(url, "/api/v1/health", 14) == 0 &&
        (url[14] == '\0' || url[14] == '?')) {
        return handle_health(conn);
    }

    /* POST /api/v1/auth/login */
    if (strcmp(method, "POST") == 0 &&
        strcmp(url, "/api/v1/auth/login") == 0) {
        return handle_login(conn, body, body_len);
    }

    /* POST /api/v1/auth/logout */
    if (strcmp(method, "POST") == 0 &&
        strcmp(url, "/api/v1/auth/logout") == 0) {
        return handle_logout(conn);
    }

    /* GET /api/v1/auth/me */
    if (strcmp(method, "GET") == 0 &&
        strcmp(url, "/api/v1/auth/me") == 0) {
        return handle_me(conn);
    }

    /* 其他 /api/ 路径 → 404 */
    if (strncmp(url, "/api/", 5) == 0) {
        return reply_json(conn, MHD_HTTP_NOT_FOUND,
                          "{\"error\":\"api not found\"}");
    }

    return serve_static(conn, url, cfg);
}