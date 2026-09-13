# GoldenEye 007 (Wii, SJBE52) static recompilation — the whole pipeline.
#
#   make            names -> recompile -> build (build/out/wiiboot)
#   make run        boot the game in a window
#   make smoke      boot headless for SMOKE_SECS seconds, log to build/run.log
#   make names      mix the symbol sources into build/names.tsv
#   make deps       build SDL3 into build/deps if the system has no SDL3 dev files
#   make clean      remove generated code and objects (keeps build/deps)
#
# The disc's contents are expected in ./game (sys/main.dol, files/, ...).

GAME      ?= game
BUILD     := build
RECOMP    := $(BUILD)/recomp
OUT       := $(BUILD)/out
SDL3_DIR  := $(abspath $(BUILD)/deps/sdl3)
FONTS     ?= $(firstword $(wildcard /usr/share/dolphin-emu/Sys/GC $(BUILD)/fonts))
DSY       ?= $(firstword $(wildcard /usr/share/dolphin-emu/Sys/totaldb.dsy /usr/share/dolphin-emu/sys/totaldb.dsy))
RECOMP_CXX ?= clang++
SMOKE_SECS ?= 30
RUN_ARGS  ?=
PY        := python3

NAMES     := $(BUILD)/names.tsv
SYMSRC    := $(wildcard config/symbols/*) config/names-manual.tsv tools/merge_symbols.py
WIIBOOT   := $(OUT)/wiiboot
BOOT_ARGS  = $(GAME) --nand $(BUILD)/nand --keys $(BUILD)/keys.txt --symbols $(RECOMP)/symbols.tsv $(if $(FONTS),--fonts $(FONTS))

.PHONY: all names recomp build run smoke deps clean check-game
all: build

check-game:
	@test -f $(GAME)/sys/main.dol || { echo "no $(GAME)/sys/main.dol: extract your own disc into ./$(GAME)"; exit 1; }
	@test -f wiikit/recomp/__main__.py || git submodule update --init

# Dolphin's signature database names SDK functions the symbol files lack (optional)
$(BUILD)/sig_guess.tsv: tools/sigmatch.py | check-game
	@mkdir -p $(BUILD)
	@if [ -n "$(DSY)" ]; then $(PY) tools/sigmatch.py $(GAME)/sys/main.dol --dsy $(DSY) --out $@ | tail -1; \
	 else echo "no Dolphin totaldb.dsy (set DSY=...): signatures skipped"; echo "addr	size	name	source	alternatives" > $@; fi

# Changes to the wiikit submodule live in patches/wiikit.patch (applied when
# the submodule is pristine). After editing wiikit: make wiikit-save
.PHONY: wiikit-patch wiikit-save
wiikit-patch: | check-game
	@if [ -s patches/wiikit.patch ] && git -C wiikit diff --quiet; then \
	    git -C wiikit apply ../patches/wiikit.patch && echo "applied patches/wiikit.patch"; fi
wiikit-save:
	@mkdir -p patches && git -C wiikit diff > patches/wiikit.patch && echo "patches/wiikit.patch: $$(grep -c '^diff' patches/wiikit.patch) file(s)"

names: $(NAMES)
$(NAMES): $(SYMSRC) $(BUILD)/sig_guess.tsv | check-game
	$(PY) tools/merge_symbols.py --dol $(GAME)/sys/main.dol --out $@

recomp: $(RECOMP)/report.txt
$(RECOMP)/report.txt: $(NAMES) config/hooks.txt $(wildcard wiikit/recomp/*.py wiikit/*.py wiikit/runtime/hooks.txt)
	$(PY) -m wiikit.recomp $(GAME)/sys/main.dol --out $(RECOMP) --symbols $(NAMES) \
	    --hooks config/hooks.txt --no-comments > $(BUILD)/recomp.log 2>&1 || { tail -20 $(BUILD)/recomp.log; exit 1; }
	@grep -v '^  hook: no symbol' $(BUILD)/recomp.log | tail -12
	@echo "hooks the executable has no name for yet: $$(grep -c 'hook: no symbol' $(BUILD)/recomp.log) (see $(BUILD)/recomp.log)"

deps:
	@if ! pkg-config --exists sdl3 && [ ! -d $(SDL3_DIR)/lib*/cmake/SDL3 ]; then sh tools/build_sdl3.sh $(BUILD)/deps; fi

build: wiikit-patch recomp deps
	cmake -S $(RECOMP) -B $(OUT) -G Ninja -DCMAKE_CXX_COMPILER=$(RECOMP_CXX) \
	    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE=-O1 \
	    -DCMAKE_PREFIX_PATH=$(SDL3_DIR) -DCMAKE_BUILD_RPATH=$(SDL3_DIR)/lib \
	    -DWIIKIT_EXTRA=$(abspath port/goldeneye.cmake) > $(BUILD)/cmake.log 2>&1 || { tail -20 $(BUILD)/cmake.log; exit 1; }
	ninja -C $(OUT) wiiboot

run: build
	$(WIIBOOT) $(BOOT_ARGS) $(RUN_ARGS)

# Headless, time-boxed: the loop's progress check. Never fails the make; read the log.
smoke: build
	-timeout $$(( $(SMOKE_SECS) + 20 )) $(WIIBOOT) $(BOOT_ARGS) --no-video --no-audio --watch 5 \
	    --quit-after $(SMOKE_SECS) $(RUN_ARGS) > $(BUILD)/run.log 2>&1; echo "exit $$?" >> $(BUILD)/run.log
	@tail -40 $(BUILD)/run.log

clean:
	rm -rf $(RECOMP) $(OUT) $(NAMES) $(BUILD)/names.*
