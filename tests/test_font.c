/* 글자: UTF-8 읽기, 글자 경계에서 자르는 복사, 한글 글꼴.
 * 저장소 맨 위 폴더에서 실행한다(ui/assets/hangul12.fnt를 읽는다). */
#include "ui/ui_internal.h"
#include "core/log.h"
#include "protocol.h"
#include "t.h"

#include <string.h>

static int ink(const canvas_t *c, int x0, int y0, int x1, int y1)   /* [x0, x1) x [y0, y1)에서 켜진 픽셀 수 */
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (c->px[y * c->w + x] != C_BG) n++;
    return n;
}

static void test_utf8_next(void)
{
    const char *s = "A가←";
    T_EQ_INT(utf8_next(&s), 'A');
    T_EQ_INT(utf8_next(&s), 0xAC00);
    T_EQ_INT(utf8_next(&s), 0x2190);
    T_EQ_INT(utf8_next(&s), 0);
    T_EQ_INT(utf8_next(&s), 0);                    /* 끝에서는 더 나아가지 않는다 */

    s = "\xF0\x9F\x98\x80";                        /* 4바이트 글자 */
    T_EQ_INT(utf8_next(&s), 0x1F600);
    T_EQ_INT(utf8_next(&s), 0);

    s = "\xEA\xB0";                                /* 잘린 한글: 한 묶음이 U+FFFD 하나 */
    T_EQ_INT(utf8_next(&s), 0xFFFD);
    T_EQ_INT(utf8_next(&s), 0);

    s = "\xEA\xB0" "A";
    T_EQ_INT(utf8_next(&s), 0xFFFD);
    T_EQ_INT(utf8_next(&s), 'A');
    T_EQ_INT(utf8_next(&s), 0);

    s = "\xFF\x80";                                /* 있을 수 없는 바이트, 짝 없는 뒷바이트 */
    T_EQ_INT(utf8_next(&s), 0xFFFD);
    T_EQ_INT(utf8_next(&s), 0xFFFD);
    T_EQ_INT(utf8_next(&s), 0);

    s = "\xC0\xAF";                                /* 길게 늘여 쓴 '/' */
    T_EQ_INT(utf8_next(&s), 0xFFFD);
    T_EQ_INT(utf8_next(&s), 0xFFFD);
    T_EQ_INT(utf8_next(&s), 0);

    s = "\xED\xA0\x80" "B";                        /* 대리 코드 */
    T_EQ_INT(utf8_next(&s), 0xFFFD);
    T_EQ_INT(utf8_next(&s), 'B');
}

static void test_utf8_copy(void)
{
    char buf[8];
    T_EQ_INT(utf8_copy(buf, sizeof buf, "가나다"), 6);       /* 9바이트 가운데 7이 아니라 6바이트만 */
    T_CHECK(strcmp(buf, "가나") == 0);
    T_EQ_INT(utf8_copy(buf, 5, "가나"), 3);
    T_CHECK(strcmp(buf, "가") == 0);
    T_EQ_INT(utf8_copy(buf, 4, "가나"), 3);
    T_EQ_INT(utf8_copy(buf, 3, "가"), 0);
    T_CHECK(buf[0] == '\0');
    T_EQ_INT(utf8_copy(buf, sizeof buf, "ABCDEFGHIJ"), 7);
    T_CHECK(strcmp(buf, "ABCDEFG") == 0);
    T_EQ_INT(utf8_copy(buf, sizeof buf, "AB가"), 5);
    T_EQ_INT(utf8_copy(buf, sizeof buf, "ABCDEF가"), 6);     /* 마지막 글자가 통째로 빠진다 */
    T_CHECK(strcmp(buf, "ABCDEF") == 0);
    T_EQ_INT(utf8_copy(buf, sizeof buf, NULL), 0);
    T_EQ_INT(utf8_copy(buf, 1, "A"), 0);
    T_EQ_INT(utf8_copy(buf, 0, "A"), 0);
    T_EQ_INT(utf8_copy(NULL, 8, "A"), 0);

    /* 스테이지 안내가 실리는 칸. 제목 32바이트에는 한글 10자까지 들어간다 */
    info_msg_t info;
    T_EQ_INT(utf8_copy(info.title, sizeof info.title, "가나다라마바사아자차카타"), 30);
    T_CHECK(strcmp(info.title, "가나다라마바사아자차") == 0);
    T_EQ_INT(utf8_copy(info.title, sizeof info.title, "거미줄 미로"), 16);
}

static void test_size(void)
{
    T_EQ_INT(ui_text_w("ABC"), 3 * UI_FONT_W);
    T_EQ_INT(ui_text_w("돌 피하기"), 4 * UI_KFONT_W + UI_FONT_W);
    T_EQ_INT(ui_text_w("← →"), 2 * UI_KFONT_W + UI_FONT_W);
    T_EQ_INT(ui_text_w("\xEA\xB0"), UI_KFONT_W);
    T_EQ_INT(ui_text_w(""), 0);
    T_EQ_INT(ui_text_w(NULL), 0);
    T_EQ_INT(ui_text_h("ABC 123"), UI_FONT_H);
    T_EQ_INT(ui_text_h("A가"), UI_KFONT_H);
    T_EQ_INT(ui_text_h(NULL), UI_FONT_H);
}

static void test_draw(void)
{
    canvas_t *a = canvas_new(40, 16), *b = canvas_new(40, 16);

    ui_clear(a, C_BG);
    ui_text(a, 0, 0, "가", C_WHITE);
    T_EQ_INT(log_warning_count(), 0);              /* 글꼴 파일을 경고 없이 읽었다 */
    T_CHECK(ink(a, 0, 0, 40, 16) >= 10);
    T_EQ_INT(ink(a, 0, 0, 40, 16), ink(a, 0, 0, UI_KFONT_W, UI_KFONT_H));    /* 자기 칸 밖에는 그리지 않는다 */

    ui_clear(b, C_BG);
    ui_text(b, 0, 0, "각", C_WHITE);
    T_CHECK(memcmp(a->px, b->px, 40 * 16) != 0);   /* 다른 글자는 다른 그림 */

    /* 음절 끝, 자모, 화살표도 들어 있다 */
    static const char *const some[] = { "힣", "ㄱ", "ㅣ", "←", "↓", "…" };
    for (unsigned i = 0; i < sizeof some / sizeof some[0]; i++) {
        ui_clear(a, C_BG);
        ui_text(a, 0, 0, some[i], C_WHITE);
        T_CHECK(ink(a, 0, 0, UI_KFONT_W, UI_KFONT_H) > 0);
    }

    /* 글꼴에 없는 글자와 깨진 바이트는 빈칸이고, 그 뒤 글자는 한 칸 뒤에 그려진다 */
    ui_clear(a, C_BG);
    ui_text(a, 0, 0, "漢가", C_WHITE);
    T_EQ_INT(ink(a, 0, 0, UI_KFONT_W, 16), 0);
    T_CHECK(ink(a, UI_KFONT_W, 0, 2 * UI_KFONT_W, 16) >= 10);
    ui_clear(a, C_BG);
    ui_text(a, 0, 0, "\xEA\xB0", C_WHITE);
    T_EQ_INT(ink(a, 0, 0, 40, 16), 0);

    /* 영문만 있으면 예전 자리에 그린다. 한글이 섞이면 영문을 UI_KFONT_LATIN_DY만큼 내린다 */
    ui_clear(a, C_BG);
    ui_text(a, 0, 0, "I", C_WHITE);
    T_CHECK(ink(a, 0, 0, UI_FONT_W, 1) > 0);
    T_EQ_INT(ink(a, 0, 7, UI_FONT_W, 16), 0);
    ui_clear(a, C_BG);
    ui_text(a, 0, 0, "I가", C_WHITE);
    T_EQ_INT(ink(a, 0, 0, UI_FONT_W, UI_KFONT_LATIN_DY), 0);
    T_CHECK(ink(a, 0, UI_KFONT_LATIN_DY, UI_FONT_W, UI_KFONT_LATIN_DY + 1) > 0);
    T_EQ_INT(ink(a, 0, UI_KFONT_LATIN_DY + 7, UI_FONT_W, 16), 0);

    /* 캔버스 밖과 빈 인자 */
    ui_text(a, -100, -100, "가나다 ABC", C_WHITE);
    ui_text(a, 35, 10, "가나다 ABC", C_WHITE);
    ui_text(a, 100000, 100000, "가", C_WHITE);
    ui_text(NULL, 0, 0, "가", C_WHITE);
    ui_text(a, 0, 0, NULL, C_WHITE);

    canvas_free(a);
    canvas_free(b);
}

int main(void)
{
    test_utf8_next();
    test_utf8_copy();
    test_size();
    test_draw();
    T_DONE();
}
