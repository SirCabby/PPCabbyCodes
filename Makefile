# PPCabbyCodes - a cheat panel for Papers, Please (Steam app 239030): Unity
# 2020.3 with IL2CPP. One mod, built twice:
#
#  - Linux (the native build: GameAssembly.so, OpenGL): it ships as
#    PapersPlease_Data/Plugins/libPPCabbyCodes.so. Unity's Linux player
#    preloads every library in that folder at start-up ("Preloaded '...'" in
#    Player.log), so the mod is in before any of the game's code runs.
#  - Windows (GameAssembly.dll, Direct3D 11), cross-compiled with mingw-w64: it
#    ships as hid.dll beside PapersPlease.exe. Unity's Windows player preloads
#    no plugin, but UnityPlayer.dll imports HID.DLL, which Windows looks for
#    beside the exe first: the mod stands in for it and forwards its functions
#    to Windows' own. No mod loader takes that name (MelonLoader is
#    version.dll there, BepInEx winhttp.dll), so the mod sits beside either.
#
# Either way no game file is renamed or replaced, and no launch options are
# needed (the Windows build under Proton aside - see README).

CXX      := g++
CC       := gcc
WIN_CXX  := x86_64-w64-mingw32-g++
WIN_CC   := x86_64-w64-mingw32-gcc
WIN_RC   := x86_64-w64-mingw32-windres

# The project name (used for the distributable), the libraries' names and the
# game executables are separate things.
PROJECT  := PPCabbyCodes
NAME     := libPPCabbyCodes
EXE      := PapersPlease
PLUGINS_SUBDIR := PapersPlease_Data/Plugins
WIN_NAME := hid
WIN_EXE  := PapersPlease.exe

# VERSION holds the single source of truth. Change it with `make version X.Y.Z`.
# With no VERSION file the build reports 0.0.0.
VERSION  := $(shell cat VERSION 2>/dev/null || echo 0.0.0)
BUILD    := build
TARGET   := $(BUILD)/$(NAME).so
WIN_BUILD := $(BUILD)/win
WIN_TARGET := $(WIN_BUILD)/$(WIN_NAME).dll

# Per-machine config (the game folders), kept out of git. Copy
# config.mk.example to config.mk and set GAME_DIR (and WIN_GAME_DIR) there. The
# paths may contain spaces, so they are only ever used quoted inside shell
# recipes below.
-include config.mk

ifneq (,$(filter version rev,$(MAKECMDGOALS)))
REV := $(strip $(filter-out version rev,$(MAKECMDGOALS)))
$(eval $(REV):;@:)
endif

# src/ is the mod, shared; src/linux/ and src/win/ are each build's own (how it
# gets into the game, its renderer, its hooks' memory, its platform calls).
COMMON_SRCS := $(wildcard src/*.cpp)
LINUX_SRCS  := $(COMMON_SRCS) $(wildcard src/linux/*.cpp)
WIN_SRCS    := $(COMMON_SRCS) $(wildcard src/win/*.cpp)

# Dear ImGui (vendored, MIT): the core plus a renderer backend - OpenGL 3 on
# Linux, Direct3D 11 on Windows (the platform side - input - is the mod's own,
# fed from Unity's Input).
IMGUI_DIR   := contrib/imgui
IMGUI_CORE  := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp \
               $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp
LINUX_IMGUI := $(IMGUI_CORE) $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
WIN_IMGUI   := $(IMGUI_CORE) $(IMGUI_DIR)/backends/imgui_impl_dx11.cpp
# Hacker Disassembler Engine 64 (vendored from MinHook, BSD): instruction
# lengths for the inline hooks' trampolines.
HDE_SRCS    := contrib/hde/hde64.c

LINUX_OBJS := $(patsubst src/%.cpp,$(BUILD)/linux/%.o,$(LINUX_SRCS)) \
              $(patsubst $(IMGUI_DIR)/%.cpp,$(BUILD)/linux/imgui/%.o,$(LINUX_IMGUI)) \
              $(patsubst contrib/hde/%.c,$(BUILD)/linux/hde/%.o,$(HDE_SRCS))
WIN_OBJS   := $(patsubst src/%.cpp,$(WIN_BUILD)/%.o,$(WIN_SRCS)) \
              $(patsubst $(IMGUI_DIR)/%.cpp,$(WIN_BUILD)/imgui/%.o,$(WIN_IMGUI)) \
              $(patsubst contrib/hde/%.c,$(WIN_BUILD)/hde/%.o,$(HDE_SRCS)) \
              $(WIN_BUILD)/resource.o

# -fvisibility=hidden: the Linux library exports nothing (Unity only dlopens it).
# -MMD -MP: header dependencies.
CXXFLAGS := -std=c++20 -O2 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
            -Wall -Wextra -Isrc -I$(IMGUI_DIR) \
            -ffunction-sections -fdata-sections -MMD -MP
CFLAGS   := -O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections

# -static-libstdc++: the game may run inside Steam's runtime, whose libstdc++
# is older than the build machine's. --exclude-libs keeps the folded-in
# runtime's symbols local, so they never stand in for the game's. The unwinder
# is NOT folded in (no -static-libgcc): GameAssembly.so throws its managed
# exceptions through libgcc_s.so.1, which it needs and the game loads, and the
# mod's catch must work them with that same unwinder - a private copy's
# _Unwind_SetGR, whose register table the game's unwinding never set up,
# aborted the game (seen 2026-10-26 with the harness's `throw`).
LDFLAGS  := -shared -Wl,-soname,$(NAME).so \
            -static-libstdc++ -Wl,--exclude-libs,ALL \
            -Wl,--gc-sections -Wl,-z,defs -Wl,-z,relro -Wl,-z,now \
            -Wl,--version-script=src/linux/exports.map
LDLIBS   := -ldl -lpthread

# Windows: the C++ runtime and winpthreads are folded in (-static), so the DLL
# needs nothing a Windows 10 or 11 install lacks (the C runtime is Windows' own
# UCRT); it exports exactly hid.dll's functions (src/win/hid.def). Of
# Windows' DLLs it imports kernel32 and user32 alone: the game loads it at
# start-up, so it must not pull in anything else - d3d11, dxgi and d3dcompiler
# are the game's, used through what the game made or loaded at run time
# (win/overlay_d3d11.cpp), and ImGui's shell and clipboard functions are left
# out (shell32; the panel has nothing to paste into).
# -s: the static runtime's debug sections would make it 14 MB.
WIN_CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wno-cast-function-type \
                -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D__USE_MINGW_ANSI_STDIO=1 \
                -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS -DIMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS \
                -Isrc -I$(IMGUI_DIR) \
                -ffunction-sections -fdata-sections -MMD -MP
WIN_CFLAGS   := -O2 -ffunction-sections -fdata-sections
WIN_LDFLAGS  := -shared -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -s
WIN_LDLIBS   := -luser32 -luuid -ldxguid

.PHONY: all linux windows clean install uninstall dist package rev version test
all: $(TARGET) $(WIN_TARGET)
linux: $(TARGET)
windows: $(WIN_TARGET)

$(TARGET): $(LINUX_OBJS) src/linux/exports.map
	$(CXX) $(LDFLAGS) -o $@ $(LINUX_OBJS) $(LDLIBS)
	@echo ">> Built $@"

$(WIN_TARGET): $(WIN_OBJS) src/win/hid.def
	$(WIN_CXX) $(WIN_LDFLAGS) -o $@ $(WIN_OBJS) src/win/hid.def $(WIN_LDLIBS)
	@echo ">> Built $@"

# Rebuild the mod's own objects when the version changes (the vendored objects
# do not use it). No VERSION file: nothing to track.
$(patsubst src/%.cpp,$(BUILD)/linux/%.o,$(LINUX_SRCS)) $(patsubst src/%.cpp,$(WIN_BUILD)/%.o,$(WIN_SRCS)): $(wildcard VERSION)

$(BUILD)/linux/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -DPPCC_VERSION='"$(VERSION)"' -c $< -o $@

$(BUILD)/linux/imgui/%.o: $(IMGUI_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/linux/hde/%.o: contrib/hde/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(WIN_BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(WIN_CXX) $(WIN_CXXFLAGS) -DPPCC_VERSION='"$(VERSION)"' -c $< -o $@

$(WIN_BUILD)/imgui/%.o: $(IMGUI_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(WIN_CXX) $(WIN_CXXFLAGS) -c $< -o $@

$(WIN_BUILD)/hde/%.o: contrib/hde/%.c
	@mkdir -p $(dir $@)
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

# The DLL's version resource: what Windows shows for hid.dll's Details
# (PPCabbyCodes and its version, so the file says what it is).
comma := ,
VERSION_NUMS := $(subst .,$(comma),$(VERSION)),0
$(WIN_BUILD)/resource.o: src/win/resource.rc $(wildcard VERSION)
	@mkdir -p $(dir $@)
	$(WIN_RC) -DPPCC_VERSION_NUMS=$(VERSION_NUMS) -DPPCC_VERSION_STR='\"$(VERSION)\"' $< -o $@

-include $(LINUX_OBJS:.o=.d) $(WIN_OBJS:.o=.d)

# --- Tests ---------------------------------------------------------------------
# Each test is a program of its own; `make test` builds and runs them all
# (tests/build/ holds the binaries and their logs). The Linux ones run as they
# are; the Windows ones (the .exe) are built with mingw-w64 and run under Wine,
# in a prefix of their own (tests/build/win/prefix), with no display: nothing
# they do reaches the desktop.
TEST_BUILD := tests/build
LINUX_TESTS := test_hook test_load test_config test_panel test_guard
WIN_TESTS   := test_hook test_config test_guard test_proxy test_slot
TESTS       := $(addprefix $(TEST_BUILD)/,$(LINUX_TESTS))
WIN_TEST_EXES := $(patsubst %,$(TEST_BUILD)/win/%.exe,$(WIN_TESTS))
TEST_LIBS  := $(BUILD)/linux/hook.o $(BUILD)/linux/linux/hook_linux.o $(BUILD)/linux/log.o \
              $(BUILD)/linux/config.o $(BUILD)/linux/linux/platform.o $(BUILD)/linux/hde/hde64.o
WIN_TEST_LIBS := $(WIN_BUILD)/hook.o $(WIN_BUILD)/win/hook_win.o $(WIN_BUILD)/log.o \
                 $(WIN_BUILD)/config.o $(WIN_BUILD)/win/platform.o $(WIN_BUILD)/hde/hde64.o

# test_panel draws the panel with ImGui and no renderer: it takes the panel and
# ImGui's core on top.
IMGUI_CORE_OBJS := $(patsubst $(IMGUI_DIR)/%.cpp,$(BUILD)/linux/imgui/%.o,$(IMGUI_CORE))
$(TEST_BUILD)/test_panel: TEST_EXTRA := $(BUILD)/linux/panel.o $(IMGUI_CORE_OBJS)
$(TEST_BUILD)/test_panel: $(BUILD)/linux/panel.o $(IMGUI_CORE_OBJS)

$(TEST_BUILD)/test_%: tests/test_%.cpp $(TEST_LIBS) $(TARGET)
	@mkdir -p $(TEST_BUILD)
	$(CXX) -std=c++20 -O1 -Wall -Wextra -Isrc -I$(IMGUI_DIR) -o $@ $< $(TEST_EXTRA) $(TEST_LIBS) -ldl -lpthread

$(TEST_BUILD)/win/test_%.exe: tests/test_%.cpp $(WIN_TEST_LIBS) $(WIN_TARGET)
	@mkdir -p $(TEST_BUILD)/win
	$(WIN_CXX) -std=c++20 -O1 -Wall -Wextra -Wno-cast-function-type -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
	  -D__USE_MINGW_ANSI_STDIO=1 -Isrc \
	  -static -o $@ $< $(WIN_TEST_LIBS) -luser32

# Wine for the Windows tests: a prefix of their own, made on first use, with no
# display (the variables unset), no menu entries or browser, and hid.dll taken
# from a file before Wine's own (the mod's, in test_proxy).
WINE_TEST_ENV := env -u DISPLAY -u WAYLAND_DISPLAY WINEPREFIX="$(abspath $(TEST_BUILD)/win/prefix)" \
                 HOME="$(abspath $(TEST_BUILD)/win/home)" WINEDEBUG=-all \
                 WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d;winebrowser.exe=d;hid=n,b"

test: $(TESTS) $(WIN_TEST_EXES)
	@set -e; for t in $(TESTS); do echo ">> $$t"; (cd tests && ../$$t); done
	@if command -v wine >/dev/null 2>&1; then \
	  set -e; mkdir -p $(TEST_BUILD)/win/home; \
	  for t in $(WIN_TEST_EXES); do echo ">> $$t (Wine)"; \
	    (cd tests && $(WINE_TEST_ENV) wine ../$$t > ../$$t.out 2>/dev/null; s=$$?; tr -d '\r' < ../$$t.out; exit $$s); \
	  done; \
	else echo ">> wine not found: the Windows tests were built, not run"; fi

# `make version` prints the version; `make version X.Y.Z` sets it (`rev` is an
# older spelling of the same thing). Make has no argument syntax, so the new
# version arrives as a second goal; the block near the top defines a do-nothing
# target for it so make does not fail trying to build "1.2.3".
version rev:
	@v='$(REV)'; \
	if [ -z "$$v" ] && [ "$@" = version ]; then echo "$(VERSION)"; exit 0; fi; \
	if ! echo "$$v" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$$'; then \
	  echo "usage: make $@ X.Y.Z        (for example: make $@ 1.2.0)"; \
	  echo "current version: $(VERSION)"; \
	  exit 1; \
	fi; \
	printf '%s\n' "$$v" > VERSION; \
	echo ">> version $(VERSION) -> $$v"; \
	echo ">> run 'make package' to build $(PROJECT)_v$$v.zip"

clean:
	rm -rf $(BUILD) $(DIST) $(TEST_BUILD)

# --- Deploy -----------------------------------------------------------------
# Linux: one library in the game's Plugins folder, beside the game's own.
# Windows (when WIN_GAME_DIR is set): hid.dll beside PapersPlease.exe - never
# over a hid.dll that is not this mod's. Builds before 2026-10-26 were
# version.dll, which is MelonLoader's name: one of those is removed.
# Atomic install: write to a temp name in the SAME directory then rename(2) over
# the target, so a running game keeps its old intact mapping.
#
# Builds before this layout stood in for libsteam_api.so (the game's own renamed
# libsteam_api_orig.so); an install finding that puts the game's library back.
install: $(TARGET) $(WIN_TARGET)
	@set -e; \
	if [ -z "$(GAME_DIR)" ]; then \
	  echo "ERROR: GAME_DIR is not set."; \
	  echo "  Copy config.mk.example to config.mk and set GAME_DIR,"; \
	  echo "  or run: make install GAME_DIR='/path/to/PapersPlease'"; \
	  exit 1; \
	fi; \
	if [ ! -d "$(GAME_DIR)" ]; then echo "ERROR: GAME_DIR '$(GAME_DIR)' does not exist."; exit 1; fi; \
	if [ ! -f "$(GAME_DIR)/$(EXE)" ]; then \
	  echo "ERROR: no '$(EXE)' in '$(GAME_DIR)' - is that really the game folder?"; exit 1; \
	fi; \
	plugins="$(GAME_DIR)/$(PLUGINS_SUBDIR)"; \
	if [ ! -d "$$plugins" ]; then echo "ERROR: no '$(PLUGINS_SUBDIR)' in '$(GAME_DIR)'."; exit 1; fi; \
	if [ -f "$$plugins/libsteam_api_orig.so" ] && grep -aq "$(PROJECT)" "$$plugins/libsteam_api.so" 2>/dev/null; then \
	  mv -f "$$plugins/libsteam_api_orig.so" "$$plugins/libsteam_api.so"; \
	  echo ">> put the game's libsteam_api.so back (an older build of the mod stood in for it)"; \
	fi; \
	live="$$plugins/$(NAME).so"; \
	t="$$plugins/.$(NAME).so.ppcc-new.$$$$"; cp -f $(TARGET) "$$t"; mv -f "$$t" "$$live"; \
	echo ">> installed (atomic) $$live"; \
	if pgrep -x "$(EXE)" >/dev/null 2>&1; then \
	  echo ">> NOTE: the game is running - it keeps the copy it loaded until it is restarted"; \
	fi; \
	if [ -z "$(WIN_GAME_DIR)" ]; then echo ">> WIN_GAME_DIR is not set: the Windows build is not installed"; exit 0; fi; \
	if [ ! -f "$(WIN_GAME_DIR)/$(WIN_EXE)" ]; then \
	  echo "ERROR: no '$(WIN_EXE)' in WIN_GAME_DIR '$(WIN_GAME_DIR)' - is that really the Windows game folder?"; exit 1; \
	fi; \
	wlive="$(WIN_GAME_DIR)/$(WIN_NAME).dll"; \
	if [ -f "$$wlive" ] && ! grep -aq "$(PROJECT)" "$$wlive"; then \
	  echo "ERROR: '$$wlive' is not this mod's: not overwriting it."; exit 1; \
	fi; \
	t="$(WIN_GAME_DIR)/.$(WIN_NAME).dll.ppcc-new.$$$$"; cp -f $(WIN_TARGET) "$$t"; mv -f "$$t" "$$wlive"; \
	echo ">> installed (atomic) $$wlive"; \
	old="$(WIN_GAME_DIR)/version.dll"; \
	if [ -f "$$old" ] && grep -aq "$(PROJECT)" "$$old"; then \
	  rm -f "$$old"; echo ">> removed $$old (an older build of the mod, which had MelonLoader's name)"; \
	fi

# Remove the mod (its settings and log stay, in the game folder).
uninstall:
	@set -e; \
	if [ -z "$(GAME_DIR)" ]; then echo "ERROR: GAME_DIR is not set."; exit 1; fi; \
	live="$(GAME_DIR)/$(PLUGINS_SUBDIR)/$(NAME).so"; \
	if [ -f "$$live" ]; then rm -f "$$live"; echo ">> removed $$live"; \
	else echo "Nothing to do: $$live not present."; fi; \
	if [ -n "$(WIN_GAME_DIR)" ]; then \
	  for wlive in "$(WIN_GAME_DIR)/$(WIN_NAME).dll" "$(WIN_GAME_DIR)/version.dll"; do \
	    if [ -f "$$wlive" ] && grep -aq "$(PROJECT)" "$$wlive"; then rm -f "$$wlive"; echo ">> removed $$wlive"; \
	    elif [ -f "$$wlive" ]; then echo ">> left $$wlive alone: it is not this mod's"; fi; \
	  done; \
	fi

# --- Distributable ----------------------------------------------------------
# `make dist` assembles dist/PPCabbyCodes/ - each build in a folder of its own
# (Linux/, Windows/), the docs and an INSTALL.txt.
DIST    ?= dist
PAYLOAD  = $(DIST)/$(PROJECT)

dist: $(TARGET) $(WIN_TARGET)
	@rm -rf "$(PAYLOAD)" && mkdir -p "$(PAYLOAD)/Linux" "$(PAYLOAD)/Windows"
	@cp $(TARGET) "$(PAYLOAD)/Linux/$(NAME).so"
	@cp $(WIN_TARGET) "$(PAYLOAD)/Windows/$(WIN_NAME).dll"
	@cp README.md LICENSE "$(PAYLOAD)/"
	@printf '%s\r\n' \
	  "$(PROJECT) $(VERSION) - pause-menu cheat panel for Papers, Please (Steam; Windows and Linux)" \
	  "" \
	  "INSTALL - WINDOWS" \
	  "  Copy  Windows/$(WIN_NAME).dll  into your Papers, Please folder: the one with" \
	  "  $(WIN_EXE) in it. That is all: the game loads it by itself - no game" \
	  "  file is replaced, no launch options. It works beside MelonLoader and" \
	  "  BepInEx (their mods too): it is none of their files." \
	  "" \
	  "INSTALL - LINUX (the native version)" \
	  "  Copy  Linux/$(NAME).so  into your Papers, Please folder's" \
	  "  $(PLUGINS_SUBDIR) folder (the game folder is the one containing" \
	  "  '$(EXE)'). The game loads it by itself." \
	  "" \
	  "UNINSTALL" \
	  "  Delete $(WIN_NAME).dll (Windows) or $(PLUGINS_SUBDIR)/$(NAME).so (Linux)." \
	  "" \
	  "IF IT DOES NOT WORK" \
	  "  The mod says why in $(PROJECT).log in the game's folder." \
	  "" \
	  "USE" \
	  "  Pause the game (Esc): a panel appears with the cheats. F7 hides/shows" \
	  "  it. Settings and the log live in $(PROJECT).ini / .log in the game folder." \
	  > "$(PAYLOAD)/INSTALL.txt"
	@echo ">> assembled $(PAYLOAD)"

# <project>_v<version>.zip, e.g. PPCabbyCodes_v1.0.0.zip
PKG ?= $(DIST)/$(PROJECT)_v$(VERSION).zip
package: dist
	@command -v zip >/dev/null || { echo "ERROR: zip required"; exit 1; }
	@rm -f "$(PKG)"
	@cd "$(PAYLOAD)" && zip -r -q "$(abspath $(PKG))" .
	@echo ">> packaged -> $(PKG) ($$(du -h "$(PKG)" | cut -f1))"
