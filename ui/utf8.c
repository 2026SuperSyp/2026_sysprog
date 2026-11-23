/* UTF-8 글자열 다루기. 글자 그리기(font.c)와 메시지에 글자열을 담는 곳이 같이 쓴다. */
#include "ui/ui_internal.h"

#include <string.h>

/* 글자 하나를 읽고 다음 글자로 넘어간다. 끝이면 0.
 * 틀린 바이트 묶음(잘린 글자 포함)은 U+FFFD 하나로 읽는다. 글자열의 끝을 넘어가지 않는다. */
int utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int c = p[0], more, cp, min;

    if (c == 0) return 0;
    if (c < 0x80) { *s += 1; return c; }
    if (c >= 0xC2 && c <= 0xDF)      { more = 1; cp = c & 0x1F; min = 0x80; }
    else if (c >= 0xE0 && c <= 0xEF) { more = 2; cp = c & 0x0F; min = 0x800; }
    else if (c >= 0xF0 && c <= 0xF4) { more = 3; cp = c & 0x07; min = 0x10000; }
    else { *s += 1; return 0xFFFD; }

    for (int i = 1; i <= more; i++) {
        if ((p[i] & 0xC0) != 0x80) {       /* 끝(0)도 여기서 걸린다 */
            *s += i;
            return 0xFFFD;
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += more + 1;
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
    return cp;
}

/* src를 cap바이트짜리 dst에 담는다. 다 들어가지 않으면 글자 경계에서 자른다
 * (한글 한 글자는 3바이트라서 바이트 수로만 자르면 마지막 글자가 깨진다).
 * 끝에는 항상 0을 붙인다. 담은 바이트 수를 돌려준다. */
size_t utf8_copy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return 0;
    size_t n = src ? strlen(src) : 0;
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    if (n > 0) memcpy(dst, src, n);
    dst[n] = '\0';
    return n;
}
