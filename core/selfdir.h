/* selfdir.h : 지금 도는 실행 파일이 있는 폴더
 *
 * 서버는 스테이지와 뇌 실행 파일을 자기 옆에서 찾는다. 그래서 build/dev/server는 build/dev 의 것을,
 * build/release/server는 build/release 의 것을 띄운다. 로그와 자료의 경로(build/logs, data)는
 * 전처럼 현재 폴더 기준이다. */
#ifndef SSP_SELFDIR_H
#define SSP_SELFDIR_H

#define SELFDIR_MAX 4096

/* 실행 파일이 있는 폴더. 끝에 '/'는 없다. 현재 폴더 아래에 있으면 상대 경로다(예: build/dev).
 * 처음 부를 때 한 번만 알아내고 그 뒤로는 같은 글자열을 돌려준다. 여러 스레드가 불러도 된다.
 * 알아내지 못하면 build/dev 를 돌려준다. */
const char *selfdir(void);

#endif
