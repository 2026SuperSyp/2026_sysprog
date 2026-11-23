#!/bin/sh
# 뇌 프로세스 검사. 서버가 뇌를 따로 띄우고, 죽거나 멈추면 다시 띄우고, 끝날 때 치우는지 본다.
#   sh tests/brain.sh [포트]      포트부터 4개를 쓴다(기본 4694~4697)
# 검사
#   0. 서버 없이: 뇌 프로세스로 돌린 판의 결과가 같은 프로세스 안의 뇌로 돌린 판과 같다
#   a. 서버 + 봇 2명이 랭킹까지 가고, 초파리가 실제로 반응한다(자극 틱과 반응 틱이 0이 아니고 늦은 틱이 없다)
#   b. 판 도중에 뇌를 kill -9 해도 랭킹까지 가고, 서버가 뇌를 다시 띄운다
#   c. 판 도중에 뇌를 SIGSTOP으로 멈추면 감시 스레드가 알아채고 서버가 다시 띄운다
#   d. 서버가 끝난 뒤(정상 종료든 SIGTERM이든) 뇌와 스테이지 프로세스, /dev/shm 의 공유 메모리가 남지 않는다
# 스테이지는 판이 끝날 때 로그에 "BRAIN shm=... stim_ticks=... fired_ticks=... late=..." 한 줄을 남긴다.
PORT=${1:-4694}
B=${SSP_BUILD:-build/dev}         # 다른 빌드를 보려면 SSP_BUILD=build/release 처럼 준다
L=build/logs
mkdir -p $L
: > $L/stage_example.log
fail=0

ok()  { echo "통과 brain $1"; }
bad() { echo "실패 brain $1"; fail=$((fail + 1)); }

# start_game <이름> <포트> <라운드 수> : 서버와 봇 2명을 띄운다. pid는 SV, B1, B2에 둔다
start_game() {
    $B/server --ranking=$L/brain_ranking.txt --port=$2 --fast --once --seed=1 --order=example --rounds=$3 --boss=0 > $L/brain_$1_server.log 2>&1 &
    SV=$!
    sleep 1.2                # 뜬 지 1초가 안 된 뇌가 죽으면 서버가 간격을 두고 다시 띄운다. 그 경우를 피한다
    timeout 40 $B/client --bot --port=$2 --name=BOT1 --wait=2 > $L/brain_$1_bot1.log 2>&1 &
    B1=$!
    sleep 0.2
    timeout 40 $B/client --bot --port=$2 --name=BOT2 --wait=2 > $L/brain_$1_bot2.log 2>&1 &
    B2=$!
}

# 봇과 서버가 끝나기를 기다린다. 종료 코드는 r1, r2, rs에 둔다. 서버가 안 끝나면 죽이고 99로 한다
finish_game() {
    wait $B1; r1=$?
    wait $B2; r2=$?
    i=0
    while kill -0 $SV 2>/dev/null && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
    if kill -0 $SV 2>/dev/null; then kill -9 $SV; wait $SV; rs=99; else wait $SV; rs=$?; fi
}

# 첫 스테이지 프로세스가 뜨고(판이 시작되고) 조금 지날 때까지 기다린다.
# 그때 떠 있는 뇌와 스테이지의 수를 seen에 둔다
wait_playing() {
    i=0; seen=0
    while [ $i -lt 400 ]; do
        if [ -n "$(pgrep -P $SV -f stage_)" ]; then seen=$(heirs); sleep 0.25; return 0; fi
        sleep 0.02; i=$((i + 1))
    done
    return 1
}

brain_pid() { pgrep -P $SV -x brain_proc | head -n 1; }

# 이 서버의 공유 메모리 이름을 물려받은 프로세스(뇌, 스테이지)의 수
heirs() { grep -lza "^SSP_BRAIN_SHM=/ssp_brain_$SV\$" /proc/[0-9]*/environ 2>/dev/null | wc -l; }

# d: 서버가 끝난 뒤 남은 것이 없는지 본다
check_clean() {
    i=0
    while [ "$(heirs)" != "0" ] && [ $i -lt 40 ]; do sleep 0.05; i=$((i + 1)); done
    n=$(heirs)
    if [ "$n" = "0" ] && [ ! -e /dev/shm/ssp_brain_$SV ]; then
        ok "d($1) 서버가 끝난 뒤 남은 프로세스와 공유 메모리가 없다"
    else
        bad "d($1) 남은 프로세스 $n개, 공유 메모리: $(ls /dev/shm | grep "ssp_brain_$SV\$")"
    fi
}

# 이 서버가 띄운 스테이지들이 남긴 BRAIN 줄에서 값을 모은다. lowest <이름>, highest <이름>
values()  { grep "^BRAIN shm=/ssp_brain_$SV " $L/stage_example.log | tr ' ' '\n' | sed -n "s/^$1=//p"; }
lowest()  { values $1 | sort -n | head -n 1; }
highest() { values $1 | sort -n | tail -n 1; }
last()    { values $1 | tail -n 1; }

# ---- 0. 서버 없이 결과 비교 -----------------------------------------------------
name=/ssp_test_brain_$$
$B/brain_proc --shm=$name --model=fast --own 2> $L/brain_0_brain.log &
BP=$!
i=0
while [ ! -e /dev/shm$name ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
same=1
for seed in 1 2; do
    a=$($B/stage_example --bot --seed=$seed --brain=fast 2>/dev/null)
    b=$(SSP_BRAIN_SHM=$name SSP_BRAIN_WAIT=1 $B/stage_example --bot --seed=$seed --brain=fast 2>> $L/brain_0_stage.log)
    [ -n "$a" ] && [ "$a" = "$b" ] || same=0
done
kill -TERM $BP; wait $BP; rb=$?
if [ $same -eq 1 ] && [ $rb -eq 0 ] && [ ! -e /dev/shm$name ]; then
    ok "0 뇌 프로세스로 돌린 결과가 같은 프로세스 안의 뇌와 같다"
else
    bad "0 결과 같음=$same 뇌 종료 코드=$rb"
fi

# ---- a. 끝까지 가고 초파리가 반응한다 --------------------------------------------
start_game a $PORT 2
finish_game
stim=$(lowest stim_ticks); fired=$(lowest fired_ticks); late=$(highest late)
if [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ] && [ "${stim:-0}" -gt 0 ] && [ "${fired:-0}" -gt 0 ] && [ "$late" = "0" ]; then
    ok "a 랭킹까지 갔고 초파리가 반응했다 (판마다 자극 틱 ${stim} 이상, 반응 틱 ${fired} 이상, 늦은 틱 0): $(cat $L/brain_a_bot1.log)"
else
    bad "a 봇1=$r1 봇2=$r2 서버=$rs 자극 틱=$stim 반응 틱=$fired 늦은 틱=$late"
    tail -n 5 $L/brain_a_server.log
fi
check_clean a

# ---- b. 판 도중에 뇌를 죽인다 ----------------------------------------------------
start_game b $((PORT + 1)) 3
wait_playing; old=$(brain_pid)
[ -n "$old" ] && kill -9 $old
finish_game
again=$(grep -c "뇌를 다시 띄웠습니다" $L/brain_b_server.log)
fired=$(last fired_ticks); count=$(last restarts)
if [ -n "$old" ] && [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ] && [ "$again" -ge 1 ] && [ "${fired:-0}" -gt 0 ] && [ "${count:-0}" -ge 1 ]; then
    ok "b kill -9 뒤에도 랭킹까지 갔고 뇌를 다시 띄웠다 (늦은 틱 최대 $(highest late)): $(grep "뇌를 다시 띄웠습니다" $L/brain_b_server.log | head -n 1)"
else
    bad "b 봇1=$r1 봇2=$r2 서버=$rs 다시 띄운 횟수=$again 마지막 판 반응 틱=$fired"
    tail -n 5 $L/brain_b_server.log
fi
check_clean b

# ---- c. 판 도중에 뇌를 멈춘다 ----------------------------------------------------
start_game c $((PORT + 2)) 3
wait_playing; old=$(brain_pid)
[ -n "$old" ] && kill -STOP $old
finish_game
again=$(grep -c "뇌를 다시 띄웠습니다.*멈춤" $L/brain_c_server.log)
fired=$(last fired_ticks); count=$(last restarts); late=$(highest late)
# 멈춘 동안 스테이지는 틱마다 뇌를 기다려 준다. 늦은 틱은 몇 개에 그쳐야 한다(한 판이 통째로 지나가면 안 된다)
if [ -n "$old" ] && [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $rs -eq 0 ] && [ "$again" -ge 1 ] && [ "${fired:-0}" -gt 0 ] && [ "${count:-0}" -ge 1 ] && [ "${late:-999}" -lt 20 ]; then
    ok "c SIGSTOP 뒤에 감시 스레드가 알아채고 다시 띄웠다 (늦은 틱 최대 $late): $(grep "뇌를 다시 띄웠습니다" $L/brain_c_server.log | head -n 1)"
else
    bad "c 봇1=$r1 봇2=$r2 서버=$rs 멈춤으로 다시 띄운 횟수=$again 마지막 판 반응 틱=$fired 늦은 틱 최대=$late"
    tail -n 5 $L/brain_c_server.log
fi
# 서버가 못 죽였을 때 멈춘 뇌가 남지 않게 한다
[ "$(cat /proc/$old/comm 2>/dev/null)" = "brain_proc" ] && kill -9 $old
check_clean c

# ---- d. 판 도중에 서버를 SIGTERM으로 끝낸다 --------------------------------------
start_game d $((PORT + 3)) 3
wait_playing
kill -TERM $SV
finish_game
if [ "$seen" -ge 2 ] && [ $rs -eq 0 ]; then
    ok "d SIGTERM으로 서버가 정상 종료했다 (그 전에 떠 있던 뇌와 스테이지 ${seen}개)"
else
    bad "d 서버 종료 코드=$rs 그 전에 떠 있던 프로세스=$seen"
fi
check_clean term

if [ $fail -eq 0 ]; then
    echo "통과 brain 전부"
    exit 0
fi
echo "실패 brain $fail건. 로그: $L/brain_*_server.log $L/brain.log $L/stage_example.log"
exit 1
