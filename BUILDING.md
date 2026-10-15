# Building Saboteur II (recomp)

## Requirements

All platforms:

- The game was written in hand-coded 8088 assembly. This project disassembles `SAB2.COM`, translates the 16-bit code into x86-64 assembly, and runs it on emulated PC hardware with SDL2 handling video and sound.
- The original `SAB2.COM`, in the repository root. The build reads the game data directly from it, and every binary embeds it, so no game files are needed at run time. `SAB2.COM` and the disassembly derived from it are copyrighted by their original owners.
- `re/SAB2.asm`, the Ghidra listing of `SAB2.COM`. Regenerating it needs Ghidra 12.1.4 (see below); the build only reads the file.

macOS (native build, x86_64, runs under Rosetta 2 on Apple Silicon):

- Xcode command line tools (clang, make, zip)
- nasm (`brew install nasm`)
- Python 3. It is used at build time to generate the translated code.
- SDL2, as the official macOS release framework (2.32.x, universal). Copy `SDL2.framework` to `port/third_party/SDL2.framework`. If macOS refuses to load it ("library load disallowed by system policy"), remove the download quarantine: `xattr -dr com.apple.quarantine port/third_party/SDL2.framework`.
- Docker Desktop, only for the Linux and Windows builds from macOS (`make linux`, `make windows`).

Linux (native build, x86_64 host):

- make, gcc, nasm, python3, pkg-config and the SDL2 development package. On Debian / Ubuntu / Linux Mint: `sudo apt install make gcc nasm python3 pkg-config libsdl2-dev`.

The macOS build has been run. The Linux build and `make check` pass in Docker, and the Windows build cross-compiles, but neither binary has been run on its target system yet.

## Building and running

From `port/`:

- `make` builds `build/saboteur2` for the host (macOS or Linux)
- `make run` builds and starts the game
- `make check` is a headless test. It runs the translated text renderer (`print_text`, CS:04B7, in graphics mode) on a test string and the mission briefing strings, and compares the resulting screen memory with an independent Python model (`tools/ref_text.py`).
- `make clean` removes `build/`. Run it after changing the SDL version: the objects do not depend on the SDL headers.

Messages about unsupported BIOS/DOS calls or I/O ports are printed once each to the terminal (not on Windows, which has no console window).

The version in the window title is `VER` in `port/Makefile`.

The high-score table `SAB_II.SCR` is kept in the per-user SDL preferences folder (on macOS `~/Library/Application Support/petarov/saboteur2/`). If the file is missing or invalid, the game picks one of its built-in default tables at random. The `SAB_II.SCR` in the repository root is not used by the build.

### Linux and Windows builds from macOS

Both run in Docker. Each target builds its toolchain image once (a few minutes) and reuses it afterwards.

- `make linux` builds `build/linux/saboteur2` in an emulated x86_64 Debian 13 container. The binary needs glibc 2.34 or newer and SDL2 (`libsdl2-2.0-0`) on the target system, e.g. Debian 12+, Ubuntu 22.04+, Linux Mint 21+.
- `make linux-check` also runs `check` in that container.
- `make windows` cross-compiles `build/windows/saboteur2.exe` with mingw-w64 and copies `SDL2.dll` next to it. The image downloads the official SDL2 mingw release (`SDL_VERSION` in the Makefile). Copy both files to the Windows machine.

### Release archives

`make pack` (macOS) builds the macOS binary and writes to `build/`:

- `saboteur2-<VER>-macos-x86_64.zip`: `saboteur2` and `SDL2.framework`
- `saboteur2-<VER>-linux-x86_64.tar.gz`: `saboteur2`, if `build/linux/saboteur2` exists
- `saboteur2-<VER>-windows-x86_64.zip`: `saboteur2.exe` and `SDL2.dll`, if `build/windows/saboteur2.exe` exists

`pack` does not rebuild the Linux and Windows binaries; run `make linux` and `make windows` first. The macOS binary is not signed or notarized, so a downloaded copy needs `xattr -dr com.apple.quarantine <folder>` or approval under Privacy & Security.

## Layout

- `re/` holds the reverse-engineering material.
  - `re/SAB2.asm` is the Ghidra listing of the `SAB2.COM` code area (CS:0100–2E60). `port/tools/asm2nasm.py` reads it. `<NOT INCLUDED>`
  - `re/DumpListing.java` is the Ghidra script that produces it, together with `re/stats.txt` and `re/fingerprint.txt`.
  - `re/NOTES.md` covers the memory layout, the indirect jumps and how they are seeded, the stack tricks, the key procedures, the high-score file format, how `SAB2.COM` differs from the protected `SAB2.EXE`, and open questions.
  - `re/CAT.asm` is the old Alley Cat listing. Nothing uses it any more. `<NOT INCLUDED>`
- `port/tools/asm2nasm.py` translates `re/SAB2.asm` into `port/build/sab2_code.asm`. Every original instruction gets a label `L_xxxx` after its code-segment offset, and a table `cs_labels` maps every code offset to its label for `JMP BX` and the INT 1Ch handlers. Its `MARKERS` table holds the port's few changes to the game code (the timer poll points, and skipping the CPU speed calibration loop, which would overflow on a modern CPU), and `CPU_PACE_NS` slows the software-driven sound and delay loops to 8088 speed.
- `port/src/runtime.inc` holds the execution model: how 8086 registers, segments, string instructions, flags, indirect jumps and BIOS/DOS/port calls map to x86-64, and the calling-convention differences between macOS/Linux and Windows. The generated code and the hand-written code both use it.
- `port/src/game.asm` holds the hand-written parts: the entry and exit, the segment lookup and the timer poll, which runs the game's INT 1Ch handlers.
- `port/src/data.asm` includes the original data segment (DS = CS + 2E6h) straight from `SAB2.COM`, so data offsets match the listing. All game text comes from there.
- `port/src/bios.c` emulates the hardware and DOS: the BIOS services (INT 10h, 16h, 1Ah), the DOS file calls for `SAB_II.SCR` (INT 21h, FCB functions), the 18.2 Hz timer tick, the speaker port, the 8088 speed pacing and the memory segments.
- `port/src/font8x8.c` is the 8x8 font for the BIOS text modes, drawn for this port in CGA style.
- `port/src/speaker.c` emulates the PC speaker on an SDL2 audio device.
- `port/src/platform.c` is the SDL2 front end: the window, display of the text and CGA graphics screens, keyboard events and the game thread.
- `port/tests/` holds the headless test program used by `make check`.

## How it works

The original program is translated, not emulated instruction by instruction. The 8086 registers keep their 16-bit names in the generated code. DS and ES become base pointers into emulated 64 KB segments. Interrupts and port accesses become calls into C.

The original installs an INT 1Ch handler for the game tick and the music. The port has no real interrupts: the handlers run synchronously at the points where the game waits for the tick or plays sound, at most two missed ticks at a time.

The game runs on its own thread. Where the original busy-waited (delay loops, the timer wait, keyboard polling), the port sleeps, so it does not hog a CPU core: the 8088 speed pacing sleeps when the translated code runs ahead, the timer wait sleeps until the next tick, and repeated empty keyboard polls sleep 1 ms. The main thread plays the part of the display hardware: it converts the emulated screen memory to pixels at 60 Hz, handles window events, and queues key presses for INT 16h. Speaker changes are timestamped on the game thread and replayed on SDL's audio thread, so the sound keeps the game's timing.

## Regenerating the listing

`re/SAB2.asm` was produced with Ghidra 12.1.4. To regenerate it, run this from the repository root. With Homebrew, `analyzeHeadless` is in `$(brew --prefix ghidra)/libexec/support/`.

```
mkdir -p /tmp/ghidra-proj
analyzeHeadless /tmp/ghidra-proj sab2 -import SAB2.COM \
    -loader BinaryLoader -loader-baseAddr 0x10100 -processor "x86:LE:16:Real Mode" \
    -scriptPath re -postScript DumpListing.java re/SAB2.asm re/stats.txt re/fingerprint.txt \
    -deleteProject
```

A .COM file has no header, so it is imported as a raw binary at 1000:0100. The script:

- splits the code area (CS:0100–2E60) from the data, and sets DS for the code
- turns off Ghidra's jump-table recovery, which runs past the end of the game's tables into data
- seeds the two INT 1Ch handlers, the script-opcode and case tables, and the continuation addresses that the game stores and later reaches through `JMP BX`
- fails if any instruction ends up in the data area
- corrects the operand order of `MOV Sreg,r/m` instructions, which Ghidra prints reversed

The output is the same on every run. After regenerating, run `make check` in `port/`: `asm2nasm.py` fails on any jump target without a label.
