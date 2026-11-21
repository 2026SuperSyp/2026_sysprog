/* log.h : 경고와 기록
 *
 * 경고는 표준 오류로 나간다. 실행기가 표준 오류를 로그 파일로 돌려 둔다.
 * 경고가 하나라도 있으면 make bot이 실패로 본다. */
#ifndef SSP_LOG_H
#define SSP_LOG_H

/* 같은 key로는 한 번만 남긴다. 매 틱 불리는 곳에서 써도 로그가 넘치지 않는다. */
void log_warn_once(const char *key, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* 지금까지 남긴 경고의 수(같은 key는 한 번으로 센다) */
int  log_warning_count(void);

#endif
