#!/bin/sh
# 스테이지 코드가 쓰면 안 되는 함수를 부르는지 본다. 스테이지를 빌드할 때 Makefile이 부른다.
#   sh tools/stage_check.sh <스테이지 이름> <목적 파일 ...>
# 스테이지는 같은 씨앗이면 같은 판이 되어야 하고(include/stage.h의 규칙 9), 시간은 틱으로만 센다.
# 그래서 난수, 시계, 잠자기, 프로세스나 스레드나 소켓을 만드는 함수를 쓰지 못한다.
name=$1
shift
bad=""
for o in "$@"; do
    [ -f "$o" ] || continue
    for f in $(nm -u "$o" 2>/dev/null | awk '{ print $NF }' |
               grep -x -E 'rand|srand|random|srandom|time|clock_gettime|gettimeofday|sleep|usleep|nanosleep|fork|socket|pthread_create'); do
        bad="$bad $(basename "$o" .o).c:$f"
    done
done
[ -z "$bad" ] && exit 0
echo "stages/$name: 스테이지에서 쓰면 안 되는 함수를 부릅니다:$bad"
echo "  난수는 stage_rng_*(ctx->seed에서 씨앗을 만든다), 시간은 틱 수로 셉니다. 자세한 것은 include/stage.h의 규칙을 보세요."
exit 1
