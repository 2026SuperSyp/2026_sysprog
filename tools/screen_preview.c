/* 화면 미리보기: 화면 하나를 예시 상태로 띄운다. 아무 키나 누르면 끝난다.
 *   make screen SCREEN=lobby|result|ranking|spectate [CASE=1..3] */
#include "screen.h"
#include "ui/ui_internal.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void fill_lobby(lobby_state_t *s, int c)
{
    static const char *names[] = { "HENRY", "YEONWOO", "GOEUN", "ABCDEFGHIJKLMNO" };
    s->n = c == 1 ? 1 : (c == 2 ? 3 : 4);
    for (int i = 0; i < s->n; i++) {
        snprintf(s->p[i].name, sizeof s->p[i].name, "%s", names[i]);
        s->p[i].ready = (i + c) % 2;
        s->p[i].ping_ms = 12 + i * 20;
        s->p[i].is_host = i == 0;
    }
    s->me = c == 3 ? -1 : 0;
}

static void fill_result(round_result_t *r, int c)
{
    static const char *names[] = { "HENRY", "YEONWOO", "ABCDEFGHIJKLMNO" };
    r->n = c == 1 ? 1 : 3;
    r->fly_score = 70;
    r->stage_name = "DODGE";
    for (int i = 0; i < r->n; i++) {
        snprintf(r->name[i], sizeof r->name[i], "%s", names[i]);
        r->score[i] = (float)(100 - i * 40 - (c == 3 ? 50 : 0));
        r->passed[i] = r->score[i] >= r->fly_score;
        r->lives[i] = r->passed[i] ? 3 : 2 - i % 2;
        r->next[i] = r->lives[i] > 0 ? NEXT_ROUND : NEXT_SPECTATE;
    }
    r->me = 0;
}

static void fill_ranking(ranking_t *r, int c)
{
    r->n = c == 1 ? 1 : (c == 2 ? 5 : 12);
    for (int i = 0; i < r->n; i++) {
        snprintf(r->name[i], sizeof r->name[i], "PLAYER%d", (i + 1) % 100);
        r->total[i] = 400 - i * 25;
        snprintf(r->best_stage[i], sizeof r->best_stage[i], "%s", i % 2 ? "DODGE" : "WEB MAZE");
    }
    r->me = c == 3 ? -1 : r->n - 1;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *name = "";
    int c = 1;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--screen=", 9) == 0) name = argv[i] + 9;
        else if (strncmp(argv[i], "--case=", 7) == 0) c = atoi(argv[i] + 7);
    }
    int which = strcmp(name, "lobby") == 0 ? 0 : strcmp(name, "result") == 0 ? 1 :
                strcmp(name, "ranking") == 0 ? 2 : strcmp(name, "spectate") == 0 ? 3 : -1;
    if (which < 0) {
        printf("SCREEN=lobby|result|ranking|spectate 중 하나를 주세요\n");
        return 2;
    }
    if (c < 1 || c > 3) c = 1;

    lobby_state_t lobby = {0};
    round_result_t result = {0};
    ranking_t ranking = {0};
    spectate_state_t spec = {0};
    fill_lobby(&lobby, c);
    fill_result(&result, c);
    fill_ranking(&ranking, c);
    snprintf(spec.watching, sizeof spec.watching, "%s", c == 3 ? "ABCDEFGHIJKLMNO" : "YEONWOO");

    if (term_enter() != 0) {
        printf("터미널에서 실행해 주세요\n");
        return 2;
    }
    canvas_t *full = canvas_new(SCREEN_W, SCREEN_H);
    present_state_t *ps = present_new();
    char *out = malloc(1 << 20);
    int shown_small = 0;

    for (int tick = 0;; tick++) {
        double t = tick * 0.05;
        lobby.t = result.t = ranking.t = spec.t = t;
        ui_clear(full, C_BG);
        switch (which) {
        case 0: screen_lobby(full, &lobby); break;
        case 1: screen_result(full, &result); break;
        case 2: screen_ranking(full, &ranking); break;
        case 3: screen_result(full, &result); screen_spectate(full, &spec); break;
        }
        int cols = 0, rows = 0;
        term_size(&cols, &rows);
        size_t n = present_encode(ps, full, cols, rows, out, 1 << 20);
        if (n > 0) {
            ssize_t w = write(STDOUT_FILENO, out, n);
            (void)w;
            shown_small = 0;
        } else if ((cols < SCREEN_W || rows < SCREEN_H / 2) && !shown_small) {
            char msg[200];
            int m = snprintf(msg, sizeof msg, "\033[0m\033[2J\033[H지금 %d칸 %d줄입니다. 240칸 67줄 이상이 필요합니다.\r\n", cols, rows);
            ssize_t w = write(STDOUT_FILENO, msg, (size_t)m);
            (void)w;
            shown_small = 1;
        }
        struct pollfd pf = { STDIN_FILENO, POLLIN, 0 };
        if (poll(&pf, 1, 50) > 0) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) == 1 && ch != 0x1B) break;
        }
    }
    term_leave();
    canvas_free(full);
    present_free(ps);
    free(out);
    return 0;
}
