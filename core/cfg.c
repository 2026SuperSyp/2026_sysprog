/* 설정 파일(stages/이름/config.ini) 읽기. 한 줄에 "이름 = 숫자", ';' 뒤는 주석이다. */
#include "core/cfg.h"
#include "core/log.h"
#include "stage.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_KEYS 128

static struct { char key[48]; float val; } table[MAX_KEYS];
static int n_keys;
static char loaded_path[256];

void cfg_load(const char *path)
{
    n_keys = 0;
    snprintf(loaded_path, sizeof loaded_path, "%s", path);
    FILE *f = fopen(path, "r");
    if (!f) {
        log_warn_once(path, "설정 파일이 없습니다: %s", path);
        return;
    }
    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *semi = strchr(line, ';');
        if (semi) *semi = '\0';
        char key[48];
        float val;
        char extra;
        int got = sscanf(line, " %47[a-z0-9_] = %f %c", key, &val, &extra);
        if (got == 2) {
            if (n_keys < MAX_KEYS) {
                snprintf(table[n_keys].key, sizeof table[0].key, "%s", key);
                table[n_keys].val = val;
                n_keys++;
            }
            continue;
        }
        /* 빈 줄(공백, CR만 있는 줄)은 넘어간다 */
        int blank = 1;
        for (char *p = line; *p; p++)
            if (!isspace((unsigned char)*p)) blank = 0;
        if (!blank) {
            char where[300];
            snprintf(where, sizeof where, "%s:%d", path, lineno);
            log_warn_once(where, "%s: 읽을 수 없는 줄입니다. \"이름 = 숫자\" 꼴로 써 주세요", where);
        }
    }
    fclose(f);
}

float cfg_get(const char *key, float fallback)
{
    for (int i = 0; i < n_keys; i++)
        if (strcmp(table[i].key, key) == 0) return table[i].val;
    char where[320];
    snprintf(where, sizeof where, "cfg:%s", key);
    log_warn_once(where, "%s에 '%s'가 없어서 기본값 %g을 씁니다", loaded_path, key, (double)fallback);
    return fallback;
}
