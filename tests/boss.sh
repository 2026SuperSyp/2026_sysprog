#!/bin/sh
# 보스전의 고장 대비. 보스전 도중에 참가자가 나가거나 뇌가 죽거나 멈춰도 판이 끝까지 가는지 본다.
#   sh tests/boss.sh [포트]      포트부터 4개를 쓴다(기본 4660~4663). 네 경우가 동시에 돈다
#   a. 참가자 한 명이 보스전이 시작되자 나간다: 남은 사람으로 보스전이 끝나고 랭킹까지 간다
#   b. 모두 나간다: 서버는 로비로 돌아가고, 새로 온 사람들로 한 판이 끝까지 간다
#   c. 보스전 도중에 뇌를 죽인다(kill -9): 서버가 뇌를 다시 띄우고 보스전이 끝난다
#   d. 보스전 도중에 뇌를 멈춘다(SIGSTOP): 감시 스레드가 알아채 다시 띄우고 보스전이 끝난다
# 미니게임 라운드는 짧게(1라운드 100틱) 하고, 보스전은 틱 사이를 10ms로 해서 몇 초 동안 이어지게 한다.
PORT=${1:-4660}
B=${SSP_BUILD:-build/dev}
L=build/logs
PEER=$B/tests/fail_peer
mkdir -p $L $B/tests
if [ ! -x $PEER ] || [ tests/fail_peer.c -nt $PEER ] || [ include/protocol.h -nt $PEER ]; then
    ${CC:-gcc} -std=gnu11 -Wall -Wextra -g -O1 -D_GNU_SOURCE -Iinclude -I. -o $PEER tests/fail_peer.c \
        || { echo "실패 boss: tests/fail_peer.c 를 빌드하지 못했습니다"; exit 1; }
fi

wait_line() {   # wait_line <파일> <패턴> <초>
    _i=0
    while [ $_i -lt $(($3 * 20)) ]; do
        grep -q -- "$2" "$1" 2>/dev/null && return 0
        sleep 0.05
        _i=$((_i + 1))
    done
    return 1
}

# server <이름> <포트> <옵션...> : 띄우고 SV에 pid를 둔다
server() {
    _n=$1; _p=$2; shift 2
    $B/server --port=$_p --fast --seed=1 --order=example --rounds=1 --ticks=100 --tick-ms=10 \
        --ranking=$L/boss_ranking.txt "$@" > $L/boss_${_n}_server.log 2>&1 &
    SV=$!
    wait_line $L/boss_${_n}_server.log "기다립니다" 10
}
bot() {         # bot <이름> <포트> <봇 이름> : LAST에 pid를 둔다
    $B/client --bot --port=$2 --name=$3 --wait=2 > $L/boss_$1_$3.log 2>&1 &          # 봇은 스스로 120초 뒤에 끝난다
    LAST=$!
}
end_server() {  # 서버가 끝나기를 기다린다. 끝난 값을 rs에 둔다. 안 끝나면 죽이고 99
    _i=0
    while kill -0 $SV 2>/dev/null && [ $_i -lt 200 ]; do sleep 0.05; _i=$((_i + 1)); done
    if kill -0 $SV 2>/dev/null; then kill -9 $SV; wait $SV 2>/dev/null; rs=99; else wait $SV; rs=$?; fi
}
# 문제가 있으면 why에 덧붙인다: 서버 종료값, 메모리 검사 오류, 남은 공유 메모리
common() {
    [ $rs -eq 0 ] || why="$why 서버 종료값=$rs"
    grep -q -E "AddressSanitizer|LeakSanitizer|runtime error" $L/boss_$1_server.log && why="$why 서버 로그에 메모리 검사 오류"
    [ ! -e /dev/shm/ssp_brain_$SV ] || why="$why 공유 메모리가 남음"
}
result() {      # result <이름> <설명>
    if [ -z "$why" ]; then echo "통과 boss $1 $2" > $L/boss_$1.result
    else echo "실패 boss $1 $2:$why (로그: $L/boss_$1_server.log)" > $L/boss_$1.result; fi
}

case_a() {
    why=""
    server a $PORT --once --boss=10 || why="$why 서버가 뜨지 않음"
    bot a $PORT STAY; b1=$LAST
    sleep 0.2
    $PEER --port=$PORT --mode=watch --name=LEAVER --until=BOSS --sec=40 > $L/boss_a_leaver.log 2>&1; rl=$?
    wait $b1; r1=$?
    end_server
    [ $rl -eq 0 ] || why="$why 나갈 사람이 보스전까지 가지 못함($rl)"
    [ $r1 -eq 0 ] && grep -q ' boss=1$' $L/boss_a_STAY.log || why="$why 남은 사람이 랭킹까지 가지 못함($r1: $(cat $L/boss_a_STAY.log))"
    grep -q "보스전 끝" $L/boss_a_server.log || why="$why 보스전이 끝나지 않음"
    common a
    result a "참가자 한 명이 보스전에서 나가도 남은 사람으로 끝까지 간다"
}

case_b() {
    why=""
    server b $((PORT + 1)) --once --boss=30 || why="$why 서버가 뜨지 않음"
    bot b $((PORT + 1)) GONE1; b1=$LAST
    sleep 0.2
    bot b $((PORT + 1)) GONE2; b2=$LAST
    wait_line $L/boss_b_server.log "보스전 시작" 30 || why="$why 보스전이 시작되지 않음"
    kill -9 $b1 $b2 2>/dev/null
    wait $b1 $b2 2>/dev/null
    sleep 0.5                                        # 서버가 로비로 돌아갈 틈
    kill -0 $SV 2>/dev/null || why="$why 모두 나가자 서버가 끝남"
    bot b $((PORT + 1)) NEW1; n1=$LAST
    sleep 0.2
    bot b $((PORT + 1)) NEW2; n2=$LAST
    wait $n1; r1=$?
    wait $n2; r2=$?
    end_server
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && grep -q ' boss=1$' $L/boss_b_NEW1.log || why="$why 새 판이 끝까지 가지 못함($r1,$r2)"
    [ "$(grep -c '보스전 시작' $L/boss_b_server.log)" -eq 2 ] && [ "$(grep -c '보스전 끝' $L/boss_b_server.log)" -eq 1 ] \
        || why="$why 보스전 시작 $(grep -c '보스전 시작' $L/boss_b_server.log)번, 끝 $(grep -c '보스전 끝' $L/boss_b_server.log)번(2번, 1번이어야 한다)"
    common b
    result b "모두 나가면 로비로 돌아가고 새 판이 끝까지 간다"
}

# brain_case <이름> <포트> <시그널> <서버 로그에 있어야 할 말> <설명>
brain_case() {
    why=""
    server $1 $2 --once --boss=20 || why="$why 서버가 뜨지 않음"
    bot $1 $2 BOT1; b1=$LAST
    sleep 0.2
    bot $1 $2 BOT2; b2=$LAST
    wait_line $L/boss_$1_server.log "보스전 시작" 30 || why="$why 보스전이 시작되지 않음"
    sleep 0.3
    bp=$(pgrep -P $SV -x brain_proc | head -n 1)
    [ -n "$bp" ] && kill -$3 $bp || why="$why 뇌 프로세스를 찾지 못함"
    wait $b1; r1=$?
    wait $b2; r2=$?
    end_server
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] || why="$why 봇이 랭킹까지 가지 못함($r1,$r2)"
    grep -q "$4" $L/boss_$1_server.log || why="$why 뇌를 다시 띄우지 않음"
    grep -q "보스전 끝" $L/boss_$1_server.log || why="$why 보스전이 끝나지 않음"
    [ "$(cat /proc/$bp/comm 2>/dev/null)" = "brain_proc" ] && { kill -9 $bp; why="$why 멈춘 뇌가 남아 있었음"; }
    common $1
    result $1 "$5"
}

rm -f $L/boss_?.result
case_a &
case_b &
brain_case c $((PORT + 2)) KILL "뇌를 다시 띄웠습니다" "보스전 도중에 뇌를 죽여도 다시 띄우고 끝까지 간다" &
brain_case d $((PORT + 3)) STOP "뇌를 다시 띄웠습니다.*멈춤" "보스전 도중에 뇌가 멈춰도 알아채 다시 띄우고 끝까지 간다" &
wait
fail=0
for c in a b c d; do
    if [ -f $L/boss_$c.result ]; then cat $L/boss_$c.result; else echo "실패 boss $c 결과가 없음"; fi
    grep -q '^통과' $L/boss_$c.result 2>/dev/null || fail=$((fail + 1))
done
[ $fail -eq 0 ] && { echo "통과 boss 전부"; exit 0; }
echo "실패 boss $fail건"
exit 1
