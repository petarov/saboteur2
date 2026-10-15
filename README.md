# Saboteur II (x86-64 port)

[![version](https://img.shields.io/badge/release-1.0-orange)](https://github.com/petarov/saboteur2.exe/releases/latest)
[![platforms-windows-mac-linux](https://img.shields.io/badge/platforms-windows,%20mac,%20linux-blue)](https://github.com/petarov/saboteur2.exe/releases/latest)
[![license-unlicense](https://img.shields.io/badge/license-Unlicense-green)](https://github.com/petarov/saboteur2.exe/blob/main/LICENSE)

A port of [Saboteur II: Avenging Angel](https://en.wikipedia.org/wiki/Saboteur_II:_Avenging_Angel) (Clive Townsend, Durell Software, 1987) from its original DOS executable to `x86_64` platforms.

> [!IMPORTANT]
> About 99% of the code is LLM-generated and only roughly reviewed. If you have concerns about AI-generated code, avoid this project.


| Main Menu | In-game |
|---|---|
| [![Title screen (Menu)](screenshots/scr01.png)](screenshots/scr01.png) | [![In-game screen](screenshots/scr02.png)](screenshots/scr02.png) |

## Platforms and downloads

| Platform | Latest release | Package to choose |
|---|---|---|
| Windows x64 | [1.0](https://github.com/petarov/saboteur2.exe/releases/latest) | `windows-x86_64` ZIP |
| Linux x64 | [1.0](https://github.com/petarov/saboteur2.exe/releases/latest) | `linux-x86_64` archive |
| macOS Intel (x64) | [1.0](https://github.com/petarov/saboteur2.exe/releases/latest) | `macos-x86_64` ZIP |
| macOS Apple Silicon (ARM64) | [1.0](https://github.com/petarov/saboteur2.exe/releases/latest) | `macos-x86_64` ZIP — runs under Rosetta 2 |

The macOS build is not signed or notarized: allow it under *Privacy & Security* or run `xattr -dr com.apple.quarantine <folder>`.

## Controls

- Use the cursor keys to move and `SPACE` to attack or pick up things
- `Esc` / `Ctrl-C`: terminates mission and goes back to main menu
 
### New (not in the original game):

- `F10`, `Ctrl-W` and `Ctrl-Q`: quits at any time

## Building

See [BUILDING.md](BUILDING.md) for requirements, build steps, the source layout and how the port works.

## License

[Unlicense](LICENSE)
