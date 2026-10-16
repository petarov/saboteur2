# Saboteur II (x86-64 recomp)

Work in progress ...

[![version](https://img.shields.io/badge/release-WIP-orange)](https://github.com/petarov/saboteur2.exe/releases/latest)
[![platforms-windows-mac-linux](https://img.shields.io/badge/platforms-windows,%20mac,%20linux-blue)](https://github.com/petarov/saboteur2.exe/releases/latest)
[![license-unlicense](https://img.shields.io/badge/license-Unlicense-green)](https://github.com/petarov/saboteur2.exe/blob/main/LICENSE)

A recompilation of [Saboteur II: Avenging Angel](https://en.wikipedia.org/wiki/Saboteur_II:_Avenging_Angel) (Clive Townsend, Durell Software, 1987) from its original DOS executable to `x86_64` platforms.

> [!IMPORTANT]
> About 99% of the code is LLM-generated and only roughly reviewed. If you have concerns about AI-generated code, avoid this project.

| Menu screen | In-game |
|---|---|
| [![Title screen (Menu)](https://media.githubusercontent.com/media/petarov/saboteur2/refs/heads/main/screenshots/scr01.png)](https://media.githubusercontent.com/media/petarov/saboteur2/refs/heads/main/screenshots/scr01.png) | [![In-game screen](https://media.githubusercontent.com/media/petarov/saboteur2/refs/heads/main/screenshots/scr02.png)](https://media.githubusercontent.com/media/petarov/saboteur2/refs/heads/main/screenshots/scr02.png) |

## Platforms and downloads

| Platform | Latest release | Package to choose |
|---|---|---|
| Windows x64 | [WIP](https://github.com/petarov/saboteur2.exe/releases/latest) | `windows-x86_64` ZIP |
| Linux x64 | [WIP](https://github.com/petarov/saboteur2.exe/releases/latest) | `linux-x86_64` archive |
| macOS Intel (x64) | [WIP](https://github.com/petarov/saboteur2.exe/releases/latest) | `macos-x86_64` ZIP |
| macOS Apple Silicon (ARM64) | [WIP](https://github.com/petarov/saboteur2.exe/releases/latest) | `macos-x86_64` ZIP — runs under Rosetta 2 |

The macOS build is not signed or notarized: allow it under *Privacy & Security* or run `xattr -dr com.apple.quarantine <folder>`.

## Controls

- Use the cursor keys to move and `SPACE` to attack or pick up things
- `Esc` / `Ctrl-C`: terminates mission and goes back to main menu
 
### New (not in the original game):

- `F10`, `Ctrl-W` and `Ctrl-Q`: quits at any time
- `Ctrl-I`: toggles invincibility (cheat): damage no longer drains the energy bar
- `Ctrl-O`: toggles one-hit kills (cheat): every hit knocks the enemy down
- `Ctrl-P`: adds $100 to PAY (cheat)
- `Alt-Enter` / `F11` (macOS: `Cmd-Enter` / `Cmd-F11`): toggles fullscreen. The game starts in fullscreen by default. Use `--window` on the command line to start in a window.

## Building

See [BUILDING.md](BUILDING.md) for requirements, build steps, the source layout and how the port works.

## License

[Unlicense](LICENSE)
