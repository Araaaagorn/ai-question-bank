#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <microhttpd.h>

#include "cJSON.h"
#include "config.h"
#include "routes.h"

/* 输出 JSON 响应 */
static enum MHD_Result reply_json(struct MHD_Connection *conn, unsigned int code,
                                  const char *json) {
    struct MHD_Response *resp = MHD_create_response_from_buffer(
        strlen(json), (void *)json, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json; charset=utf-8");
    enum MHD_Result ret = MHD_queue_response(conn, code, resp);
    MHD_destroy_response(resp);
    return ret;
}

/* GET /api/v1/health */
static enum MHD_Result handle_health(struct MHD_Connection *conn) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return reply_json(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "{\"error\":\"oom\"}");
    }
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "service", "ai-question-bank");
    char *body = cJSON_PrintUnformatted(root);
    enum MHD_Result ret = reply_json(conn, MHD_HTTP_OK, body);
    cJSON_free(body);
    cJSON_Delete(root);
    return ret;
}

static const char *content_type_for(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext == NULL) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    return "application/octet-stream";
}

/* 静态文件服务（web_root + 请求路径）。防路径穿越：拒绝包含 ".." 的路径。 */
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
        return reply_json(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "{\"error\":\"read failed\"}");
    }
    rewind(f);
    char *data = malloc((size_t)sz + 1);
    if (data == NULL) {
        fclose(f);
        return reply_json(conn, MHD_HTTP_INTERNAL_SERVER_ERROR, "{\"error\":\"oom\"}");
    }
    size_t rd = fread(data, 1, (size_t)sz, f);
    fclose(f);
    data[rd] = '\0';

    struct MHD_Response *resp = MHD_create_response_from_buffer(rd, data, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", content_type_for(path));
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    free(data);
    return ret;
}

enum MHD_Result route_dispatch(struct MHD_Connection *conn, const char *url,
                               const char *method, const AppConfig *cfg) {
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/v1/health", 14) == 0 &&
        (url[14] == '\0' || url[14] == '?')) {
        return handle_health(conn);
    }
    if (strncmp(url, "/api/", 5) == 0) {
        return reply_json(conn, MHD_HTTP_NOT_FOUND, "{\"error\":\"api not found\"}");
    }
    return serve_static(conn, url, cfg);
}
