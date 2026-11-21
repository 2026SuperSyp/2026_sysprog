# 팀원 안내

스테이지(미니게임)와 화면을 만들 때 필요한 것만 적었다. 자세한 약속은 `include/`의 헤더 주석과 `docs/skeleton-design.md` 6장에 있다.

## 준비

```
wsl --install -d Ubuntu-24.04          (윈도우에서 한 번만)
sudo apt install build-essential git   (우분투에서 한 번만)
git clone https://github.com/2026SuperSyp/2026_sysprog.git ~/2026_sysprog
cd ~/2026_sysprog
```

코드는 우분투의 홈 폴더(`~/`)에 둔다. 윈도우 쪽 폴더에 두면 느리고 경로 문제로 빌드가 깨질 수 있다.

## 터미널 크기

게임 화면은 가로 240칸, 세로 67줄 이상이 필요하다. 터미널을 최대화하고 `Ctrl`+`-`를 몇 번 눌러 글자를 줄인다. `stty size`를 쳤을 때 `67 240` 이상이면 된다. 모자라면 게임 대신 안내가 뜬다.

## 명령

| 명령 | 하는 일 |
|---|---|
| `make run STAGE=example` | 스테이지를 실행한다. 왼쪽이 초파리, 가운데가 나다. 오른쪽 위에는 무작위로 움직이는 다른 참가자 한 명이 보이고 `Tab`으로 바꾼다. 오른쪽 아래는 초파리 뇌 표시다. `q`로 나간다 |
| `make run STAGE=example SEED=7 PLAYERS=1 BRAIN=slow` | 같은 문제로 다시, 한 명만, 느린 가짜 뇌로 |
| `make bot STAGE=example` | 화면 없이 여러 판을 돌려 검사한다. "통과"가 나와야 한다 |
| `make screen SCREEN=lobby CASE=2` | 화면 하나를 예시 상태로 띄운다(`lobby`, `result`, `ranking`, `spectate`). 아무 키나 누르면 끝난다 |
| `make log STAGE=example` | 스테이지의 `printf` 출력과 경고를 본다 |

## 스테이지를 새로 만드는 순서

1. `stages/example` 폴더를 자기 스테이지 이름으로 복사한다. 예: `cp -r stages/example stages/swat`
2. `make run STAGE=swat`이 도는지 본다. 다른 파일은 고치지 않아도 된다.
3. `stages/swat/` 안의 C 파일, `config.ini`, `assets/`만 고친다.
4. 올리기 전에 `make bot STAGE=swat`이 통과하는지 본다.

## 꼭 지킬 것

- 자기 폴더 밖은 고치지 않는다. 헤더(`include/`)를 바꿔야 하면 서진석에게 말한다.
- 유저마다 상태를 따로 둔다. 초파리 상태도 따로 둔다. 예제의 `side_t`를 참고한다.
- 시간은 틱 수로 센다. 한 틱은 0.05초이고 한 판은 `ctx->n_ticks`번 돈다. 시계를 읽거나 `sleep`하지 않는다.
- 난수는 `stage_rng_*`만 쓴다. `rand()`, `time()`은 쓰지 않는다.
- `stage_draw`에서는 그리기만 한다. 상태를 바꾸지 않는다. 초파리 칸에는 모두에게 같은 그림을 그린다.
- `brain_*`는 `stage_tick` 안에서만 부른다. `brain_fired`는 반응하는 동안 계속 1이므로, 한 번만 반응하게 하려면 쿨다운을 둔다.
- 이펙트(`fx_*`)는 사건이 난 틱에 `stage_tick`이나 `stage_input`에서 부른다. `stage_draw`에서 부르지 않는다.
- 난이도 숫자는 코드에 쓰지 않고 `config.ini`에 둔다.
- 점수는 0 이상이고, 한 판을 잘하면 100 근처가 되게 맞춘다.

## 팀 명세서 v6에서 바뀐 이름

| v6 | 지금 |
|---|---|
| `KEY_LEFT`, `KEY_RIGHT`, `KEY_UP`, `KEY_DOWN` | `SK_LEFT`, `SK_RIGHT`, `SK_UP`, `SK_DOWN` |
| `KEY_A`(스페이스), `KEY_B`(F) | `SK_ACT`, `SK_SUB` |
| `SUGAR`, `LOOM_L`, `ESCAPE` 등 | `BR_SUGAR`, `BR_LOOM_L`, `BR_ESCAPE` 등 |
| `fx_shake(캔버스, ...)` | `fx_shake(칸 번호, ...)`. 초파리 칸은 `PANE_FLY` |
| `make screen NAME=lobby` | `make screen SCREEN=lobby` |

## 아직 안 되는 것

- 한글 글자: 글꼴을 넣기 전이라 한글은 빈 네모로 보인다. 지금은 영문 대문자와 숫자를 쓴다.
- 서버 접속, 진짜 뇌, 보스전은 아직 없다. 지금은 혼자 돌려 보는 것만 된다.
