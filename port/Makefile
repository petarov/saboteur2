# Saboteur II port: SAB2.COM code translated to x86-64 (generated from the
# Ghidra listing re/SAB2.asm) + emulated BIOS/DOS/hardware + SDL2 front end.
# Builds an x86_64 binary: on macOS it runs under Rosetta 2 on Apple Silicon,
# on Linux it needs an x86_64 host and the system SDL2 (pkg-config sdl2),
# on Windows SDL2.dll next to saboteur2.exe.
# The web build (make web, in Docker) translates the listing to C instead
# (tools/asm2c.py) and compiles it to WebAssembly with Emscripten.

BUILD    := build
LISTING  := ../re/SAB2.asm
EXE      := ../SAB2.COM
NASM     := nasm
PYTHON   := python3 -I
VER      := 1.1

# Windows is cross-compiled with mingw-w64 (make windows, in Docker).
TARGET_OS ?= $(shell uname -s)

ifeq ($(TARGET_OS),Windows)

SDL_MINGW ?= /opt/SDL2/x86_64-w64-mingw32
CC       := x86_64-w64-mingw32-gcc
CFLAGS   := -std=c11 -O2 -Wall -Wextra -Isrc -I$(SDL_MINGW)/include
NFLAGS   := -f win64 -Wall -w-reloc-rel-dword -Isrc/
LDFLAGS  := -L$(SDL_MINGW)/lib -lmingw32 -lSDL2main -lSDL2 -static-libgcc
# The game is a GUI program (no console window); SDL2main supplies WinMain.
GUI_LDFLAGS := -mwindows
EXE_SUFFIX  := .exe

else ifeq ($(TARGET_OS),Web)

CC       := emcc
CFLAGS   := -std=c11 -O2 -Wall -Wextra -Isrc -sUSE_SDL=2
WEB_LDFLAGS := -O2 -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH -sENVIRONMENT=web

else ifeq ($(TARGET_OS),Linux)

CC       := cc
CFLAGS   := -std=c11 -O2 -Wall -Wextra -Isrc $(shell pkg-config --cflags sdl2)
NFLAGS   := -f elf64 -Wall -w-reloc-rel-dword -Isrc/
LDFLAGS  := $(shell pkg-config --libs sdl2)

else

ARCH     := x86_64
SDL_DIR  := third_party
CC       := clang
CFLAGS   := -arch $(ARCH) -std=c11 -O2 -Wall -Wextra -F$(SDL_DIR) -Isrc
# Mach-O C symbols carry a leading underscore; --gprefix adds it to asm globals/externs.
# RIP-relative references across sections are normal on Mach-O, so that warning is off.
NFLAGS   := -f macho64 --gprefix _ -Wall -w-reloc-rel-dword -Isrc/
LDFLAGS  := -arch $(ARCH) -F$(SDL_DIR) -framework SDL2 -Wl,-rpath,@executable_path/../$(SDL_DIR) \
            -Wl,-rpath,@executable_path

endif

GAME_OBJS := $(BUILD)/bios.o $(BUILD)/font8x8.o $(BUILD)/speaker.o $(BUILD)/game.o $(BUILD)/sab2_code.o $(BUILD)/data.o
TARGET    := $(BUILD)/saboteur2$(EXE_SUFFIX)
CHECK     := $(BUILD)/check_text

ifeq ($(TARGET_OS),Web)
all: $(BUILD)/index.html
else ifeq ($(TARGET_OS),Windows)
all: $(TARGET) $(BUILD)/SDL2.dll

$(BUILD)/SDL2.dll: $(SDL_MINGW)/bin/SDL2.dll | $(BUILD)
	cp $< $@
else
all: $(TARGET)
endif

$(TARGET): $(BUILD)/platform.o $(GAME_OBJS)
	$(CC) $^ $(LDFLAGS) $(GUI_LDFLAGS) -o $@

$(BUILD)/sab2_code.asm: $(LISTING) tools/asm2nasm.py | $(BUILD)
	$(PYTHON) tools/asm2nasm.py $(LISTING) $@

$(BUILD)/sab2_code.o: $(BUILD)/sab2_code.asm src/runtime.inc
	$(NASM) $(NFLAGS) $< -o $@

$(BUILD)/game.o: src/game.asm src/runtime.inc | $(BUILD)
	$(NASM) $(NFLAGS) $< -o $@

# data.asm incbins ../SAB2.COM relative to this directory. It also holds the
# version shown in the menu; rebuild when VER changes.
$(BUILD)/data.o: src/data.asm src/runtime.inc $(EXE) Makefile | $(BUILD)
	$(NASM) $(NFLAGS) -DSAB2_VERSION='"$(VER)"' $< -o $@

$(BUILD)/%.o: src/%.c src/bios.h src/font8x8.h src/speaker.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Web: the same C sources plus the game translated to C; the page is
# index.html with index.js and index.wasm next to it. The translation holds
# the data segment, with the version shown in the menu.
WEB_OBJS := $(BUILD)/platform.o $(BUILD)/bios.o $(BUILD)/font8x8.o $(BUILD)/speaker.o \
            $(BUILD)/runtime.o $(BUILD)/sab2_code_c.o

$(BUILD)/index.html: $(WEB_OBJS) src/web/shell.html
	$(CC) $(WEB_OBJS) $(WEB_LDFLAGS) --shell-file src/web/shell.html -o $@

$(BUILD)/sab2_code.c: $(LISTING) $(EXE) tools/asm2c.py tools/asm2nasm.py Makefile | $(BUILD)
	$(PYTHON) tools/asm2c.py $(LISTING) $(EXE) $(VER) $@

$(BUILD)/sab2_code_c.o: $(BUILD)/sab2_code.c src/runtime.h src/bios.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/runtime.o: src/runtime.h

# The version is shown in the window title; rebuild when VER changes.
$(BUILD)/platform.o: CFLAGS += -DSAB2_VERSION='"$(VER)"'
$(BUILD)/platform.o: Makefile

# Headless check: generated print_text (graphics mode) vs. the independent
# Python model, on a test string and the mission briefing strings.
$(BUILD)/check_main.o: tests/check_main.c src/bios.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/check_text.o: tests/check_text.asm src/runtime.inc | $(BUILD)
	$(NASM) $(NFLAGS) $< -o $@

$(CHECK): $(BUILD)/check_main.o $(BUILD)/check_text.o $(GAME_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

# World map: runs the generated room_setup headless for every map cell
# (32x32 grid; row 0 is only the start screen) and stitches the rooms into
# build/world.png, 8192x4608. Room dumps go to build/rooms/.
RENDER := $(BUILD)/render_room

$(BUILD)/render_room.o: tools/render_room.c src/bios.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/render_room_asm.o: tools/render_room.asm src/runtime.inc | $(BUILD)
	$(NASM) $(NFLAGS) $< -o $@

$(RENDER): $(BUILD)/render_room.o $(BUILD)/render_room_asm.o $(GAME_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

world: $(RENDER)
	rm -rf $(BUILD)/rooms && mkdir -p $(BUILD)/rooms
	@fail=0; r=0; while [ $$r -lt 32 ]; do \
		c=0; while [ $$c -lt 32 ]; do \
			out=$$(printf '$(BUILD)/rooms/r%02X_c%02X.bin' $$r $$c); \
			./$(RENDER) $$r $$c $$out 2>/dev/null; rc=$$?; \
			if [ $$rc -ne 0 ] && [ $$rc -ne 3 ]; then rm -f $$out; fail=$$((fail + 1)); \
				echo "world: row $$r col $$c not rendered (exit $$rc)"; fi; \
			c=$$((c + 1)); done; \
		r=$$((r + 1)); done; \
	echo "world: $$fail rooms failed"
	$(PYTHON) tools/world2png.py $(BUILD)/rooms $(BUILD)/world.png

check: $(CHECK)
	$(PYTHON) tools/ref_text.py $(EXE) $(BUILD)/check_input.bin $(BUILD)/vram_ref.bin
	./$(CHECK) $(BUILD)/check_input.bin $(BUILD)/vram_game.bin
	cmp $(BUILD)/vram_game.bin $(BUILD)/vram_ref.bin && echo "check: print_text output matches reference"

# Linux build + headless test in an x86_64 container (emulated on Apple Silicon).
# The toolchain image is built once and reused; objects go to build/linux.
LINUX_BASE  ?= debian:trixie
LINUX_IMAGE := saboteur2-linux-build

linux-image:
	printf 'FROM $(LINUX_BASE)\nRUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq make gcc nasm python3 pkg-config libsdl2-dev >/dev/null\n' \
		| docker build --platform linux/amd64 -t $(LINUX_IMAGE) -

LINUX_RUN = docker run --rm --platform linux/amd64 -v "$(abspath ..)":/src -w /src/port \
	-e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy $(LINUX_IMAGE) make BUILD=build/linux

linux: linux-image
	$(LINUX_RUN) all

linux-check: linux-image
	$(LINUX_RUN) all check

# Windows x86_64 cross build with mingw-w64 and the official SDL2 mingw release.
# The compiler runs natively in the container; only its output is x86_64.
# Result: build/windows/saboteur2.exe + SDL2.dll (copy both to the Windows box).
SDL_VERSION   := 2.32.10
WINDOWS_IMAGE := saboteur2-windows-build

windows-image:
	printf 'FROM $(LINUX_BASE)\nRUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq make gcc-mingw-w64-x86-64 nasm python3 curl ca-certificates >/dev/null\nRUN curl -fsSL https://github.com/libsdl-org/SDL/releases/download/release-$(SDL_VERSION)/SDL2-devel-$(SDL_VERSION)-mingw.tar.gz | tar -xz -C /opt && mv /opt/SDL2-$(SDL_VERSION) /opt/SDL2\n' \
		| docker build -t $(WINDOWS_IMAGE) -

windows: windows-image
	docker run --rm -v "$(abspath ..)":/src -w /src/port $(WINDOWS_IMAGE) make TARGET_OS=Windows BUILD=build/windows all

# Web (WebAssembly) build with the official Emscripten image; the -arm64
# variant runs natively on Apple Silicon. Building the image compiles a
# program with the same flags once, so SDL2 and the system libraries are in
# its cache. Result: build/web/index.html, .js and .wasm; serve the
# directory over HTTP (e.g. python3 -m http.server -d build/web).
EMSDK_VERSION := 4.0.10
EMSDK_TAG     := $(EMSDK_VERSION)$(if $(filter arm64 aarch64,$(shell uname -m)),-arm64)
WEB_IMAGE     := saboteur2-web-build

web-image:
	printf 'FROM emscripten/emsdk:$(EMSDK_TAG)\nRUN echo "int main(void) { return 0; }" > /tmp/t.c && emcc -O2 -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH -sENVIRONMENT=web /tmp/t.c -o /tmp/t.html && rm -f /tmp/t.*\n' \
		| docker build -t $(WEB_IMAGE) -

web: web-image
	docker run --rm -v "$(abspath ..)":/src -w /src/port $(WEB_IMAGE) make TARGET_OS=Web BUILD=build/web all

# SDL2.framework for the macOS archive, built from third_party: only the
# x86_64 slice (the game is x86_64; arm64 Macs run it under Rosetta 2), re-signed
# ad hoc as the release is, plus Resources; no Headers. The symlinks are made
# here, since a copied framework often has them flattened into copies.
FW_SRC := $(SDL_DIR)/SDL2.framework/Versions/A

pack-framework:
	mkdir -p $(FW)/Versions/A
	lipo $(FW_SRC)/SDL2 -thin x86_64 -output $(FW)/Versions/A/SDL2
	cp -R $(FW_SRC)/Resources $(FW)/Versions/A/
	ln -s A $(FW)/Versions/Current
	ln -s Versions/Current/SDL2 $(FW)/SDL2
	ln -s Versions/Current/Resources $(FW)/Resources
	codesign --force --sign - $(FW)

# Release archives in build/: the macOS build (always) plus the Linux and
# Windows builds if they exist (make linux / make windows first). Each holds
# one top-level directory named like the archive. Run on macOS.
PACK_NAME := saboteur2-$(VER)
PACK_DIR  := $(BUILD)/pack

pack: $(TARGET)
	rm -rf $(PACK_DIR)
	mkdir -p $(PACK_DIR)/$(PACK_NAME)-macos-x86_64
	cp $(TARGET) $(PACK_DIR)/$(PACK_NAME)-macos-x86_64/
	$(MAKE) pack-framework FW=$(PACK_DIR)/$(PACK_NAME)-macos-x86_64/SDL2.framework
	rm -f $(BUILD)/$(PACK_NAME)-macos-x86_64.zip
	cd $(PACK_DIR) && zip -qry ../$(PACK_NAME)-macos-x86_64.zip $(PACK_NAME)-macos-x86_64
	@echo "pack: $(BUILD)/$(PACK_NAME)-macos-x86_64.zip"
	@if [ -f $(BUILD)/linux/saboteur2 ]; then \
		mkdir -p $(PACK_DIR)/$(PACK_NAME)-linux-x86_64 && \
		cp $(BUILD)/linux/saboteur2 $(PACK_DIR)/$(PACK_NAME)-linux-x86_64/ && \
		COPYFILE_DISABLE=1 tar -czf $(BUILD)/$(PACK_NAME)-linux-x86_64.tar.gz --uid 0 --gid 0 \
			-C $(PACK_DIR) $(PACK_NAME)-linux-x86_64 && \
		echo "pack: $(BUILD)/$(PACK_NAME)-linux-x86_64.tar.gz"; \
	fi
	@if [ -f $(BUILD)/windows/saboteur2.exe ]; then \
		mkdir -p $(PACK_DIR)/$(PACK_NAME)-windows-x86_64 && \
		cp $(BUILD)/windows/saboteur2.exe $(BUILD)/windows/SDL2.dll $(PACK_DIR)/$(PACK_NAME)-windows-x86_64/ && \
		rm -f $(BUILD)/$(PACK_NAME)-windows-x86_64.zip && \
		(cd $(PACK_DIR) && zip -qr ../$(PACK_NAME)-windows-x86_64.zip $(PACK_NAME)-windows-x86_64) && \
		echo "pack: $(BUILD)/$(PACK_NAME)-windows-x86_64.zip"; \
	fi
	rm -rf $(PACK_DIR)

$(BUILD):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

clean:
	@-rm -rf $(BUILD)/

.PHONY: all run check clean linux-image linux linux-check windows-image windows web-image web pack \
        pack-framework world
