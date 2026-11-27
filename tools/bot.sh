#!/bin/sh
# 스테이지 하나를 화면 없이 여러 판 돌려서 검사한다.
#   sh tools/bot.sh <스테이지 이름>
# 통과 조건
#   - 모든 판이 정상 종료한다(판마다 10초 제한)
#   - 같은 씨앗으로 두 번 돌리면 결과가 같다
#   - 경고가 없고, 초파리 칸의 그림이 참가자마다 같다
#   - 그리기를 틱마다 0번, 2번 불러도 점수가 같다
#   - 3명 판에서 각자의 점수가 혼자 돌린 판의 점수와 같다
#     (협동 스테이지는 건너뛴다. 모두가 한 상태를 같이 움직이므로 다른 것이 맞다.
#      stages/이름/config.ini에 "coop = 1"이 있으면 협동 스테이지다)
stage=$1
bin=${SSP_BUILD:-build/dev}/stage_$stage      # 다른 빌드를 보려면 SSP_BUILD=build/release 처럼 준다
log=build/logs/bot_$stage.log
mkdir -p build/logs
: > "$log"
fail=0
runs=0
coop=0
grep -q -E '^[[:space:]]*coop[[:space:]]*=[[:space:]]*1' "stages/$stage/config.ini" 2>/dev/null && coop=1

say_fail() { echo "실패: $1"; fail=$((fail + 1)); }

run() {  # run <옵션들> : 표준 출력을 돌려준다
    runs=$((runs + 1))
    timeout 10 "$bin" --bot "$@" 2>>"$log"
}

field() {  # field <RESULT 줄> <이름> : "이름=값"에서 값을 꺼낸다
    echo "$1" | tr ' ' '\n' | sed -n "s/^$2=//p"
}

for brain in fast slow; do
    for seed in 1 2 3 4 5 6; do
        for n in 1 2 3; do
            opt="--seed=$seed --players=$n --brain=$brain"
            a=$(run $opt) || { say_fail "비정상 종료 ($opt)"; continue; }
            b=$(run $opt) || { say_fail "비정상 종료 ($opt)"; continue; }
            [ "$a" = "$b" ] || say_fail "같은 씨앗인데 결과가 다릅니다 ($opt)"

            res=$(echo "$a" | grep '^RESULT')
            stats=$(echo "$a" | grep '^STATS')
            [ "$(field "$stats" warnings)" = "0" ] || say_fail "경고가 있습니다 ($opt). $log 를 보세요"
            [ "$(field "$stats" fly_mismatch)" = "0" ] || say_fail "초파리 칸의 그림이 참가자마다 다릅니다 ($opt)"

            for d in 0 2; do
                r=$(run $opt --draws=$d | grep '^RESULT')
                [ "$r" = "$res" ] || say_fail "그리기 횟수($d)에 따라 점수가 달라집니다 ($opt)"
            done

            if [ "$n" = "3" ] && [ "$coop" = "1" ]; then
                last="$res / $stats"
            elif [ "$n" = "3" ]; then
                for i in 0 1 2; do
                    solo=$(run --seed=$seed --brain=$brain --solo=$i | grep '^RESULT')
                    [ "$(field "$solo" p0)" = "$(field "$res" p$i)" ] || \
                        say_fail "유저 $i의 점수가 혼자 돌린 판과 다릅니다. 유저별 상태가 섞였습니다 ($opt)"
                    [ "$(field "$solo" fly)" = "$(field "$res" fly)" ] || \
                        say_fail "초파리 점수가 인원에 따라 달라집니다 ($opt)"
                done
                last="$res / $stats"
            fi
        done
    done
done

echo "마지막 3명 판: $last"
[ "$coop" = "1" ] && echo "협동 스테이지: 혼자 돌린 판과의 점수 비교는 건너뛰었습니다"
if [ "$fail" -eq 0 ]; then
    echo "통과: $stage"
else
    echo "실패 $fail건: $stage. 로그: $log"
    exit 1
fi
