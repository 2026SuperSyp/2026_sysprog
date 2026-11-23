# 슈퍼 시프 파티 빌드 규칙
#
#   make                         전부 빌드
#   make test                    자동 검사
#   make run STAGE=이름          스테이지 하나를 3분할 화면으로 실행 (SEED=, PLAYERS=, BRAIN=fast|slow|real)
#   make bot STAGE=이름          화면 없이 여러 판을 돌려 검사
#   make screen SCREEN=이름      화면 하나를 예시 상태로 띄움 (CASE=)
#   make log STAGE=이름          스테이지 로그를 따라 읽음
#   make termbench               터미널 속도 측정
#   make server                  서버를 띄움 (PORT=, ORDER=이름,이름, BRAIN=)
#   make client                  서버에 접속 (HOST=, PORT=, NAME=)
#   make demo-local              한 컴퓨터에서 서버, 봇 2명, 사람 1명으로 한 판
#   make brain-data              진짜 뇌에 필요한 자료를 받아 data/ 에 준비 (한 번만)
#   make brainprobe              진짜 뇌에 자극을 넣어 보고 반응과 계산 시간을 표로 봄
#
# 경로는 전부 상대 경로다. 저장소 폴더 이름에 공백이 있어도 깨지지 않게 하려는 것이다.

.DEFAULT_GOAL := all

# 환경 변수에서 온 값은 쓰지 않는다. WSL에는 NAME 같은 환경 변수가 이미 있다.
define only_from_cmdline
ifeq ($$(origin $(1)),environment)
override $(1) :=
endif
endef
$(foreach v,STAGE SEED PLAYERS SCREEN CASE BRAIN HOST PORT ORDER NAME,$(eval $(call only_from_cmdline,$(v))))

CC      := gcc
B       := build/dev
CFLAGS  := -std=gnu11 -Wall -Wextra -g -O1 -fno-omit-frame-pointer \
           -fsanitize=address,undefined -fno-sanitize-recover=undefined \
           -D_GNU_SOURCE -Iinclude -I. -MMD -MP -pthread
LDFLAGS := -fsanitize=address,undefined -pthread
LDLIBS  := -lm

# 터미널을 쓰는 프로그램이 어떻게 끝났든 터미널을 되돌린다.
RESTORE := stty sane 2>/dev/null; printf '\033[?1003l\033[?1006l\033[?25h\033[?1049l\033[0m'

# ---- 소스 목록 -------------------------------------------------------------
KIT_SRC    := $(filter-out brain/brain_proc.c,$(wildcard ui/*.c brain/*.c core/log.c core/cfg.c core/rng.c) core/ranking.c core/toplist.c)
HOST_SRC   := $(wildcard core/stage_host.c core/stage_main.c core/host_bot.c core/host_local.c core/host_pipe.c \
                         core/default_screens.c core/scenes.c core/msg.c core/frame.c)
NET_SRC    := $(wildcard core/server.c core/client.c core/brain_sup.c brain/brain_proc.c)
SCREEN_SRC := $(wildcard screens/*.c)
TEST_SRC   := $(wildcard tests/test_*.c)
STAGES     := $(notdir $(patsubst %/,%,$(wildcard stages/*/)))
STAGE_SRC  := $(wildcard stages/*/*.c)

KIT_OBJ    := $(patsubst %.c,$(B)/%.o,$(KIT_SRC))
HOST_OBJ   := $(patsubst %.c,$(B)/%.o,$(HOST_SRC))
SCREEN_OBJ := $(patsubst %.c,$(B)/%.o,$(SCREEN_SRC))
TEST_BIN   := $(patsubst tests/%.c,$(B)/tests/%,$(TEST_SRC))

# 실행기가 있을 때만 스테이지 실행 파일을 만든다.
ifneq ($(wildcard core/stage_main.c),)
STAGE_BIN  := $(addprefix $(B)/stage_,$(STAGES))
endif
ifneq ($(wildcard tools/screen_preview.c),)
TOOL_BIN   += $(B)/screen_preview
endif
ifneq ($(wildcard tools/termbench.c),)
TOOL_BIN   += $(B)/termbench
endif
ifneq ($(wildcard core/server.c),)
TOOL_BIN   += $(B)/server $(B)/client $(B)/brain_proc
endif
ifneq ($(wildcard tools/brainprobe.c),)
TOOL_BIN   += $(B)/brainprobe
endif

ALL_SRC := $(KIT_SRC) $(HOST_SRC) $(NET_SRC) $(SCREEN_SRC) $(TEST_SRC) $(STAGE_SRC) $(wildcard tools/*.c)

.PHONY: all test run bot screen log termbench brain-data brainprobe server client demo-local clean need-stage need-screen
all: $(KIT_OBJ) $(STAGE_BIN) $(TOOL_BIN)

# ---- 컴파일 ----------------------------------------------------------------
# 스테이지와 화면 코드는 쓰지 않는 인자가 흔하다(t, dt 등). 그 경고는 끈다.
$(B)/stages/%.o $(B)/screens/%.o: CFLAGS += -Wno-unused-parameter

# 뇌 시뮬레이터는 속도가 중요하다. 검사 옵션 없이 최적화해서 컴파일한다.
$(B)/brain/lif.o: CFLAGS := -std=gnu11 -Wall -Wextra -g -O2 -D_GNU_SOURCE -Iinclude -I. -MMD -MP

$(B)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

# 스테이지마다 실행 파일 하나. 목적 파일을 그대로 링크한다(보관 파일로 묶지 않는다).
define STAGE_RULE
$(B)/stage_$(1): $$(HOST_OBJ) $$(KIT_OBJ) $$(SCREEN_OBJ) $$(patsubst %.c,$(B)/%.o,$$(wildcard stages/$(1)/*.c))
	@mkdir -p $$(@D)
	$$(CC) $$(LDFLAGS) -o $$@ $$^ $$(LDLIBS)
endef
$(foreach s,$(STAGES),$(eval $(call STAGE_RULE,$(s))))

$(B)/screen_preview: $(B)/tools/screen_preview.o $(B)/core/default_screens.o $(KIT_OBJ) $(SCREEN_OBJ)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/termbench: $(B)/tools/termbench.o $(KIT_OBJ)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/server: $(B)/core/server.o $(B)/core/brain_sup.o $(B)/core/msg.o $(KIT_OBJ)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# 뇌 프로세스. 서버가 띄운다. 모델(가짜 뇌, 진짜 뇌)과 공유 메모리 부분만 링크한다.
# 실행 파일 이름이 brain이 아닌 것은 build/dev/brain 이 목적 파일 폴더이기 때문이다.
$(B)/brain_proc: $(addprefix $(B)/brain/,brain_proc.o brain_model.o brain_real.o lif.o brain_shm.o)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/client: $(B)/core/client.o $(B)/core/msg.o $(B)/core/frame.o $(B)/core/scenes.o \
             $(B)/core/default_screens.o $(KIT_OBJ) $(SCREEN_OBJ)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/brainprobe: $(B)/tools/brainprobe.o $(B)/brain/lif.o
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(B)/tests/%: $(B)/tests/%.o $(KIT_OBJ)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# ---- 실행 ------------------------------------------------------------------
test: $(TEST_BIN) $(STAGE_BIN) $(B)/server $(B)/client $(B)/brain_proc
	@sh tests/run.sh $(TEST_BIN)
	@sh tests/e2e.sh
	@sh tests/brain.sh

need-stage:
	@if [ -z "$(STAGE)" ]; then echo "STAGE=이름 을 주세요. 있는 스테이지: $(STAGES)"; exit 2; fi
	@if [ ! -d "stages/$(STAGE)" ]; then echo "stages/$(STAGE) 폴더가 없습니다. 있는 스테이지: $(STAGES)"; exit 2; fi

need-screen:
	@if [ -z "$(SCREEN)" ]; then echo "SCREEN=lobby|result|ranking|spectate 를 주세요"; exit 2; fi

run: need-stage
	@$(MAKE) --no-print-directory $(B)/stage_$(STAGE)
	@mkdir -p build/logs
	@$(B)/stage_$(STAGE) --local --seed=$(or $(SEED),1) --players=$(or $(PLAYERS),3) --brain=$(or $(BRAIN),fast) \
	    2> build/logs/stage_$(STAGE).log; st=$$?; $(RESTORE); \
	 n=$$(grep -c '^경고' build/logs/stage_$(STAGE).log); \
	 echo "끝 (종료 코드 $$st, 경고 $$n개). 로그: build/logs/stage_$(STAGE).log"; \
	 tail -n 10 build/logs/stage_$(STAGE).log; exit $$st

bot: need-stage
	@$(MAKE) --no-print-directory $(B)/stage_$(STAGE)
	@sh tools/bot.sh $(STAGE)

screen: need-screen
	@$(MAKE) --no-print-directory $(B)/screen_preview
	@$(B)/screen_preview --screen=$(SCREEN) --case=$(or $(CASE),1); st=$$?; $(RESTORE); exit $$st

termbench:
	@$(MAKE) --no-print-directory $(B)/termbench
	@$(B)/termbench; st=$$?; $(RESTORE); exit $$st

# 서버는 스테이지 실행 파일을 직접 띄우므로 먼저 전부 빌드한다.
server: all
	@$(B)/server --port=$(or $(PORT),4620) --order=$(or $(ORDER),example) --brain=$(or $(BRAIN),fast)

client: all
	@$(B)/client --host=$(or $(HOST),127.0.0.1) --port=$(or $(PORT),4620) --name=$(or $(NAME),PLAYER); \
	 st=$$?; $(RESTORE); exit $$st

demo-local: all
	@mkdir -p build/logs
	@$(B)/server --port=$(or $(PORT),4620) --order=$(or $(ORDER),example) --brain=$(or $(BRAIN),fast) \
	    > build/logs/server.log 2>&1 & sv=$$!; sleep 0.5; \
	 $(B)/client --bot --wait=3 --port=$(or $(PORT),4620) --name=BOT1 > build/logs/bot1.log 2>&1 & \
	 sleep 0.2; $(B)/client --bot --wait=3 --port=$(or $(PORT),4620) --name=BOT2 > build/logs/bot2.log 2>&1 & \
	 sleep 0.2; $(B)/client --port=$(or $(PORT),4620) --name=$(or $(NAME),YOU); st=$$?; $(RESTORE); \
	 kill $$sv 2>/dev/null; exit $$st

brain-data:
	@sh tools/brain_data.sh

brainprobe:
	@$(MAKE) --no-print-directory $(B)/brainprobe
	@$(B)/brainprobe data

log: need-stage
	@tail -n 50 -f build/logs/stage_$(STAGE).log

clean:
	rm -rf build

-include $(patsubst %.c,$(B)/%.d,$(ALL_SRC))
