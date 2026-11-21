/* 작은 검사 도구. 검사 파일 하나가 실행 파일 하나다.
 *   T_CHECK(조건);  T_EQ_INT(a, b);  main 끝에서 T_DONE(); */
#ifndef TESTS_T_H
#define TESTS_T_H

#include <stdio.h>

static int t_fail;

#define T_CHECK(cond) \
    do { \
        if (!(cond)) { \
            t_fail++; \
            printf("  실패 %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define T_EQ_INT(a, b) \
    do { \
        long t_a = (long)(a), t_b = (long)(b); \
        if (t_a != t_b) { \
            t_fail++; \
            printf("  실패 %s:%d: %s == %s (%ld != %ld)\n", __FILE__, __LINE__, #a, #b, t_a, t_b); \
        } \
    } while (0)

#define T_DONE() return t_fail ? 1 : 0

#endif
