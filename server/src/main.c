#include <stdio.h>

#include "config.h"
#include "db.h"
#include "http_server.h"

int main(void) {
    AppConfig cfg = config_load();

    int rc = db_init(cfg.db_path);
    if (rc != 0) {
        fprintf(stderr, "✘ 数据库初始化失败（%s）\n", cfg.db_path);
        return 1;
    }
    printf("✔ 数据库就绪: %s\n", cfg.db_path);

    return http_server_start(&cfg);
}
