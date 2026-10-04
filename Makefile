# GoldenEye 007 (Wii, SJBE52) static recompilation — the whole pipeline.
#
#   make            names -> recompile -> build (build/out/wiiboot)
#   make run        boot the game in a window
#   make smoke      boot headless for SMOKE_SECS seconds, log to build/run.log
#   make names      mix the symbol sources into build/names.tsv
#   make deps       build SDL3 into build/deps if the system has no SDL3 dev files
#   make win        the Windows build, cross-compiled and zipped: build/GoldenEye007-win64.zip
#   make linux      the Linux build to hand out, zipped: build/GoldenEye007-linux64.zip
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
# optimisation of the recompiled code and the runtime: -O1 builds in a minute,
# -O2 runs faster. Plain -O2 miscompiles something (the game panics in
# DVDReadAsync at boot): the two flags are needed with it
# -march=x86-64-v3: any x86-64 CPU since about 2015 (AVX2, BMI, MOVBE: the
# guest's big-endian loads in one instruction), not this machine's alone;
# ARCH= for an older one
ARCH      ?= -march=x86-64-v3
# profile-guided optimisation, when a profile has been made (make pgo): about a tenth faster
PGO       := $(wildcard $(BUILD)/pgo/ge.profdata)
PGO_USE   := $(if $(PGO),-fprofile-instr-use=$(abspath $(PGO)) -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date)
OPT_BASE  ?= -O2 -fno-strict-aliasing -fwrapv $(ARCH)
OPT       ?= $(OPT_BASE) $(PGO_USE)
# the recompiler's own options (--no-cr-live, --cr-check: wiikit/recomp/crlive.py)
RECOMP_FLAGS ?=
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
	@if [ -s patches/wiikit.patch ] && git -C wiikit apply --check ../patches/wiikit.patch 2>/dev/null; then \
	    git -C wiikit apply ../patches/wiikit.patch && echo "applied patches/wiikit.patch"; fi
wiikit-save:
	@mkdir -p patches && git -C wiikit add -N . && git -C wiikit diff > patches/wiikit.patch && \
	    echo "patches/wiikit.patch: $$(grep -c '^diff' patches/wiikit.patch) file(s)"

names: $(NAMES)
$(NAMES): $(SYMSRC) $(BUILD)/sig_guess.tsv | check-game
	$(PY) tools/merge_symbols.py --dol $(GAME)/sys/main.dol --out $@

# Gecko codes: the executable the recompiler reads has them applied
PATCHED   := $(BUILD)/patched
$(PATCHED)/main.dol: config/gecko.txt tools/gecko.py | check-game
	$(PY) tools/gecko.py $(GAME)/sys/main.dol config/gecko.txt --out $(PATCHED)

recomp: $(RECOMP)/report.txt
$(RECOMP)/report.txt: $(NAMES) $(PATCHED)/main.dol config/hooks.txt $(wildcard wiikit/recomp/*.py wiikit/*.py wiikit/runtime/hooks.txt)
	$(PY) -m wiikit.recomp $(PATCHED)/main.dol --out $(RECOMP) --symbols $(NAMES) \
	    --hooks config/hooks.txt --no-comments $(RECOMP_FLAGS) > $(BUILD)/recomp.log 2>&1 || { tail -20 $(BUILD)/recomp.log; exit 1; }
	@grep -v '^  hook: no symbol' $(BUILD)/recomp.log | tail -12
	@echo "hooks the executable has no name for yet: $$(grep -c 'hook: no symbol' $(BUILD)/recomp.log) (see $(BUILD)/recomp.log)"

deps:
	@if ! pkg-config --exists sdl3 && [ ! -d $(SDL3_DIR)/lib*/cmake/SDL3 ]; then sh tools/build_sdl3.sh $(BUILD)/deps; fi
	@[ -d $(BUILD)/deps/imgui ] || sh tools/get_imgui.sh $(BUILD)/deps 2>/dev/null || echo "no Dear ImGui: the debug overlay is left out"

build: wiikit-patch recomp deps
	cmake -S $(RECOMP) -B $(OUT) -G Ninja -DCMAKE_CXX_COMPILER=$(RECOMP_CXX) \
	    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="$(OPT)" \
	    -DCMAKE_PREFIX_PATH=$(SDL3_DIR) -DCMAKE_BUILD_RPATH=$(SDL3_DIR)/lib \
	    -DWIIKIT_EXTRA=$(abspath port/goldeneye.cmake) > $(BUILD)/cmake.log 2>&1 || { tail -20 $(BUILD)/cmake.log; exit 1; }
	ninja -C $(OUT) wiiboot

# Windows, 64-bit: cross-compiled with llvm-mingw (fetched into build/deps), run from its own
# folder (GE_PORTABLE), packaged by tools/package_win.sh without anything of the disc
.PHONY: win
WIN_OUT   := $(BUILD)/out-win
win: wiikit-patch recomp deps
	@sh tools/get_mingw.sh $(BUILD)/deps
	@sh tools/get_winpy.sh $(BUILD)/deps
	@[ -f $(BUILD)/deps/sdl3-win/bin/SDL3.dll ] || sh tools/build_sdl3.sh $(BUILD)/deps win
	@[ -f $(BUILD)/deps/openssl-win/lib/libssl.a ] || sh tools/build_openssl.sh $(BUILD)/deps
	cmake -S $(RECOMP) -B $(WIN_OUT) -G Ninja -DCMAKE_TOOLCHAIN_FILE=$(abspath tools/mingw.cmake) \
	    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="$(OPT_BASE)" -DCMAKE_EXE_LINKER_FLAGS=-static \
	    -DCMAKE_PREFIX_PATH=$(abspath $(BUILD)/deps/sdl3-win) -DGE_PORTABLE=ON \
	    -DOPENSSL_ROOT_DIR=$(abspath $(BUILD)/deps/openssl-win) -DOPENSSL_USE_STATIC_LIBS=ON \
	    -DWIIKIT_EXTRA=$(abspath port/goldeneye.cmake) > $(BUILD)/cmake-win.log 2>&1 || { tail -20 $(BUILD)/cmake-win.log; exit 1; }
	ninja -C $(WIN_OUT) wiiboot
	sh tools/package_win.sh $(FONTS)

# Linux, to hand out: the same portable layout as the Windows package (GE_PORTABLE), SDL3 beside
# the game in lib/, the C++ runtime linked in, packaged by tools/package_linux.sh
.PHONY: linux
LINUX_OUT := $(BUILD)/out-linux
linux: wiikit-patch recomp deps
	@[ -d $(SDL3_DIR)/lib*/cmake/SDL3 ] || sh tools/build_sdl3.sh $(BUILD)/deps
	cmake -S $(RECOMP) -B $(LINUX_OUT) -G Ninja -DCMAKE_CXX_COMPILER=$(RECOMP_CXX) \
	    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="$(OPT)" \
	    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc -Wl,-rpath,'\$$ORIGIN/lib'" \
	    -DCMAKE_PREFIX_PATH=$(SDL3_DIR) -DCMAKE_SKIP_RPATH=ON -DGE_PORTABLE=ON \
	    -DWIIKIT_EXTRA=$(abspath port/goldeneye.cmake) > $(BUILD)/cmake-linux.log 2>&1 || { tail -20 $(BUILD)/cmake-linux.log; exit 1; }
	ninja -C $(LINUX_OUT) wiiboot
	sh tools/package_linux.sh $(FONTS)

# the key file: the port's defaults, copied once; build/keys.txt is the player's to edit
$(BUILD)/keys.txt: | config/keys.txt
	@mkdir -p $(BUILD) && cp config/keys.txt $@

run: build $(BUILD)/keys.txt
	$(or $(GE_BIN),$(WIIBOOT)) $(BOOT_ARGS) $(RUN_ARGS)

# Headless, time-boxed: the loop's progress check. Never fails the make; read the log.
smoke: build $(BUILD)/keys.txt
	-timeout $$(( $(SMOKE_SECS) + 20 )) $(WIIBOOT) $(BOOT_ARGS) --no-video --no-audio --watch 5 \
	    --quit-after $(SMOKE_SECS) $(RUN_ARGS) > $(BUILD)/run.log 2>&1; echo "exit $$?" >> $(BUILD)/run.log
	@tail -40 $(BUILD)/run.log

# Profile-guided optimisation: an instrumented build is played for a while,
# and what it counted steers the real build.
#   make pgo-build     the instrumented game, in build/out-gen (slow: about half speed)
#   GE_BIN=build/out-gen/wiiboot ./launch.sh     play a few minutes of a mission, then quit
#   make pgo-merge     the counts become build/pgo/ge.profdata; make then uses it
#   make pgo-clean     back to a build without it
.PHONY: pgo-build pgo-merge pgo-clean
pgo-build:
	@mkdir -p $(BUILD)/pgo
	$(MAKE) build OUT=$(BUILD)/out-gen OPT="$(OPT_BASE) -fprofile-instr-generate=$(abspath $(BUILD)/pgo/ge.profraw)"
pgo-merge:
	llvm-profdata merge -o $(BUILD)/pgo/ge.profdata $(BUILD)/pgo/ge.profraw
	@echo "profile ready: run make"
pgo-clean:
	rm -rf $(BUILD)/pgo $(BUILD)/out-gen

clean:
	rm -rf $(RECOMP) $(OUT) $(NAMES) $(BUILD)/names.*
