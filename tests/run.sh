#!/bin/sh
# 검사 실행 파일들을 하나씩 돌리고 결과를 모은다.
#   sh tests/run.sh build/dev/tests/test_a build/dev/tests/test_b ...
pass=0
fail=0
for t in "$@"; do
    if "$t"; then
        pass=$((pass + 1))
        echo "통과 $(basename "$t")"
    else
        fail=$((fail + 1))
        echo "실패 $(basename "$t")"
    fi
done
echo "검사 $((pass + fail))개 중 $pass개 통과"
[ "$fail" -eq 0 ]
