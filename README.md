# LearnfunPlayfun for Atari 2600

An AI that learns to play Atari 2600 games by watching human gameplay, then plays autonomously using brute-force search over discovered objective functions.

Ported from [LearnfunPlayfun-Revival](https://github.com/AtlasRedux/LearnfunPlayfun-Revival) (NES/FCEUX) to Atari 2600 via the [stella2023](https://github.com/libretro/stella2023) libretro core.

Based on Tom Murphy VII's original [learnfun & playfun](http://tom7.org/mario/) — the paper is a great read if you want to understand the theory.

## How it works

1. **Record** a human playing an Atari game (`recordfun`)
2. **Learn** objective functions from the recording by finding lexicographic orderings on RAM bytes that correlate with "progress" (`learnfun`)
3. **Play** the game autonomously by searching over possible input sequences and scoring them against the learned objectives (`playfun`)
4. **Replay** the AI's best run visually with sound (`replayfun`)

The approach is entirely game-agnostic — it knows nothing about any specific game. It discovers what "progress" means by observing which RAM values tend to increase when a human plays well.

## Tools

| Executable | Purpose |
|---|---|
| `recordfun` | Play the game with keyboard and save inputs as `.a26inp` |
| `learnfun` | Analyze a recording to discover objective functions |
| `playfun` | AI plays the game using learned objectives (master/helper distributed search) |
| `replayfun` | Watch a `.a26inp` replay with video and audio |
| `scopefun` | Visualize RAM activity during a replay |
| `pinviz` | Visualize input pin activity |
| `tasbot_gui.pyw` | Python/tkinter GUI for the full workflow |

## Building

### Prerequisites

- **Visual Studio 2022** (MSVC v143 toolset)
- **CMake** 3.21+
- **vcpkg** (with `VCPKG_ROOT` environment variable set)
- **SDL2** (via vcpkg: `vcpkg install sdl2:x64-windows`)
- **Other deps** installed automatically via vcpkg: protobuf, zlib, libpng, lz4

### Clone with stella2023

```bash
git clone https://github.com/AtlasRedux/LearnfunPlayfunAtari2600.git
cd LearnfunPlayfunAtari2600
git submodule update --init --recursive
```

### Build

```bash
cd tasbot
cmake --preset default
cmake --build --preset release
```

Or use the included `build.bat` from the project root.

Binaries are output to `tasbot/build/Release/`.

## Usage

All tools read a `config.txt` file in the working directory:

```
game pitfall
rom pitfall.a26
movie pitfall.a26inp
```

### 1. Record gameplay

Place your ROM (e.g. `pitfall.a26`) and `config.txt` in the same directory as the executables, then:

```
recordfun.exe
```

Play the game with arrow keys, Z/Space for fire, ESC to stop and save. The recording is saved as `<game>.a26inp`.

**Keyboard controls:**
- Arrow keys: joystick directions
- Z / Space: fire button
- Enter: console reset
- Tab: console select
- ESC: quit and save

### 2. Learn objectives

```
learnfun.exe
```

Reads the `.a26inp` recording and outputs `<game>.objectives` — the discovered objective functions.

### 3. Run the AI

Start helpers (one per CPU core):

```
playfun.exe --helper 29000
playfun.exe --helper 29001
...
```

Then start the master:

```
playfun.exe --master 29000 29001 29002 ...
```

The AI writes its best run so far to `<game>-playfun-futures-progress.a26inp` periodically.

### 4. Watch the replay

```
replayfun.exe
```

Automatically finds the playfun progress file. Controls: Space (pause), arrow keys (speed/step), R (restart), M (mute), ESC (quit).

## Architecture

- **Emulator wrapper** (`emulator.cc`): Abstracts the stella2023 libretro core behind a clean C++ API — initialize, step, save/load state, read RAM, get video/audio.
- **Objective discovery** (`objective.cc`, `weighted-objectives.cc`): Finds lexicographic orderings on RAM that correlate with human gameplay progress. Works on arbitrary byte arrays — fully generic.
- **MARIONET protocol** (`marionet.proto`, `netutil.cc`): Protobuf-based TCP protocol for distributed master/helper search. Sends opaque byte blobs (emulator states, input sequences).
- **Input format** (`.a26inp`): Simple text format — one line per frame, 5 characters `UDLRF` where `.` means not pressed. Human-readable and diffable.

## Atari 2600 vs NES

| | NES (original) | Atari 2600 (this port) |
|---|---|---|
| Emulator | FCEUX (embedded) | stella2023 (libretro) |
| RAM | 2048 bytes | 128 bytes |
| Inputs | 8 buttons (RLDUTSBA) | 5 inputs (UDLRF) + console switches |
| State size | ~10-20 KB | ~1.2 KB |
| Input format | FM2 | .a26inp |
| Recording | FCEUX built-in | recordfun (SDL2) |

The smaller RAM and state sizes make the Atari version significantly faster per-frame than the NES version.

## Credits

- **Tom Murphy VII** — original learnfun/playfun concept, NES implementation, and the [brilliant paper](http://tom7.org/mario/)
- **AtlasRedux** — NES revival and Atari 2600 port
- **stella2023** — Atari 2600 libretro core by the Stella team
- **cc-lib** — Tom Murphy VII's utility library

## License

Same terms as the original learnfun/playfun. See individual source files for details.
