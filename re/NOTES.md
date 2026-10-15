# SAB2.COM (Saboteur II, 1987): reverse-engineering notes

Target: `SAB2.COM`, 42,890 bytes, dated 1996-12-24, SHA-256 `bb0fca0d…74fe8c`. High-score file `SAB_II.SCR`, 256 bytes.
Tools: Ghidra 12.1.4 headless with `DumpListing.java`, cross-checked against a linear `ndisasm` pass.

**Legend:** **[O]** means observed directly in bytes or instructions. **[I]** means inferred from usage. The names are inferences. Nothing was executed.

## Verdict: hand-written 8088 assembly, not C

- [O] 106 procedures and **0** `PUSH BP / MOV BP,SP` frames. Arguments are passed in registers. There is no CRT startup and there are no compiler or library strings.
- [O] 8088 instructions only: no `PUSHA`, `ENTER`, immediate pushes or shift counts other than 1 (the entry divides by 16 with four `SHR BX,1`).
- [O] Near code only: no far calls or jumps and no `RETF`.
- [O] 34 `EB xx 90` sequences (short `JMP` plus `NOP`). [I] This is the padding the Microsoft Macro Assembler leaves on forward jumps. Register-to-register encodings (`0A C0` for `OR AL,AL`, `03 C3`, `8B C8`) also match MASM. TASM came out in 1988, after the copyright date.
- [O] The program uses BIOS `INT 10h/16h/1Ah`, DOS `INT 21h` (FCB file calls only) and port 61h. It hooks `INT 1Ch` and nothing else.

## Memory layout [O]

`SAB2.COM` is a plain .COM image, loaded at PSP:0100. The entry code sets `DS = ES = CS + (2E61h >> 4) = CS + 2E6h`. [I] DOS sets SS to the PSP segment with SP at the top of that 64 KB segment, as for any .COM file.

| Part | Address | File offset | Size | Contents |
|---|---|---|---|---|
| Code | CS:0100–2E60 | 0x0000–0x2D60 | 0x2D61 | All code. File offset = CS offset − 100h |
| Data | DS:0000–7A29 | 0x2D60–0xA789 | 0x7A2A | Sprites, room scripts, tables, texts, default high scores. File offset = DS offset + 2D60h |

DS:0000 is the last code byte (the `RET` at CS:2E60). Code and data overlap by one byte.

## Ghidra caveats

- [O] The file is imported as a raw binary: `-loader BinaryLoader -loader-baseAddr 0x10100 -processor "x86:LE:16:Real Mode"`. CS is then 1000h, and the offset part of every `0x1000:xxxx` operand in the listing is the CS offset.
- [O] The script splits the memory block at CS:2E61 and sets DS = 12E6h over the code range. Otherwise Ghidra resolves `[xxxx]` against CS.
- [O] Ghidra's jump-table recovery (decompiler switch analysis and the x86 constant reference analyzer) reads the DS tables past their real length and disassembled about 6,000 "instructions" in the data area. The script turns both off, disassembles with code analysis disabled and seeds the tables itself. It then fails if any instruction lands in the data area.
- [O] Ghidra 12.1.4 prints opcode `8E /r` with its operands reversed. `SAB2.asm` corrects this, as `CAT.asm` did.
- [O] `ndisasm` loses sync once, at CS:2031 (a `00` pad byte before CS:2032). Every other Ghidra instruction starts on an ndisasm boundary.

## Indirect control flow

The code has 8 `JMP BX` sites and no indirect calls.

| Site | Target source | Notes |
|---|---|---|
| CS:0705 | Word table DS:1BE8, indexed by a script opcode byte | [O] Room script interpreter in `room_setup`. Scripts are selected through DS:3B4C. The opcode byte `FFh` ends a script. Entries 1–105 are code addresses. Entry 0 (`FF30h`) is not |
| CS:072E | The same table with base DS:1BEA (index + 1) | [O] 22 three-byte records at DS:2567: a word compared with `[1BD6h]`, then the opcode |
| CS:0909, 09A2, 09EB, 0AD1 | `[3D38h]` | [O] Opcode handlers return through this variable. It holds CS:0707 (next opcode) or CS:0730 (next record). The script pointer sits on the stack as data. The handlers pop it, advance it and push it back. It is never used as a return address |
| CS:11A9 | `[1BBAh]` | [O] Player state machine in `player_update`. 25 continuations are stored either as `MOV [1BBAh],imm` or as `MOV BX,imm` followed by a jump to the shared tails CS:1206/1209/120D. Those tails also store a sprite pointer in `[1BB8h]` and a byte in `[1BD8h]` |
| CS:221A | Word table DS:6F5D, indexed by `[SI+0Bh]` | [O] 13 entries (0–12). Entry 13 onwards is data |

The script finds the `[3D38h]` and `[1BBAh]` continuations by pattern and repeats the search until no new code turns up.

## Stack tricks [O]

- `sound_seq_play` (CS:2AD8) saves SP in `[78AAh]` and installs `int1c_sound_isr`. It then runs a byte-code sequencer from DS:79A8. The sequencer has call (`PUSH SI`) and return (`POP SI`) opcodes on the stack. The end opcode jumps to CS:2ABC. There the code restores the INT 1Ch vector, reloads SP from `[78AAh]`, pops ES and returns to the caller of CS:2AD8, which drops any pending sequencer frames.
- The script interpreter keeps its data pointer on the stack across `JMP BX` (see above).

## Program structure

- **entry (CS:0100)** [O]: computes DS and ES and reads the video mode (`INT 10h AH=0Fh`). For mode 7 or higher it prints "WRONG SCREEN MODE/MONITOR" (DS:1B82) and exits with `INT 21h AX=4C00h`.
- **Start-up (CS:0196)** [O]:
  - Picks the game mode: CGA mode 5 if the current mode is 0, 2 or 5 (the black-and-white modes), otherwise mode 4. It also picks the matching 40-column text mode, 0 or 1.
  - Seeds `[1BBEh]` from `INT 1Ah`.
  - Loads `SAB_II.SCR` (see below).
  - Shows the copyright text in text mode (DS:06FD), then switches to graphics.
  - Installs `int1c_tick_isr`.
- **CPU speed calibration (CS:0270)** [O]: the code counts a 10-iteration delay loop until the next timer tick. Below 600h passes it adds 8 to `[79A9h]`, which is byte 1 of the sound sequence at DS:79A8 that `sound_seq_play` plays (CS:2B13). [I] It adjusts that tune for slow machines. This matters for a port.
- **Title, menu and high scores** [I]: CS:2736 onwards (`JMP` from CS:028B). From there, `JMP` CS:028E starts a mission.
- **start_mission (CS:028E)** [O]: prints "MISSION BRIEFING", "LEVEL", "MISSION NAME" and the rank texts ("999 STRENGTH OF MIND AND BODY" …), waits for a key and sets graphics mode again. It then resets the game state (CS:03B2), calls `room_setup` and jumps to `game_loop`.
- **game_loop (CS:049A–04B5)** [O]: an endless frame loop. Each pass calls CS:0EBC, `read_keyboard`, `player_update`, CS:104A, CS:2032, CS:269B, CS:259E and CS:0C0D, then `wait_ticks_2`. Leaving the loop (death, room change, game over) happens inside those calls and has not been traced.
- **Exit**: `save_scores_exit` (CS:012D) restores the video mode and the INT 1Ch vector, writes the score file and calls `INT 21h AX=4C00h`.

### Key procedures

| CS | Name | Evidence |
|---|---|---|
| 0196 | startup | [O] Target of the `JC` after the mode check. Follows the DOS exit in `save_scores_exit`, so it is seeded explicitly |
| 04B7 | print_text | [O] Byte string at SI. `FFh` ends it, `FEh c` sets the colour, `FDh x y` sets the position. Text mode goes through `INT 10h AH=09h`, graphics through `draw_char_cga`. 47 callers |
| 054A | draw_char_cga | [O] Writes to B800h with colour masks from DS:1B72. The 8×8 font is at DS:0CB1 |
| 05BB / 2A13 / 2A18 | wait_key / get_key / flush_keyboard | [O] `INT 16h AH=01h/00h` |
| 05C6 | hide_cursor | [O] `INT 10h AH=01h`, CH=20h |
| 05CD / 05F2 | install / restore_int1c_tick | [O] Save the INT 1Ch vector to DS:1BAA and install CS:0600 |
| 0600 | int1c_tick_isr | [O] Recomputes DS from CS, increments `[1BC2h]` (tick counter) and `[1BB3h]`, then `IRET` |
| 0620 | wait_ticks_2 | [O] Waits until `[1BC2h]` ≥ 2, then clears it. [I] This paces the game at 2 ticks (≈110 ms) |
| 062D / 0640 | score_table_encode / _decode_check | [O] XOR chain over 255 bytes (see below) |
| 0660 | room_setup | [O] Clears buffers and runs the room script interpreter (CS:06E5–0740). [I] Builds the current room |
| 107C | rand_step | [O] `[1BBEh] += ++[1BB3h]`. 18 callers |
| 10A4 | read_keyboard | [O] Matches `INT 16h` keys against 5 four-byte entries at DS:5800. [I] These are the control keys |
| 1162 | player_update | [O] Dispatches the player state through `[1BBAh]` |
| 2772 | toggle_sound | [O] Flips bit 1 of `[1BB2h]`, the mask the sound routines XOR into port 61h (0 = silent), and switches the speaker off |
| 2843 | high_score_insert | [O] Compares the score with the 15 entries at DS:76AE and shifts 17-byte records down |
| 2AD8 | sound_seq_play | [O] Sound sequencer (see stack tricks). Toggles port 61h |
| 2D56 | int1c_sound_isr | [O] Installed only while `sound_seq_play` runs. Plays percussion through `speaker_noise` every second tick |
| 2DD0 | speaker_noise | [O] Writes port 61h DH times, XORing in the `[1BB2h]` mask each time. The delays come from bytes at 0000:BX (low memory used as a noise source), shifted by CH |

### Keys in read_keyboard (ASCII from INT 16h) [O]

| Key | Effect | Status |
|---|---|---|
| Ctrl-C (03h) | Sets `[1BD5h]` = 3. `player_update` then switches the player to state CS:1A05 once | Code [O], meaning not known |
| Ctrl-N (0Eh) | `toggle_sound` | [O] |
| Ctrl-S (13h) | Calls `sound_seq_play` repeatedly while it returns ZF=1, then reads a key. [I] Pause with music | Code [O], meaning [I] |

These keys were read from the code and have not been tested at runtime.

### Sound [O]

The speaker is driven only through port 61h, bits 0–1. The PIT is never programmed, so pitch and duration depend on CPU speed (see the calibration above). A port needs the same kind of pacing as Alley Cat's `CPU_PACE_NS`.

## High-score file SAB_II.SCR [O]

- **Layout:** 255 bytes in memory at DS:76AE: 15 records of a 6-digit ASCII score followed by an 11-character name. Byte 256 (DS:77AD) is always `1Ah`.
- **Encoding (CS:062D):** `enc[i] = plain[i] XOR plain[i+1]` for i = FEh down to 0.
- **Decoding (CS:0640):** `plain[i] = enc[i] XOR plain[i+1]`. The table is valid if every byte is in 20h–7Fh.
- **Load (CS:01B1):** FCB at DS:0D8C (`SAB_II  SCR`), `INT 21h AH=0Fh`, then two 128-byte sequential reads (`AH=14h`) into DS:0BA2. The data is copied to DS:76AE and decoded.
  - A valid table is kept (`score_table_decode_check` returns ZF=1).
  - If the file is missing, a read fails or the table is invalid, the game picks `rand_step` AND 7. For 1–7 it copies default table n, of FFh bytes each, from DS:0004 + (n−1)·FFh (`000200 5 ALIVE` …). For 0 it keeps whatever is at DS:76AE.
- **Save (CS:0137):** delete (`13h`), create (`16h`), encode, two sequential writes (`15h`), close (`10h`). The file is deleted again if any step fails.

## Provenance: SAB2.COM vs. SAB2.EXE

An earlier `SAB2.EXE` (44,324 bytes, dated 2008-08-23, SHA-256 `1ff0837b…bad`) was compared and then replaced by the .COM.

- [O] It is an MZ wrapper around the same ORG 100h image: CS:IP 0000:0100, SS:SP 0000:0000, 4 relocations and a Microsoft LINK header.
- [O] Its instruction stream matches the .COM's except for 3 NOPs at the entry and a 96-NOP block ending in `RET` at CS:2035. All 4 relocations point into that block.
- [O] Its data has an extra "ILLEGAL COPY / PLEASE SWITCH OFF AND RE-BOOT" text and about 0x230 more bytes.
- [I] The EXE is the copy-protected release with the protection routine removed. The .COM is a build without protection. Data offsets differ between the two, so use the .COM addresses only.

## Open items

- 80 of the 106 procedures are still unnamed (`FUN_1000_xxxx`). `fingerprint.txt` tags each one by the hardware it touches and its caller count.
- The meaning of the 105 script opcodes, the room data at DS:3B4C and the 13 cases at CS:221A has not been mapped.
- Ctrl-C's effect (state CS:1A05) is unknown.
- How control leaves `game_loop` has not been traced.
- CS:0463–0498 is skipped by the `JMP` at CS:0461. It is a key-stepping loop that advances `[1BD7h]` and `[1BBCh]`. [I] Leftover debug code. CS:2B18 is a second entry into the sequencer with no reference.

## Files

- `SAB2.asm`: annotated listing of the code area only. It is not reassemblable MASM, and the data area is not emitted. It contains the Ghidra disassembly with names, call-target annotations, seed comments (script opcode, table case, continuation) and the `8E` fix. The 35 one-byte non-code runs (34 MASM padding NOPs and the `00` at CS:2031) are shown as `db`. Instruction lines use the same format as `CAT.asm`.
- `DumpListing.java`: GhidraScript that regenerates the listing, `stats.txt` and `fingerprint.txt` (see BUILDING.md for the command).
- `stats.txt`: counts of prologues, RET/IRET/CALL, INT and port use, and non-code runs.
- `fingerprint.txt`: per-procedure size, calls, callers and hardware tags. The tag rules are in its header.
- `CAT.asm`: the Alley Cat listing. It is kept because `port/tools/asm2nasm.py` still reads it.
