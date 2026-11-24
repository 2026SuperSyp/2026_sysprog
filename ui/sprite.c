/* 그림 파일(.spr) 읽기와 그리기.
 *
 *   ; 주석
 *   size 8 6
 *   frames 2          (없어도 된다. 없으면 1)
 *   palette . C_NONE  b C_FLY_BODY
 *   ........          (픽셀 줄. 프레임 수 x 높이만큼)
 *
 * 찾는 순서: 지금 스테이지 폴더의 assets/, screens/assets/, ui/assets/ */
#include "ui/ui_internal.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SPRITES 64
#define MAX_DIM     128

typedef struct {
    char name[40];
    int w, h, frames;
    color_t *px;               /* frames * h * w */
} sprite_t;

static sprite_t sprites[MAX_SPRITES];
static int n_sprites;          /* 0번은 SPR_MISSING */
static char stage_dir[200];

static const char *color_names[] = {
    "C_NONE", "C_BG", "C_TEXT", "C_FLY_BODY", "C_FLY_EYE", "C_WING", "C_WEB",
    "C_GOOD", "C_BAD", "C_WARN", "C_WHITE", "C_BLACK", "C_GRAY1", "C_GRAY2", "C_GRAY3",
    "C_RED", "C_ORANGE", "C_YELLOW", "C_GREEN", "C_CYAN", "C_BLUE", "C_PURPLE", "C_PINK", "C_BROWN",
};

static int color_by_name(const char *name)
{
    for (unsigned i = 0; i < sizeof color_names / sizeof color_names[0]; i++)
        if (strcmp(color_names[i], name) == 0) return (int)i;
    return -1;
}

void ui_set_stage_dir(const char *dir)
{
    snprintf(stage_dir, sizeof stage_dir, "%s", dir ? dir : "");
}

/* 눈에 띄는 대체 그림(분홍 바둑판)을 0번에 만들어 둔다. */
static void ensure_missing(void)
{
    if (n_sprites > 0) return;
    sprite_t *s = &sprites[0];
    snprintf(s->name, sizeof s->name, "(missing)");
    s->w = s->h = 8;
    s->frames = 1;
    s->px = malloc(64);
    for (int i = 0; i < 64; i++)
        s->px[i] = (((i % 8) / 2 + (i / 8) / 2) % 2) ? C_PINK : C_BLACK;
    n_sprites = 1;
}

static FILE *open_sprite(const char *name, char *path, size_t cap)
{
    const char *dirs[3] = { stage_dir[0] ? stage_dir : NULL, "screens", "ui" };
    for (int i = 0; i < 3; i++) {
        if (!dirs[i]) continue;
        snprintf(path, cap, "%s/assets/%s.spr", dirs[i], name);
        FILE *f = fopen(path, "r");
        if (f) return f;
    }
    snprintf(path, cap, "assets/%s.spr", name);
    return NULL;
}

/* 파일 하나를 읽는다. 성공하면 1. 틀리면 이유를 경고로 남기고 0. */
static int parse_sprite(FILE *f, const char *path, sprite_t *out)
{
    char line[512];
    int lineno = 0, w = 0, h = 0, frames = 1, have_palette = 0, row = 0;
    int map[256];
    for (int i = 0; i < 256; i++) map[i] = -1;
    color_t *px = NULL;

    while (fgets(line, sizeof line, f)) {
        lineno++;
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0 || line[0] == ';') continue;

        if (!have_palette) {
            char word[32];
            if (sscanf(line, "%31s", word) != 1) continue;
            if (strcmp(word, "size") == 0) {
                if (sscanf(line, "size %d %d", &w, &h) != 2 || w < 1 || h < 1 || w > MAX_DIM || h > MAX_DIM) {
                    log_warn_once(path, "%s:%d: size가 틀렸습니다", path, lineno);
                    return 0;
                }
            } else if (strcmp(word, "frames") == 0) {
                if (sscanf(line, "frames %d", &frames) != 1 || frames < 1 || frames > 32) {
                    log_warn_once(path, "%s:%d: frames가 틀렸습니다", path, lineno);
                    return 0;
                }
            } else if (strcmp(word, "palette") == 0) {
                char *tok = strtok(line + 7, " \t");
                while (tok) {
                    char ch = tok[0];
                    char *name = strtok(NULL, " \t");
                    int col = name ? color_by_name(name) : -1;
                    if (tok[1] != '\0' || col < 0) {
                        log_warn_once(path, "%s:%d: 모르는 색 이름 '%s'", path, lineno, name ? name : "");
                        return 0;
                    }
                    map[(unsigned char)ch] = col;
                    tok = strtok(NULL, " \t");
                }
                if (w == 0) {
                    log_warn_once(path, "%s:%d: palette 앞에 size가 있어야 합니다", path, lineno);
                    return 0;
                }
                have_palette = 1;
                px = malloc((size_t)w * h * frames);
            } else {
                log_warn_once(path, "%s:%d: 모르는 줄 '%s'", path, lineno, word);
                return 0;
            }
            continue;
        }

        /* 픽셀 줄 */
        if (row >= h * frames) {
            log_warn_once(path, "%s:%d: 픽셀 줄이 남습니다", path, lineno);
            free(px);
            return 0;
        }
        if ((int)len != w) {
            log_warn_once(path, "%s:%d: 줄 길이가 %d이 아닙니다", path, lineno, w);
            free(px);
            return 0;
        }
        for (int x = 0; x < w; x++) {
            int col = map[(unsigned char)line[x]];
            if (col < 0) {
                log_warn_once(path, "%s:%d: palette에 없는 글자 '%c'", path, lineno, line[x]);
                free(px);
                return 0;
            }
            px[row * w + x] = (color_t)col;
        }
        row++;
    }
    if (!have_palette || row != h * frames) {
        log_warn_once(path, "%s: 픽셀 줄이 모자랍니다(%d줄 필요, %d줄 있음)", path, h * frames, row);
        free(px);
        return 0;
    }
    out->w = w;
    out->h = h;
    out->frames = frames;
    out->px = px;
    return 1;
}

static void ensure_core(void);

sprite_id_t ui_sprite_load(const char *name)
{
    ensure_core();             /* 초파리 그림 네 장이 1~4번을 먼저 차지하게 한다 */
    if (!name) return SPR_MISSING;
    for (int i = 1; i < n_sprites; i++)
        if (strcmp(sprites[i].name, name) == 0) return i;
    if (n_sprites >= MAX_SPRITES) {
        log_warn_once("spr-full", "그림이 너무 많습니다(최대 %d개)", MAX_SPRITES - 1);
        return SPR_MISSING;
    }

    char path[300];
    FILE *f = open_sprite(name, path, sizeof path);
    if (!f) {
        log_warn_once(path, "그림 파일이 없습니다: %s", path);
        return SPR_MISSING;
    }
    sprite_t *s = &sprites[n_sprites];
    int ok = parse_sprite(f, path, s);
    fclose(f);
    if (!ok) return SPR_MISSING;
    snprintf(s->name, sizeof s->name, "%s", name);
    return n_sprites++;
}

/* 코어가 주는 초파리 그림 네 장은 처음 쓸 때 읽는다. 번호는 ui.h의 SPR_FLY_* 순서다. */
static void ensure_core(void)
{
    static int done;
    if (done) return;
    done = 1;
    ensure_missing();
    static const char *core[] = { "fly_idle", "fly_eat", "fly_jump", "fly_hit" };
    for (int i = 0; i < 4; i++) {
        sprite_id_t id = ui_sprite_load(core[i]);
        if (id != SPR_FLY_IDLE + i) {
            /* 못 읽었으면 그 자리를 대체 그림으로 채워 번호가 밀리지 않게 한다. */
            sprites[n_sprites] = sprites[0];
            snprintf(sprites[n_sprites].name, sizeof sprites[0].name, "%s", core[i]);
            n_sprites++;
        }
    }
}

static const sprite_t *get(sprite_id_t id)
{
    ensure_core();
    if (id < 0 || id >= n_sprites) return &sprites[0];
    return &sprites[id];
}

int ui_sprite_w(sprite_id_t id)      { return get(id)->w; }
int ui_sprite_h(sprite_id_t id)      { return get(id)->h; }
int ui_sprite_frames(sprite_id_t id) { return get(id)->frames; }

void ui_sprite(canvas_t *c, sprite_id_t id, int x, int y, int frame)
{
    const sprite_t *s = get(id);
    if (!c) return;
    frame %= s->frames;
    if (frame < 0) frame += s->frames;
    /* 화면을 픽셀 그림으로 보낼 때 진행자 시프봇은 같은 그림을 실제 해상도로 매끈하게 키워 그린다(ui/hd_art.c).
     * 여기서는 자리만 알려 둔다 */
    if (strncmp(s->name, "sipbot", 6) == 0 && ui_capture_note(c, UI_HD_SPRITE, x, y, s->w, s->h, frame, s->name)) return;
    const color_t *px = s->px + (size_t)frame * s->w * s->h;
    for (int yy = 0; yy < s->h; yy++)
        for (int xx = 0; xx < s->w; xx++)
            ui_px(c, x + xx, y + yy, px[yy * s->w + xx]);
}

const color_t *ui_sprite_pixels(sprite_id_t id, int frame, int *w, int *h)
{
    const sprite_t *s = get(id);
    if (!s->px || s->frames < 1) return NULL;
    frame %= s->frames;
    if (frame < 0) frame += s->frames;
    if (w) *w = s->w;
    if (h) *h = s->h;
    return s->px + (size_t)frame * s->w * s->h;
}
