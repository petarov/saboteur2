# Saboteur II (x86-64 recomp)

[![version](https://img.shields.io/badge/release-1.1-orange)](https://github.com/petarov/saboteur2/releases/latest)
[![platforms-windows-mac-linux](https://img.shields.io/badge/platforms-windows,%20mac,%20linux-blue)](https://github.com/petarov/saboteur2/releases/latest)
[![license-unlicense](https://img.shields.io/badge/license-Unlicense-green)](https://github.com/petarov/saboteur2/blob/main/LICENSE)

A recompilation of [Saboteur II: Avenging Angel](https://en.wikipedia.org/wiki/Saboteur_II:_Avenging_Angel) (by Clive Townsend, Durell Software, 1987) from its original DOS executable to `x86_64` platforms.

If you like this game, make sure to go and check Clive Townsend's [Saboteur 2 remake](https://www.clivetownsend.com/downloads2.php).

> [!IMPORTANT]
> About 99% of the code is LLM-generated and only roughly reviewed. If you have concerns about AI-generated code, avoid this project.

| Menu screen | In-game |
|---|---|
| [![Title screen (Menu)](https://media.githubusercontent.com/media/petarov/saboteur2/refs/heads/main/screenshots/scr01.png)](https://media.githubusercontent.com/media/petarov/saboteur2/refs/heads/main/screenshots/scr01.png) | [![In-game screen](https://media.githubusercontent.com/media/petarov/saboteur2/refs/heads/main/screenshots/scr02.png)](https://media.githubusercontent.com/media/petarov/saboteur2/refs/heads/main/screenshots/scr02.png) |

The [whole game world](screenshots/world.png) pre-rendered in a PNG file.

## Platforms and downloads

| Platform | Latest release | Package to choose |
|---|---|---|
| [Web](https://petarov.github.io/saboteur2/) | [1.1](https://github.com/petarov/saboteur2/releases/latest) | Play in your browser |
| Windows x64 | [1.1](https://github.com/petarov/saboteur2/releases/latest) | `windows-x86_64` ZIP |
| Linux x64 | [1.1](https://github.com/petarov/saboteur2/releases/latest) | `linux-x86_64` archive |
| macOS Intel (x64) | [1.1](https://github.com/petarov/saboteur2/releases/latest) | `macos-x86_64` ZIP |
| macOS Apple Silicon (ARM64) | [1.1](https://github.com/petarov/saboteur2/releases/latest) | `macos-x86_64` ZIP — runs under Rosetta 2 |

The macOS build is not signed or notarized: allow it under *Privacy & Security* or run `xattr -dr com.apple.quarantine <folder>`.

## Controls

- Use the cursor keys to move and `SPACE` to attack or pick up things
- `Esc` / `Ctrl-C`: terminates mission and goes back to main menu
- `Ctrl-N`: turns the sound on/off
 
### New (not in the original game):

- `F10`, `Ctrl-W` and `Ctrl-Q`: quits at any time
- `Ctrl-I`: (cheat) toggles invincibility: damage no longer drains the energy bar
- `Ctrl-O`: (cheat) toggles one-hit kills: every hit knocks the enemy down
- `Ctrl-P`: (cheat) adds $100 to PAY
- `Ctrl-J`: (cheat) adds 100 to the timer, up to 999
- `Ctrl-K`: toggles responsive controls (on by default): the cursor keys and `SPACE` act while held and stop on release, and every `SPACE` press is a new attack. Off restores the original (classic) feel.
- `Alt-Enter` / `F11` (macOS: `Cmd-Enter` / `Cmd-F11`): toggles fullscreen. The game starts in fullscreen by default. Use `--window` on the command line to start in a window.

### Mission codes

There are 9 missions, named after the nine [Kuji-in](https://en.wikipedia.org/wiki/Kuji-in) ranks. Finishing a mission shows the code for the next one. To start a later mission, press `M` in the main menu and type its code.

| Code | Level | Name | Timer | Objectives |
|---|---|---|---|---|
| (`none`) | 1 | RIN - Strength of mind and body | 999 | Kill enemy guards, escape from building via tunnels, smash through fence on motorbike |
| `JONIN` | 2 | KYO - Direction of energy | 901 | Kill enemy guards, escape from building via tunnels |
| `KIME` | 3 | TOH - Harmony with the universe | 801 | Kill enemy guards, collect 2 pieces of paper tape, escape from building via tunnels, smash through fence on motorbike |
| `KUJI KIRI` | 4 | SHA - Healing of self and others | 751 | Kill enemy guards, collect 5 pieces of paper tape, disable electrified fence, escape from building via tunnels, smash through fence on motorbike |
| `SAIMENJITSU` | 5 | KAI - Premonition of danger | 701 | Kill enemy guards, collect 7 pieces of paper tape, disable electrified fence, escape from building via tunnels, smash through fence on motorbike |
| `GENIN` | 6 | JIN - Knowing the thoughts of others | 651 | Kill enemy guards, collect 9 pieces of paper tape, insert tape in missile console, disable electrified fence, escape from building via tunnels, smash through fence on motorbike |
| `MI LU KATA` | 7 | RETSU - Mastery of time and space | 601 | Kill enemy guards, collect 11 pieces of paper tape, insert tape in missile console, escape from building via tunnels |
| `DIM MAK` | 8 | ZAI - Control of nature's elements | 551 | Kill enemy guards, collect 14 pieces of paper tape, insert tape in missile console, escape from building via tunnels, smash through fence on motorbike |
| `SATORI` | 9 | ZEN - Enlightenment | 999 | Kill enemy guards, collect 14 pieces of paper tape, insert tape in missile console, disable electrified fence, escape from building via tunnels, smash through fence on motorbike |

## Building

See [BUILDING.md](BUILDING.md) for requirements, build steps, the source layout and how the port works.

## License

[Unlicense](LICENSE)
