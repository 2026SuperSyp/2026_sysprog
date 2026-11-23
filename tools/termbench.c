/* 터미널 속도 측정. 이 터미널이 게임 화면(초당 20장)을 따라오는지 본다.
 *   make termbench        (전체 화면, 글자를 줄여 240칸 67줄 이상으로 만든 뒤 실행)
 * 세 가지 그림을 각각 몇 초씩 최대 속도로 그리고, 초당 몇 장을 내보냈는지와 한 장의 크기를 찍는다.
 *   전부 바뀜   모든 칸이 매번 바뀐다(가장 나쁜 경우)
 *   게임 비슷   칸 세 개 안에서 물체 몇 개가 움직인다
 *   거의 안 바뀜 숫자 하나만 바뀐다
 * 도는 동안 마우스를 움직이면 마우스 보고가 오는지도 확인한다. q로 일찍 끝낸다. */
#include "ui/ui_internal.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define OUT_CAP  (1 << 20)
#define SECONDS  4.0
#define N_CASES  3

static double now_sec(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void draw_case(canvas_t *c, int which, int frame)
{
    char buf[32];
    if (which == 0) {
        /* 모든 픽셀의 색을 매번 바꾼다 */
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++)
                ui_px(c, x, y, (color_t)(1 + (x / 3 + y / 3 + frame) % 23));
    } else if (which == 1) {
        ui_clear(c, C_BG);
        for (int pane = 0; pane < 3; pane++) {
            int ox = 1 + pane * 80;
            ui_rect(c, ox, 14, 78, 108, C_GRAY1);
            for (int k = 0; k < 6; k++) {
                int x = ox + (frame * (k + 2) + k * 13) % 66, y = 14 + (frame * (k + 1) * 2 + k * 17) % 96;
                ui_rect(c, x, y, 12, 12, (color_t)(C_RED + k));
            }
        }
        snprintf(buf, sizeof buf, "TIME %d", frame % 100);
        ui_text(c, 3, 3, buf, C_TEXT);
    } else {
        if (frame == 0) ui_clear(c, C_BG);
        ui_rect(c, 100, 60, 60, 10, C_BG);
        snprintf(buf, sizeof buf, "%d", frame);
        ui_text(c, 100, 60, buf, C_YELLOW);
    }
}

int main(void)
{
    static const char *const names[N_CASES] = { "전부 바뀜", "게임 비슷", "거의 안 바뀜" };
    double fps[N_CASES] = {0}, bytes[N_CASES] = {0};
    int cols = 0, rows = 0, mouse_events = 0, stop = 0;

    if (term_enter() != 0) {
        printf("터미널에서 실행해 주세요 (make termbench)\n");
        return 2;
    }
    term_size(&cols, &rows);
    canvas_t *full = canvas_new(SCREEN_W, SCREEN_H);
    present_state_t *ps = present_new();
    char *out = malloc(OUT_CAP);
    uint8_t in[256];
    size_t in_len = 0;

    for (int which = 0; which < N_CASES && !stop && cols >= SCREEN_W && rows >= SCREEN_H / 2; which++) {
        double t0 = now_sec(), total = 0.0;
        int frames = 0;
        present_invalidate(ps);
        while (now_sec() - t0 < SECONDS && !stop) {
            draw_case(full, which, frames);
            size_t n = present_encode(ps, full, cols, rows, out, OUT_CAP);
            for (size_t off = 0; off < n; ) {
                ssize_t w = write(STDOUT_FILENO, out + off, n - off);
                if (w <= 0) { stop = 1; break; }
                off += (size_t)w;
            }
            tcdrain(STDOUT_FILENO);              /* 터미널이 다 받아 갈 때까지 기다린다 */
            total += (double)n;
            frames++;

            /* 그 사이에 들어온 키와 마우스 */
            struct pollfd pf = { STDIN_FILENO, POLLIN, 0 };
            if (poll(&pf, 1, 0) > 0) {
                ssize_t r = read(STDIN_FILENO, in + in_len, sizeof in - in_len);
                if (r > 0) in_len += (size_t)r;
            }
            in_event_t ev;
            size_t used;
            while (in_len > 0 && input_parse(in, in_len, &ev, &used)) {
                memmove(in, in + used, in_len - used);
                in_len -= used;
                if (ev.kind == IN_POINTER) mouse_events++;
                if (ev.kind == IN_KEY && (ev.code == 'q' || ev.code == 3)) stop = 1;
            }
            if (in_len == sizeof in) in_len = 0;
        }
        double dt = now_sec() - t0;
        fps[which] = frames / (dt > 0 ? dt : 1);
        bytes[which] = frames ? total / frames : 0;
    }

    term_leave();
    canvas_free(full);
    present_free(ps);
    free(out);

    printf("터미널 크기: %d칸 %d줄\n", cols, rows);
    if (cols < SCREEN_W || rows < SCREEN_H / 2) {
        printf("240칸 67줄 이상이 필요합니다. 전체 화면으로 하고 Ctrl 과 - 로 글자를 줄인 뒤 다시 실행하세요.\n");
        return 1;
    }
    printf("%-14s %10s %14s  %s\n", "그림", "초당 장수", "한 장(바이트)", "초당 20장");
    for (int i = 0; i < N_CASES; i++)
        printf("%-14s %10.1f %14.0f  %s\n", names[i], fps[i], bytes[i],
               fps[i] == 0 ? "-" : (fps[i] >= 20.0 ? "충분" : "모자람"));
    printf("마우스 보고: %s (%d번)\n", mouse_events > 0 ? "들어옴" : "안 들어옴(도는 동안 마우스를 움직여야 잡힙니다)", mouse_events);
    printf("게임에서는 '게임 비슷'이 평소이고, 화면이 바뀌는 순간에만 '전부 바뀜'에 가까워집니다.\n");
    return 0;
}
