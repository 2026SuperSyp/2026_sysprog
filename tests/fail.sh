#!/bin/sh
# 서버와 클라이언트가 실패 상황을 견디는지 본다. 경우마다 한 줄(통과/실패)을 찍는다.
#   sh tests/fail.sh [첫 포트] [경우 번호 ...]
# 첫 포트부터 10개를 쓴다(기본 4720~4729). 번호를 주면 그 경우만 돌린다.
# 경우들은 서로 다른 포트에서 동시에 돈다. FAIL_SERIAL=1이면 하나씩 돌린다.
# 로그는 build/logs/fail/ 에 남는다. 스레드 검사용 실행 파일은 build/tsan/ 에 따로 만든다.
# 먼저 make all 로 서버, 클라이언트, 스테이지를 빌드해 두어야 한다.
#
#   1  스테이지를 죽임(kill -9)          6  읽지 않는 접속자
#   2  스테이지가 멈춤                   7  잘못된 입력
#   3  스테이지가 없거나 뜨자마자 죽음    8  서버를 죽이고 다시 띄움
#   4  참가자 한 명이 도중에 나감         9  접속이 16개를 넘음, HELLO가 아주 느림, 접속을 되풀이
#   5  참가자가 모두 도중에 나감         10  스레드 검사(ThreadSanitizer)
# 새는 것은 세 가지로 본다: 서버가 끝날 때 메모리 검사 도구가 남긴 것(서버, 봇, 클라이언트, 스테이지 로그),
# 판이나 접속이 끝난 뒤 서버의 fd와 스레드 수(1번, 9번), 접속을 되풀이하는 동안 서버의 메모리(9번).
# kill -9로 죽인 프로세스는 검사 도구가 아무것도 남기지 못한다(8번).
BASE=${1:-4720}
[ $# -gt 0 ] && shift
CASES=${*:-1 2 3 4 5 6 7 8 9 10}
B=${SSP_BUILD:-build/dev}         # 다른 빌드를 보려면 SSP_BUILD=build/release 처럼 준다
L=build/logs/fail
PEER=$B/tests/fail_peer
T0=$(date +%s)
# 스테이지는 라운드가 끝나면 정상 종료하므로 그때 메모리 검사 도구가 스테이지 로그에 남긴다. 돌리기 전과 뒤를 센다
stage_errs() { cat build/logs/stage_*.log 2>/dev/null | grep -c -E 'AddressSanitizer|LeakSanitizer|runtime error'; }
SE0=$(stage_errs)

for f in $B/server $B/client $B/stage_example; do
    [ -x $f ] || { echo "실패: $f 가 없습니다. 먼저 make all 을 실행하세요"; exit 1; }
done
mkdir -p $L $B/tests
rm -f $L/*
: > $L/pids

# 검사용 접속자를 빌드한다. 통신 규격만 쓰므로 파일 하나로 된다.
if [ ! -x $PEER ] || [ tests/fail_peer.c -nt $PEER ] || [ include/protocol.h -nt $PEER ]; then
    ${CC:-gcc} -std=gnu11 -Wall -Wextra -g -O1 -D_GNU_SOURCE -Iinclude -I. -o $PEER tests/fail_peer.c \
        || { echo "실패: tests/fail_peer.c 를 빌드하지 못했습니다"; exit 1; }
fi

# 가짜 스테이지: 검사용 접속자가 그 노릇도 한다(종류는 tests/fail_peer.c에 있다).
# 서버가 build/dev/stage_<이름>을 띄우므로 그 이름으로 복사해 두고, 끝날 때 지운다.
FAKES=""
for k in crash partial win early bigscore count; do
    cp $PEER $B/stage_t_$k || exit 1
    FAKES="$FAKES $B/stage_t_$k"
done

# 끝날 때 이 검사가 띄운 프로세스만(기록해 둔 pid만) 정리한다.
cleanup() {
    for p in $(cat $L/pids 2>/dev/null); do kill -9 "$p" 2>/dev/null; done
    rm -f $FAKES
}
trap cleanup EXIT
trap 'exit 1' INT TERM

# ---- 도구 ---------------------------------------------------------------------
alive() { [ -d /proc/$1 ] && ! grep -q '^State:[[:space:]]*Z' /proc/$1/status 2>/dev/null; }

wait_line() {   # wait_line <파일> <패턴> <초>
    _i=0
    while [ $_i -lt $(($3 * 20)) ]; do
        grep -q -- "$2" "$1" 2>/dev/null && return 0
        sleep 0.05
        _i=$((_i + 1))
    done
    return 1
}

wait_exit() {   # wait_exit <pid> <초> : 끝난 값을 돌려준다. 시간이 다 되면 죽이고 124
    _i=0
    while alive $1; do
        if [ $_i -ge $(($2 * 20)) ]; then kill -9 $1 2>/dev/null; wait $1 2>/dev/null; return 124; fi
        sleep 0.05
        _i=$((_i + 1))
    done
    wait $1
}

wait_gone() {   # wait_gone <pid> <초> : 내 자식이 아닌 프로세스가 사라지기를 기다린다
    _i=0
    while alive $1; do
        [ $_i -ge $(($2 * 20)) ] && return 1
        sleep 0.05
        _i=$((_i + 1))
    done
    return 0
}

server() {      # server <경우> <옵션...> : 띄우고 SV에 pid를 둔다. NOFILE이 있으면 fd 한도를 그만큼으로 낮춘다
    _c=$1; shift
    PORT=$((BASE + _c % 10))
    ( [ -n "$NOFILE" ] && ulimit -n $NOFILE; exec ${SERVER_BIN:-$B/server} --port=$PORT --ranking=$L/ranking.txt --boss=0 --rejoin-ms=0 "$@" ) >> $L/c$_c.server.log 2>&1 &
    SV=$!
    echo $SV >> $L/pids
    wait_line $L/c$_c.server.log "포트 $PORT에서 기다립니다" 5
}

bot() {         # bot <경우> <이름> <시작 인원> : 띄우고 LAST에 pid를 둔다
    ${CLIENT_BIN:-$B/client} --bot --port=$PORT --name=$2 --wait=$3 > $L/c$1.$2.log 2>&1 &
    LAST=$!
    echo $LAST >> $L/pids
}

peer() {        # peer <경우> <로그 이름> <옵션...>
    _c=$1; _n=$2; shift 2
    $PEER --port=$PORT "$@" > $L/c$_c.$_n.log 2>&1 &
    LAST=$!
    echo $LAST >> $L/pids
}

# 서버의 자식 가운데 스테이지만 고른다(뇌 프로세스도 서버의 자식이다)
stage_pid() { pgrep -P $SV -f 'stage_' 2>/dev/null | head -n 1; }
zombies()   { ps -o stat= --ppid $SV 2>/dev/null | grep -c Z; }
rss_kb()    { awk '/^VmRSS/ { print $2 }' /proc/$1/status 2>/dev/null; }
n_fd()      { ls /proc/$1/fd 2>/dev/null | wc -l; }
n_thread()  { awk '/^Threads/ { print $2 }' /proc/$1/status 2>/dev/null; }

# 서버의 fd 수와 스레드 수가 기준(BASE_FD, BASE_TH)으로 돌아오기를 기다린다. 끝난 접속과 라운드가
# 남긴 것(닫지 않은 소켓이나 통로, 끝나지 않은 스레드)이 있으면 돌아오지 않는다.
back_to_base() { # back_to_base <설명> : 2초 안에 돌아오지 않으면 WHY에 덧붙인다
    _i=0
    while [ $_i -lt 40 ]; do
        [ "$(n_fd $SV)" -le "$BASE_FD" ] && [ "$(n_thread $SV)" -le "$BASE_TH" ] && return 0
        sleep 0.05
        _i=$((_i + 1))
    done
    WHY="$WHY $1: 서버의 fd $BASE_FD -> $(n_fd $SV)개, 스레드 $BASE_TH -> $(n_thread $SV)개"
}

# 서버를 SIGTERM으로 끝내고, 깨끗이 끝났는지와 로그에 검사 도구의 오류가 없는지 본다.
# 서버는 정상 종료하므로 그때 메모리 검사 도구가 풀지 않은 메모리를 찾아 로그에 남긴다.
# 그 경우에 띄운 봇과 클라이언트의 로그(c<경우>.*.log)도 같이 본다.
stop_server() { # stop_server <경우> : 문제가 있으면 WHY에 덧붙인다
    if alive $SV; then
        kill -TERM $SV
        wait_exit $SV 5; _r=$?
        [ $_r -eq 0 ] || WHY="$WHY 서버 종료값=$_r"
    else
        WHY="$WHY 서버가 죽어 있음"
    fi
    _bad=$(grep -l -E 'AddressSanitizer|runtime error|LeakSanitizer' $L/c$1.*.log 2>/dev/null | tr '\n' ' ')
    [ -z "$_bad" ] || WHY="$WHY 로그에 검사 오류($_bad)"
}

begin() { WHY=""; T_CASE=$(date +%s%N); }

report() {      # report <경우> <설명> : WHY가 비어 있으면 통과. 걸린 시간을 같이 적는다
    _ms=$(( ($(date +%s%N) - T_CASE) / 1000000 ))
    _t="$((_ms / 1000)).$((_ms % 1000 / 100))초"
    if [ -z "$WHY" ]; then echo "통과 $1 $2 ($_t)" > $L/c$1.result
    else echo "실패 $1 $2 ($_t):$WHY" > $L/c$1.result; fi
}

# 봇 2명과 관찰자 1명을 참가자로 넣는다. pid는 B1, B2, W에 둔다.
join3() {       # join3 <경우> [관찰자 옵션...]
    _c3=$1; shift
    bot $_c3 BOT1 3; B1=$LAST
    sleep 0.15
    bot $_c3 BOT2 3; B2=$LAST
    sleep 0.15
    peer $_c3 watch --mode=watch --name=WATCH --sec=30 "$@"; W=$LAST
}

# ---- 1. 스테이지를 죽임 ----------------------------------------------------------
case_1() {
    begin
    server 1 --fast --seed=1 --order=example --rounds=2 --tick-ms=10 --ticks=150 || WHY="$WHY 서버가 뜨지 않음"
    BASE_FD=$(n_fd $SV); BASE_TH=$(n_thread $SV)    # 아무도 없을 때. 판이 끝나고 모두 나가면 여기로 돌아와야 한다
    join3 1
    npipe=0; nleak=1
    if wait_line $L/c1.watch.log 'PHASE PLAY round=1' 10; then
        sleep 0.2
        st=$(stage_pid)
        # 스테이지가 서버에게서 물려받는 것은 통로 2개뿐이어야 한다(소켓이나 eventfd가 보이면 샌 것이다)
        ls -l /proc/$st/fd > $L/c1.stage_fd.log 2>/dev/null
        npipe=$(grep -c 'pipe:' $L/c1.stage_fd.log)
        nleak=$(grep -c -E 'socket:|eventfd' $L/c1.stage_fd.log)
        kill -9 $st
    else
        WHY="$WHY PLAY에 가지 못함"
    fi
    wait_exit $B1 20; r1=$?
    wait_exit $B2 20; r2=$?
    wait_exit $W 20; rw=$?
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rw -eq 0 ] || WHY="$WHY 봇=$r1,$r2 관찰자=$rw"
    grep -q '무효' $L/c1.server.log || WHY="$WHY 무효 처리가 없음"
    [ "$(grep -c 'PHASE VERDICT' $L/c1.watch.log)" -eq 2 ] || WHY="$WHY 판정이 2번이 아님"
    [ "$npipe" -eq 2 ] && [ "$nleak" -eq 0 ] || WHY="$WHY 스테이지 fd: 통로 ${npipe}개, 샌 것 ${nleak}개"
    sleep 0.3
    [ "$(zombies)" -eq 0 ] || WHY="$WHY 거두지 않은 자식이 있음"
    back_to_base "2라운드와 접속 3개가 끝난 뒤"
    stop_server 1
    report 1 "스테이지를 죽임(kill -9): 무효 뒤 다음 라운드, 랭킹까지 감"
}

# ---- 2. 스테이지가 멈춤 ----------------------------------------------------------
case_2() {
    begin
    # (가) 게임 중에 SIGSTOP
    server 2 --fast --seed=1 --order=example --rounds=1 --tick-ms=10 --ticks=150 || WHY="$WHY 서버가 뜨지 않음"
    join3 2
    st=0
    if wait_line $L/c2.watch.log 'PHASE PLAY round=1' 10; then
        sleep 0.2
        st=$(stage_pid)
        kill -STOP $st
    else
        WHY="$WHY PLAY에 가지 못함"
    fi
    wait_exit $B1 20; r1=$?
    wait_exit $B2 20; r2=$?
    wait_exit $W 20; rw=$?
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rw -eq 0 ] || WHY="$WHY (가) 봇=$r1,$r2 관찰자=$rw"
    grep -q '답하지 않습니다' $L/c2.server.log || WHY="$WHY (가) 시간 초과 처리가 없음"
    wait_gone $st 2 || WHY="$WHY (가) 멈춘 스테이지가 남아 있음"
    stop_server 2

    # (나) 메시지를 절반만 보내고 멈추는 스테이지. 게임 마스터가 읽다가 막히면 안 된다
    mv $L/c2.server.log $L/c2.server_a.log
    mv $L/c2.watch.log $L/c2.watch_a.log
    server 2 --fast --seed=1 --order=t_partial,example --rounds=2 --tick-ms=10 --ticks=40 || WHY="$WHY 서버가 뜨지 않음"
    join3 2
    wait_exit $B1 25; r1=$?
    wait_exit $B2 25; r2=$?
    wait_exit $W 25; rw=$?
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rw -eq 0 ] || WHY="$WHY (나) 봇=$r1,$r2 관찰자=$rw"
    grep -q '무효' $L/c2.server.log || WHY="$WHY (나) 무효 처리가 없음"
    sleep 0.3
    [ -z "$(stage_pid)" ] || WHY="$WHY (나) 스테이지가 남아 있음"
    stop_server 2
    report 2 "스테이지가 멈춤(SIGSTOP, 메시지 중간): 시간이 지나면 죽이고 이어 감"
}

# ---- 3. 스테이지가 없거나 뜨자마자 죽음 --------------------------------------------
case_3() {
    begin
    # (가) 실행 파일이 없다: 무효가 4번 쌓이면 랭킹으로 가서 판을 끝낸다
    server 3 --fast --seed=1 --order=nosuchstage --rounds=2 || WHY="$WHY 서버가 뜨지 않음"
    join3 3
    wait_exit $W 15; rw=$?
    wait_exit $B1 15; wait_exit $B2 15
    [ $rw -eq 0 ] || WHY="$WHY (가) 랭킹에 가지 못함($rw)"
    n=$(grep -c '무효' $L/c3.server.log)
    [ "$n" -eq 4 ] || WHY="$WHY (가) 무효가 ${n}번(4번이어야 함)"
    sleep 0.3
    [ "$(zombies)" -eq 0 ] || WHY="$WHY (가) 거두지 않은 자식이 있음"
    # 랭킹 뒤에는 로비로 돌아와 접속을 계속 받는다
    $PEER --port=$PORT --mode=fill --n=1 --sec=0.5 > $L/c3.fill.log 2>&1
    grep -q 'welcomed=1' $L/c3.fill.log || WHY="$WHY (가) 그 뒤 접속을 받지 않음"
    stop_server 3

    # (나) 뜨자마자 죽는 스테이지가 섞여 있다: 그 라운드만 무효이고 판은 끝까지 간다
    mv $L/c3.server.log $L/c3.server_a.log
    mv $L/c3.watch.log $L/c3.watch_a.log
    server 3 --fast --seed=1 --order=t_crash,example --rounds=2 || WHY="$WHY 서버가 뜨지 않음"
    join3 3
    wait_exit $B1 15; r1=$?
    wait_exit $B2 15; r2=$?
    wait_exit $W 15; rw=$?
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rw -eq 0 ] || WHY="$WHY (나) 봇=$r1,$r2 관찰자=$rw"
    grep -q '무효' $L/c3.server.log || WHY="$WHY (나) 무효 처리가 없음"
    [ "$(grep -c 'PHASE VERDICT' $L/c3.watch.log)" -eq 2 ] || WHY="$WHY (나) 판정이 2번이 아님"
    stop_server 3

    # (다) 점수를 터무니없이 크게 보내고, 결과를 낸 뒤에도 끝나지 않는 스테이지:
    #      누적 점수가 넘쳐 서버가 죽으면 안 되고, 지난 라운드의 스테이지가 남아 있으면 안 된다
    mv $L/c3.server.log $L/c3.server_b.log
    mv $L/c3.watch.log $L/c3.watch_b.log
    server 3 --fast --seed=1 --order=t_bigscore --rounds=3 || WHY="$WHY 서버가 뜨지 않음"
    join3 3
    wait_exit $B1 15; r1=$?
    wait_exit $B2 15; r2=$?
    wait_exit $W 15; rw=$?
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rw -eq 0 ] || WHY="$WHY (다) 봇=$r1,$r2 관찰자=$rw"
    grep -q 'rounds=3 .*total=3000000' $L/c3.BOT1.log || WHY="$WHY (다) 점수가 잘리지 않음($(cat $L/c3.BOT1.log))"
    sleep 0.3
    [ -z "$(stage_pid)" ] || WHY="$WHY (다) 끝나지 않은 스테이지가 남아 있음"
    stop_server 3
    report 3 "스테이지가 없거나, 뜨자마자 죽거나, 점수가 터무니없음: 무효 4번이면 랭킹으로 끝냄"
}

# ---- 4. 참가자 한 명이 도중에 나감 --------------------------------------------------
case_4() {
    begin
    # 참가자가 늘 이기는 스테이지를 쓴다. 그래야 나간 사람의 패가 점수 때문이 아님을 알 수 있다
    server 4 --fast --seed=1 --order=t_win --rounds=2 --tick-ms=10 || WHY="$WHY 서버가 뜨지 않음"
    join3 4
    if wait_line $L/c4.watch.log 'PHASE PLAY round=1' 10; then
        sleep 0.2
        kill -9 $B2
    else
        WHY="$WHY PLAY에 가지 못함"
    fi
    wait_exit $B1 20; r1=$?
    wait_exit $W 20; rw=$?
    wait $B2 2>/dev/null
    [ $r1 -eq 0 ] && [ $rw -eq 0 ] || WHY="$WHY 남은 봇=$r1 관찰자=$rw"
    v1=$(grep 'PHASE VERDICT round=1' $L/c4.watch.log)
    echo "$v1" | grep -q 'BOT1:seat=1:lives=3:pass=1' || WHY="$WHY 남은 사람이 이긴 것으로 되지 않음"
    echo "$v1" | grep -q 'BOT2:seat=0:lives=2:pass=0' || WHY="$WHY 나간 사람이 진 것으로 되지 않음"
    f=$(echo "$v1" | sed -n 's/.* frames=\([0-9]*\).*/\1/p')
    [ "${f:-0}" -ge 45 ] || WHY="$WHY 라운드가 이어지지 않음(화면 ${f:-0}장, 60장 중)"
    grep -q 'PHASE VERDICT round=2' $L/c4.watch.log || WHY="$WHY 2라운드가 없음"
    grep -q 'SEATS .*BOT2:seat=0' $L/c4.watch.log || WHY="$WHY 게임 도중에 나간 것이 아님"
    stop_server 4
    report 4 "참가자 한 명이 도중에 나감: 라운드는 계속, 나간 사람은 패"
}

# ---- 5. 참가자가 모두 도중에 나감 ---------------------------------------------------
case_5() {
    begin
    server 5 --fast --seed=1 --order=example --rounds=2 --tick-ms=10 --ticks=80 || WHY="$WHY 서버가 뜨지 않음"
    join3 5
    sleep 0.15
    # 네 번째는 관전자다. 로비로 돌아오는 것을 보면 나간다
    peer 5 spec --mode=watch --name=SPEC --until=LOBBY --sec=20; SP=$LAST
    if wait_line $L/c5.watch.log 'PHASE PLAY round=1' 10; then
        sleep 0.2
        kill -9 $B1 $B2 $W
    else
        WHY="$WHY PLAY에 가지 못함"
    fi
    wait_exit $SP 10; rs=$?
    wait $B1 $B2 $W 2>/dev/null
    [ $rs -eq 0 ] || WHY="$WHY 로비로 돌아오지 않음($rs)"
    sleep 0.3
    [ -z "$(stage_pid)" ] || WHY="$WHY 스테이지가 남아 있음"
    # 같은 서버에서 새 판
    mv $L/c5.watch.log $L/c5.watch_a.log
    bot 5 NEW1 2; n1=$LAST
    sleep 0.15
    bot 5 NEW2 2; n2=$LAST
    wait_exit $n1 20; r1=$?
    wait_exit $n2 20; r2=$?
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] || WHY="$WHY 새 판의 봇=$r1,$r2"
    grep -q 'rounds=2' $L/c5.NEW1.log || WHY="$WHY 새 판이 2라운드가 아님"
    stop_server 5
    report 5 "참가자가 모두 도중에 나감: 로비로 돌아가고 새 판이 됨"
}

# ---- 6. 읽지 않는 접속자 ------------------------------------------------------------
case_6() {
    begin
    # 메모리를 재려고, 검사 도구가 풀린 메모리를 붙들어 두는 양을 줄인다
    export ASAN_OPTIONS=quarantine_size_mb=4
    server 6 --fast --seed=1 --order=example --rounds=1 --tick-ms=10 --ticks=1200 || WHY="$WHY 서버가 뜨지 않음"
    unset ASAN_OPTIONS
    bot 6 BOT1 3; B1=$LAST
    sleep 0.15
    bot 6 BOT2 3; B2=$LAST
    sleep 0.15
    # 읽지 않는 쪽은 서버가 끊어도 스스로는 알 수 없다. 그래서 끊었는지는 서버 쪽에서 본다:
    # 자리 표시(관찰자의 SEATS 줄)와, 16개를 더 열었을 때 받아 주는 수(빈 접속 자리 수)로 안다.
    peer 6 slow --mode=slow --name=SLOW --rcvbuf=4096 --sec=14; SL=$LAST
    sleep 0.15
    peer 6 watch --mode=watch --name=WATCH --sec=30; W=$LAST
    wait_line $L/c6.watch.log 'PHASE PLAY round=1' 10 || WHY="$WHY PLAY에 가지 못함"
    rss0=$(rss_kb $SV)
    # 읽지 않다가 1.5초 뒤에 잘못된 머리를 보내는 관전자. 쓰기 스레드가 막혀 있어도 바로 자리가 비어야 한다
    peer 6 stuck --mode=stuck --name=STUCK --rcvbuf=4096 --after=1500 --sec=12; SK=$LAST
    sleep 2.1
    # 지금 남아 있어야 하는 접속은 봇 2, 읽지 않는 참가자, 관찰자로 4개다. 그러니 16개 중 12개가 받아져야 한다.
    # (읽지 않는 참가자가 그새 끊겼으면 13개다. 쓰기가 막힌 접속의 자리가 비지 않았으면 11개다.)
    want=12
    grep -q 'SEATS .*SLOW:seat=0' $L/c6.watch.log && want=13
    $PEER --port=$PORT --mode=fill --n=16 --sec=0.5 > $L/c6.fill1.log 2>&1
    w1=$(sed -n 's/^FILL welcomed=\([0-9]*\).*/\1/p' $L/c6.fill1.log)
    [ "${w1:-0}" -ge $want ] || WHY="$WHY 쓰기가 막힌 접속의 자리가 비지 않음($(head -n 1 $L/c6.fill1.log), 기대 $want)"
    # 읽지 않는 참가자는 SEND_WAIT_MS(3초)쯤 뒤에 끊긴다
    wait_line $L/c6.watch.log 'SEATS .*SLOW:seat=0' 8 || WHY="$WHY 읽지 않는 접속자를 끊지 않음"
    rss1=$(rss_kb $SV)
    $PEER --port=$PORT --mode=fill --n=16 --sec=0.5 > $L/c6.fill2.log 2>&1
    grep -q 'welcomed=13 ' $L/c6.fill2.log || WHY="$WHY 끊은 뒤에도 자리가 비지 않음($(head -n 1 $L/c6.fill2.log))"
    wait_exit $B1 20; r1=$?
    wait_exit $B2 20; r2=$?
    wait_exit $W 20; rw=$?
    kill -9 $SL $SK 2>/dev/null
    wait $SL $SK 2>/dev/null
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rw -eq 0 ] || WHY="$WHY 봇=$r1,$r2 관찰자=$rw"
    v=$(grep 'PHASE VERDICT round=1' $L/c6.watch.log)
    f=$(echo "$v" | sed -n 's/.* frames=\([0-9]*\).*/\1/p')
    g=$(echo "$v" | sed -n 's/.* gap=\([0-9]*\).*/\1/p')
    t_play=$(sed -n 's/^PHASE PLAY round=1 ms=\([0-9]*\).*/\1/p' $L/c6.watch.log)
    t_drop=$(sed -n 's/^SEATS ms=\([0-9]*\).*SLOW:seat=0.*/\1/p' $L/c6.watch.log | head -n 1)
    # 화면은 최신 것만 보내므로 받는 쪽이 바쁘면 몇 장은 건너뛴다. 그래서 장수는 넉넉하게 본다
    [ "${f:-0}" -ge 400 ] || WHY="$WHY 다른 접속자가 받은 화면이 ${f:-0}장(500장 중)"
    [ "${g:-9999}" -lt 300 ] || WHY="$WHY 화면 사이가 ${g}ms까지 벌어짐"
    grow=$(( ${rss1:-0} - ${rss0:-0} ))
    [ $grow -lt 65536 ] || WHY="$WHY 서버 메모리가 ${grow}kB 늘어남"
    echo "rss0=${rss0}kB rss1=${rss1}kB frames=$f gap=${g}ms drop_after_play_ms=$(( ${t_drop:-0} - ${t_play:-0} )) fill1=$(head -n 1 $L/c6.fill1.log) fill2=$(head -n 1 $L/c6.fill2.log)" > $L/c6.numbers
    stop_server 6
    report 6 "읽지 않는 접속자: 그 접속만 끊고 나머지는 제 속도로 받음"
}

# ---- 7. 잘못된 입력 -----------------------------------------------------------------
case_7() {
    begin
    server 7 --fast --seed=1 --order=example,t_early,t_count --rounds=3 --tick-ms=10 --ticks=150 --idle-ms=3000 || WHY="$WHY 서버가 뜨지 않음"
    pids=""
    for m in silent idle version hugelen garbage late half cut; do
        peer 7 $m --mode=$m --sec=8
        pids="$pids $m:$LAST"
    done
    for mp in $pids; do
        wait_exit ${mp#*:} 10; r=$?
        [ $r -eq 0 ] || WHY="$WHY ${mp%%:*}=$r"
    done
    # 들어온 뒤 아무것도 보내지 않는 접속은 --idle-ms(여기서는 3초)쯤에 끊겨야 한다
    im=$(sed -n 's/^BAD idle closed_ms=\([0-9]*\).*/\1/p' $L/c7.idle.log)
    [ -n "$im" ] && [ "$im" -ge 2500 ] && [ "$im" -le 5000 ] || WHY="$WHY 말 없는 접속을 ${im:-?}ms에 끊음(3000쯤이어야 한다)"
    # 자리가 새지 않았는지: 16개가 모두 다시 받아져야 한다
    sleep 0.2
    $PEER --port=$PORT --mode=fill --n=16 --sec=1 > $L/c7.fill.log 2>&1
    grep -q 'welcomed=16 refused=0' $L/c7.fill.log || WHY="$WHY 자리가 샘($(head -n 1 $L/c7.fill.log))"

    # 입력을 쉬지 않고 쏟아붓는 참가자, 20ms마다 키를 하나 누르는 참가자(probe), 봇이 한 판(3라운드)을 한다.
    # 판이 멈추면 안 된다. 2라운드는 결과를 내기 전에 읽는 쪽을 먼저 닫는 스테이지다. 그때 못 보낸 입력
    # 때문에 라운드가 무효가 되어서도 안 된다(결과가 이미 오고 있다).
    # 한 라운드는 참가자별로 받은 입력 수를 점수로 내는 스테이지(t_count, 제목 "FAKE count")다. 서버는 접속 하나의 입력을
    # 100ms에 256개까지만 넘긴다(core/server.c의 INPUT_WIN_*). 그래서
    #   - 폭주하는 참가자의 입력이 스테이지에 닿은 수는 1.5초 판에서 6000개를 넘지 않아야 하고
    #     (막는 것이 없으면 백만 개 가까이 닿는다)
    #   - 다른 참가자가 누른 키는 거의 다 닿아야 한다(판이 끝날 무렵의 몇 개만 빠질 수 있다)
    sleep 0.2
    bot 7 BOT1 3; B1=$LAST
    sleep 0.15
    peer 7 flood1 --mode=flood --name=FLOOD1 --sec=20; F1=$LAST
    sleep 0.15
    peer 7 probe --mode=probe --after=20 --name=PROBE --sec=20; F2=$LAST
    wait_exit $B1 21; r1=$?
    wait_exit $F1 21; rf1=$?
    wait_exit $F2 21; rf2=$?
    [ $r1 -eq 0 ] && [ $rf1 -eq 0 ] && [ $rf2 -eq 0 ] || WHY="$WHY 입력 폭주: 봇=$r1 폭주=$rf1 probe=$rf2"
    # 화면 수는 example 라운드(150틱)에서 본다. 라운드 순서는 서버가 섞으므로 제목으로 찾는다
    ex=$(sed -n 's/^PROBE round=\([0-9]*\) .* title=돌 피하기$/\1/p' $L/c7.probe.log)
    f=$(sed -n "s/^PHASE VERDICT round=${ex:-0} .* frames=\([0-9]*\).*/\1/p" $L/c7.flood1.log)
    [ "${f:-0}" -ge 100 ] || WHY="$WHY 입력 폭주 중 화면이 ${f:-0}장(150장 중)"
    grep -q 'rounds=3' $L/c7.BOT1.log || WHY="$WHY 입력 폭주 판이 3라운드가 아님($(cat $L/c7.BOT1.log))"
    grep -q '무효' $L/c7.server.log && WHY="$WHY 결과를 낸 라운드가 무효가 됨"
    cr=$(sed -n 's/^PROBE round=\([0-9]*\) .* title=FAKE count$/\1/p' $L/c7.probe.log)
    fl=$(sed -n "s/^PHASE VERDICT round=${cr:-0} .* FLOOD1:[^ ]*:score=\([0-9]*\).*/\1/p" $L/c7.probe.log)
    ps=$(sed -n "s/^PROBE round=${cr:-0} sent=\([0-9]*\) got=.*/\1/p" $L/c7.probe.log)
    pg=$(sed -n "s/^PROBE round=${cr:-0} sent=[0-9]* got=\([0-9]*\).*/\1/p" $L/c7.probe.log)
    [ "${fl:-999999}" -gt 0 ] && [ "${fl:-999999}" -le 6000 ] || WHY="$WHY 폭주한 입력이 스테이지에 ${fl:-?}개 닿음(한도 6000)"
    [ "${ps:-0}" -ge 50 ] && [ "${pg:-0}" -ge $(( ${ps:-0} - 3 )) ] || WHY="$WHY 다른 참가자의 입력이 빠짐(보낸 ${ps:-?}, 닿은 ${pg:-?})"
    echo "flood_delivered=$fl probe_sent=$ps probe_got=$pg / $(tail -n 1 $L/c7.flood1.log) / $(cat $L/c7.BOT1.log)" > $L/c7.numbers
    stop_server 7
    report 7 "잘못된 입력(버전, 큰 len, 쓰레기, 중간에 끊김, 말 없는 접속, 입력 폭주): 그 접속만 끊고 판은 이어 감"
}

# ---- 8. 서버를 죽이고 다시 띄움 -------------------------------------------------------
case_8() {
    begin
    server 8 --fast --seed=1 --order=example --rounds=2 --tick-ms=10 --ticks=300 || WHY="$WHY 서버가 뜨지 않음"
    join3 8
    st=0
    if wait_line $L/c8.watch.log 'PHASE PLAY round=1' 10; then
        sleep 0.2
        st=$(stage_pid)
        kill -9 $SV
        wait $SV 2>/dev/null
    else
        WHY="$WHY PLAY에 가지 못함"
    fi
    # 바로 같은 포트로 다시 띄운다
    mv $L/c8.server.log $L/c8.server_a.log
    server 8 --fast --once --seed=1 --order=example --rounds=1 || WHY="$WHY 같은 포트로 다시 뜨지 않음"
    [ "$st" -gt 0 ] && wait_gone $st 2 || WHY="$WHY 스테이지($st)가 남아 있음"
    wait_exit $B1 5; r1=$?
    wait_exit $B2 5; r2=$?
    wait_exit $W 5; rw=$?
    [ $r1 -ne 0 ] && [ $r2 -ne 0 ] && [ $rw -ne 0 ] || WHY="$WHY 옛 접속자가 끊김을 모름($r1,$r2,$rw)"
    bot 8 NEW1 2; n1=$LAST
    sleep 0.15
    bot 8 NEW2 2; n2=$LAST
    wait_exit $n1 20; r1=$?
    wait_exit $n2 20; r2=$?
    wait_exit $SV 10; rs=$?
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ] || WHY="$WHY 새 서버의 판: 봇=$r1,$r2 서버=$rs"
    report 8 "서버를 죽이고 다시 띄움: 스테이지도 죽고 같은 포트가 바로 열림"
}

# ---- 9. 접속이 16개를 넘음 ------------------------------------------------------------
case_9() {
    begin
    server 9 --fast --seed=1 --order=example --rounds=1 --tick-ms=10 --ticks=250 || WHY="$WHY 서버가 뜨지 않음"
    join3 9
    wait_line $L/c9.watch.log 'PHASE PLAY round=1' 10 || WHY="$WHY PLAY에 가지 못함"
    # 이미 3개가 있다. 20개를 더 열면 13개만 받아져야 한다
    $PEER --port=$PORT --mode=fill --n=20 --sec=1.2 > $L/c9.fill.log 2>&1
    grep -q 'welcomed=13 refused=7' $L/c9.fill.log || WHY="$WHY $(head -n 1 $L/c9.fill.log)"
    grep -q 'FILL_END alive=13 lost=0' $L/c9.fill.log || WHY="$WHY $(tail -n 1 $L/c9.fill.log)"
    wait_exit $B1 20; r1=$?
    wait_exit $B2 20; r2=$?
    wait_exit $W 20; rw=$?
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rw -eq 0 ] || WHY="$WHY 봇=$r1,$r2 관찰자=$rw"
    v=$(grep 'PHASE VERDICT round=1' $L/c9.watch.log)
    f=$(echo "$v" | sed -n 's/.* frames=\([0-9]*\).*/\1/p')
    [ "${f:-0}" -ge 200 ] || WHY="$WHY 있던 접속자가 받은 화면이 ${f:-0}장(250장 중)"
    # 자리가 비면 다시 받는다
    sleep 0.3
    $PEER --port=$PORT --mode=fill --n=16 --sec=0.8 > $L/c9.fill2.log 2>&1
    grep -q 'welcomed=16 refused=0' $L/c9.fill2.log || WHY="$WHY 뒤에 다시 받지 않음($(head -n 1 $L/c9.fill2.log))"
    stop_server 9

    # (나) fd 한도가 낮아 accept가 실패해도, 자리가 나면 다시 받아야 한다
    mv $L/c9.server.log $L/c9.server_a.log
    NOFILE=16
    server 9 --fast || WHY="$WHY 서버가 뜨지 않음"
    NOFILE=""
    $PEER --port=$PORT --mode=fill --n=14 --sec=1 > $L/c9.fill3.log 2>&1
    sleep 0.4
    $PEER --port=$PORT --mode=fill --n=1 --sec=1 > $L/c9.fill4.log 2>&1
    grep -q 'welcomed=1 ' $L/c9.fill4.log || WHY="$WHY (나) fd가 모자랐던 뒤로 접속을 받지 않음"
    stop_server 9

    # (다) HELLO를 0.5초에 한 바이트씩 보내는 접속 16개가 자리를 다 채운다. HELLO 전체의 한도(3초) 뒤에는
    # 모두 끊겨야 하고, 그 뒤에 온 진짜 접속자는 들어와야 한다
    mv $L/c9.server.log $L/c9.server_b.log
    export ASAN_OPTIONS=quarantine_size_mb=4         # 메모리를 재려고 줄인다(6번과 같다)
    server 9 --fast || WHY="$WHY 서버가 뜨지 않음"
    unset ASAN_OPTIONS
    BASE_FD=$(n_fd $SV); BASE_TH=$(n_thread $SV)
    peer 9 drip --mode=drip --n=16 --after=500 --sec=8; DR=$LAST
    sleep 0.5
    $PEER --port=$PORT --mode=fill --n=1 --sec=0.3 > $L/c9.fill5.log 2>&1
    grep -q 'refused=1' $L/c9.fill5.log || WHY="$WHY (다) 느린 접속 16개가 자리를 채우지 못함($(head -n 1 $L/c9.fill5.log))"
    wait_exit $DR 9; rd=$?
    dm=$(sed -n 's/^DRIP n=16 closed=16 max_ms=\([0-9]*\).*/\1/p' $L/c9.drip.log)
    [ $rd -eq 0 ] && [ -n "$dm" ] && [ "$dm" -le 4000 ] || WHY="$WHY (다) 느린 HELLO가 자리를 오래 붙듦($(head -n 1 $L/c9.drip.log))"
    $PEER --port=$PORT --mode=fill --n=1 --sec=0.3 > $L/c9.fill6.log 2>&1
    grep -q 'welcomed=1 ' $L/c9.fill6.log || WHY="$WHY (다) 느린 접속이 끊긴 뒤에 들어오지 못함($(head -n 1 $L/c9.fill6.log))"

    # (라) 접속을 열고 닫기를 되풀이해도 서버의 fd, 스레드, 메모리가 늘지 않는다
    for k in 1 2 3 4 5 6 7 8; do
        $PEER --port=$PORT --mode=fill --n=16 --sec=0.2 > $L/c9.cycle.log 2>&1
        for m in version garbage half cut; do $PEER --port=$PORT --mode=$m --sec=2 >> $L/c9.cycle.log 2>&1; done
        [ $k -eq 2 ] && rss_a=$(rss_kb $SV)
    done
    back_to_base "(라) 접속 160개를 열고 닫은 뒤"
    rss_b=$(rss_kb $SV)
    grow=$(( ${rss_b:-0} - ${rss_a:-0} ))
    [ $grow -lt 16384 ] || WHY="$WHY (라) 접속을 되풀이하는 동안 서버 메모리가 ${grow}kB 늘어남"
    echo "drip_max_ms=$dm cycle_rss=${rss_a}->${rss_b}kB fd=$BASE_FD->$(n_fd $SV) threads=$BASE_TH->$(n_thread $SV)" > $L/c9.numbers
    stop_server 9
    report 9 "접속이 16개를 넘거나 fd가 모자라거나 HELLO가 느림: 넘는 것만 거절하고 느린 것은 3초 뒤 끊음"
}

# ---- 10. 스레드 검사 ------------------------------------------------------------------
case_10() {
    begin
    T=build/tsan
    make B=$T LDFLAGS="-fsanitize=thread -pthread" \
        CFLAGS="-std=gnu11 -w -g -O1 -fno-omit-frame-pointer -fsanitize=thread -D_GNU_SOURCE -Iinclude -I. -MMD -MP -pthread" \
        $T/server $T/client > $L/c10.build.log 2>&1 || { WHY=" 빌드 실패(build/logs/fail/c10.build.log)"; report 10 "스레드 검사"; return; }
    # 서버는 자기 옆의 스테이지와 뇌를 띄운다. 여기서는 서버와 클라이언트의 스레드만 보므로 그 둘은 보통 빌드를 옆에 복사해 둔다
    cp $B/stage_example $B/brain_proc $T/ || { WHY=" $T 에 스테이지와 뇌를 두지 못함"; report 10 "스레드 검사"; return; }
    export TSAN_OPTIONS="exitcode=66 log_path=$L/tsan"
    # 주소 배치가 무작위이면 이 검사 도구가 뜨지 못하는 커널이 있다. 그래서 끄고 띄운다
    NOASLR=""
    setarch "$(uname -m)" -R true 2>/dev/null && NOASLR="setarch $(uname -m) -R"
    SERVER_BIN="$NOASLR $T/server" server 10 --fast --seed=1 --order=example --rounds=2 --tick-ms=10 --ticks=150 \
        || WHY="$WHY 서버가 뜨지 않음"
    CLIENT_BIN="$NOASLR $T/client"
    join3 10
    wait_line $L/c10.watch.log 'PHASE CARD round=1' 15 || WHY="$WHY 판이 시작되지 않음"
    # 화면을 그리는 클라이언트는 터미널이 있어야 하므로 script로 가짜 터미널을 준다(관전자로 들어간다).
    # 키와 마우스 입력을 조금 흘려 넣고, 판이 끝난 것을 보면 q를 눌러 나가게 한다
    rc=0
    if command -v script > /dev/null 2>&1; then
        ( sleep 0.7; printf '\t'; sleep 0.5; printf 'a\033[A\033[<35;120;30M'; sleep 0.5; printf '\t'
          wait_line $L/c10.watch.log '^END' 60; sleep 0.3; printf 'q'; sleep 0.2 ) |
            script -qefc "stty cols 240 rows 70; $NOASLR $T/client --no-title --port=$PORT --name=HUMAN" /dev/null > $L/c10.human.log 2>&1 &
        H=$!
        echo $H >> $L/pids
        # 화면 클라이언트가 들어와 그리기 시작할 때까지 기다린다(아래에서 접속 자리를 다 채우기 전에)
        _i=0
        while [ $_i -lt 300 ]; do
            _n=$( (wc -c < $L/c10.human.log) 2>/dev/null)
            [ "${_n:-0}" -ge 20000 ] && break
            sleep 0.05
            _i=$((_i + 1))
        done
    else
        H=0
        echo "script 명령이 없어 화면 클라이언트는 돌리지 않았습니다" > $L/c10.human.log
    fi
    # 게임 중에 들어왔다 나가는 접속, 거절되는 접속, 잘못된 접속도 같이 겪게 한다
    peer 10 spec --mode=watch --name=SPEC --sec=30; SP=$LAST
    $PEER --port=$PORT --mode=fill --n=20 --sec=0.5 > $L/c10.fill.log 2>&1
    $PEER --port=$PORT --mode=version > $L/c10.version.log 2>&1
    $PEER --port=$PORT --mode=cut > $L/c10.cut.log 2>&1
    kill -9 $SP 2>/dev/null
    wait $SP 2>/dev/null
    wait_exit $B1 60; r1=$?
    wait_exit $B2 60; r2=$?
    wait_exit $W 60; rw=$?
    hb=0
    if [ $H -gt 0 ]; then
        wait_exit $H 30; rc=$?
        hb=$(wc -c < $L/c10.human.log)
        [ $rc -eq 0 ] || WHY="$WHY 화면 클라이언트=$rc"
        [ $hb -ge 100000 ] || WHY="$WHY 화면 클라이언트가 게임 화면을 그리지 못함(출력 ${hb}바이트)"
    fi
    # 서버는 SIGTERM으로 끝낸다. 경고가 있었으면 끝나는 값이 66이다
    kill -TERM $SV 2>/dev/null
    wait_exit $SV 30; rs=$?
    unset TSAN_OPTIONS CLIENT_BIN
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rw -eq 0 ] && [ $rs -eq 0 ] || WHY="$WHY 봇=$r1,$r2 관찰자=$rw 서버=$rs"
    n=$(cat $L/tsan.* 2>/dev/null | grep -c 'WARNING: ThreadSanitizer')
    [ "$n" -eq 0 ] || WHY="$WHY 경고 ${n}건(build/logs/fail/tsan.*)"
    echo "tsan 경고=$n 화면 클라이언트 종료값=$rc 출력=${hb}바이트" > $L/c10.numbers
    report 10 "스레드 검사(ThreadSanitizer): 서버, 봇 2명, 화면 클라이언트로 한 판"
}

# ---- 실행 ------------------------------------------------------------------------
for c in $CASES; do
    if [ -n "$FAIL_SERIAL" ]; then case_$c; else case_$c & fi
done
wait
bad=0
total=0
for c in $CASES; do
    total=$((total + 1))
    if [ -f $L/c$c.result ]; then cat $L/c$c.result; else echo "실패 $c 결과가 없음"; fi
    grep -q '^통과' $L/c$c.result 2>/dev/null || bad=$((bad + 1))
done
se=$(( $(stage_errs) - SE0 ))
if [ $se -gt 0 ]; then echo "실패 스테이지 로그에 검사 오류 ${se}줄 (build/logs/stage_*.log)"; else echo "통과 스테이지 로그에 검사 오류 없음"; fi
echo "검사(실패 상황) ${total}개 중 $((total - bad))개 통과 ($(( $(date +%s) - T0 ))초)"
[ $bad -eq 0 ] && [ $se -le 0 ]
