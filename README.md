# Crazy Taxi - Static Recompilation

A **static recompilation** of Crazy Taxi (Sega, 1999) from the original Dreamcast SH-4 binary to native x86-64 C code. This is **not** an emulator — the original game code is translated ahead-of-time into C functions that compile natively on modern hardware.

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                   Crazy Taxi Game                    │
│           (11,561 recompiled C functions)            │
├─────────────────────────────────────────────────────┤
│                  SH-4 CPU State                      │
│       (registers, memory map, FPU state)             │
├──────────────┬──────────────┬───────────────────────┤
│  PowerVR2    │    AICA      │    Maple Bus           │
│  GPU (Holly) │  Sound CPU   │  (Controllers)         │
│  → OpenGL    │  → SDL2 Audio│  → SDL2 Input          │
├──────────────┴──────────────┴───────────────────────┤
│              Platform Layer (SDL2)                    │
│         (Window, Input, Audio, Timing)               │
└─────────────────────────────────────────────────────┘
```

### How It Works

1. **Disc Extraction** — The GD-ROM disc image is parsed and all game files are extracted (1ST_READ.BIN, textures, models, sound, etc.)
2. **SH-4 Disassembly** — The 1.47 MB game executable (SH-4 CPU) is disassembled and 11,561 functions are identified via prologue detection and call graph analysis
3. **Static Recompilation** — Each SH-4 function is translated to an equivalent C function operating on a `SH4CPU` state struct. PC-relative data loads are resolved at recompile time.
4. **Hardware Abstraction** — Dreamcast hardware (PVR2 GPU, AICA sound, Maple controllers, GD-ROM) is reimplemented using modern APIs (OpenGL, SDL2)
5. **Native Compilation** — The generated C code compiles with any standard C compiler (GCC, Clang, MSVC) to a native executable

### Key Differences from Emulation

| | Emulation | Static Recompilation |
|---|---|---|
| CPU | Interpret/JIT each instruction | Pre-translated to native C |
| Performance | Runtime overhead | Near-native speed |
| Accuracy | Cycle-accurate possible | Function-level accuracy |
| Hardware | Full HW simulation | Targeted reimplementation |

## Building

### Prerequisites

- CMake 3.16+
- C compiler (GCC, Clang, or MSVC)
- SDL2 (optional, for windowed mode)

### Build

```bash
mkdir build && cd build
cmake ..
cmake --build . --parallel
```

Without SDL2, the project builds in headless mode for testing.

### Running

Place the extracted game files in `disc_extract/` and run:

```bash
./crazytaxi [path_to_disc_extract]
```

## Project Structure

```
crazytaxi/
├── CMakeLists.txt              # Build system
├── include/
│   ├── recompiler/
│   │   └── sh4_cpu.h           # SH-4 CPU state structure
│   ├── hal/
│   │   └── dc_hardware.h       # Dreamcast hardware interface
│   ├── game/
│   │   └── game_functions.h    # 11,561 recompiled function declarations
│   └── platform/
│       └── platform.h          # Platform abstraction (SDL2/Win32)
├── src/
│   ├── main.c                  # Entry point & game loop
│   ├── recompiler/
│   │   └── sh4_cpu.c           # CPU state & memory access
│   ├── hal/
│   │   └── dc_hardware.c       # Hardware register emulation
│   ├── game/
│   │   ├── game_code_000.c     # Recompiled functions (batch 0)
│   │   ├── ...                 # ... (24 source files total)
│   │   ├── game_code_023.c     # Recompiled functions (batch 23)
│   │   └── dispatch_table.c    # Address → function lookup table
│   └── platform/
│       └── platform_sdl2.c     # SDL2 windowing & input
└── tools/
    ├── extract_gdi.py          # GD-ROM disc image extractor
    ├── sh4_disasm.py           # SH-4 disassembler & analysis
    └── static_recompile.py     # SH-4 → C static recompiler
```

## Game Data

Original Dreamcast disc contents (extracted from GD-ROM Track 3):

| File | Size | Description |
|------|------|-------------|
| 1ST_READ.BIN | 1.47 MB | Main executable (SH-4 code) |
| AICADRV.BIN | 56 KB | AICA sound driver |
| TEXDC0-3.BIN | 4.5 MB | Texture data |
| POLDC0-3.BIN | 2.8 MB | Polygon/3D model data |
| COLDC1-3.BIN | 8.3 MB | Collision data |
| MOTDC.BIN | 2.7 MB | Motion/animation data |
| SPR*.BIN | 3.8 MB | Sprite data |
| SNDDC*.BIN | 2.1 MB | Sound effects |
| SONG01.AFS | 30 MB | Music (streamed) |
| VOICE01.AFS | 13.5 MB | Voice lines |
| BINC1-3.AFS | 28.4 MB | Cutscene/misc archives |

---

## Progress Tracker

### Phase 1: Foundation ✅
- [x] GD-ROM disc image parser (CUE/BIN with LBA offset correction)
- [x] ISO9660 filesystem extraction (all 50 game files)
- [x] IP.BIN header parsing (game metadata)
- [x] SH-4 disassembler (full instruction set decode)
- [x] Function boundary detection (11,561 functions found)
- [x] Instruction statistics & analysis

### Phase 2: Static Recompiler ✅
- [x] SH-4 → C instruction translator (arithmetic, logic, shifts)
- [x] Branch/jump handling with delay slots
- [x] BSR/JSR call translation (direct calls)
- [x] PC-relative data load resolution (MOV.L @(disp,PC))
- [x] Floating-point instruction translation (FADD, FMUL, FDIV, FIPR, etc.)
- [x] Function dispatch table (binary search lookup)
- [x] Multi-file output (24 source files, parallel compilation)

### Phase 2.5: Build & Compilation 🔧
- [x] CMake build system (MSVC / Visual Studio 17 2022)
- [x] Multi-library split for parallel compilation (5 game libs)
- [x] Label error post-processing (goto → function call conversion)
- [x] Branch-outside-function → tail call conversion
- [ ] Successful full compilation (in progress — large files take time with MSVC)
- [ ] Linker pass (resolve undefined func_XXXXXXXX references via stubs)
- [ ] First successful link & executable output

### Phase 3: CPU & Memory 🔧
- [x] SH-4 CPU state structure (registers, FPU, system regs)
- [x] Dreamcast memory map (16MB RAM, 8MB VRAM, 2MB AICA)
- [x] Memory-mapped I/O routing
- [x] P1/P2 address translation (cached/uncached mirrors)
- [ ] Store Queue emulation (SQ → VRAM DMA)
- [ ] Cache behavior (where game-visible)

### Phase 4: Hardware Abstraction 🔧
- [x] Hardware register framework (SB, PVR, Maple, AICA, GD-ROM)
- [x] PVR2 register stubs (ID, reset, framebuffer, render trigger)
- [x] Maple controller state (buttons, triggers, analog sticks)
- [x] AICA ARM reset control
- [x] Interrupt status registers (ISTNRM, ISTEXT, ISTERR)
- [ ] PVR2 Tile Accelerator (vertex submission, polygon lists)
- [ ] PVR2 rendering pipeline → OpenGL translation
- [ ] Texture format conversion (PVR → OpenGL)
- [ ] AICA sound channel mixing → SDL2 audio
- [ ] ADPCM audio decoding
- [ ] Maple DMA (controller polling protocol)
- [ ] GD-ROM file access (redirect to extracted files)

### Phase 5: Rendering Pipeline ❌
- [ ] OpenGL 3.3 core renderer
- [ ] PVR vertex format → GL vertex array conversion
- [ ] Texture atlas / VQ texture decompression
- [ ] Alpha sorting (opaque, translucent, punch-through lists)
- [ ] Fog table implementation
- [ ] Modifier volumes (shadow/light)
- [ ] Framebuffer output (640x480 → window)

### Phase 6: Audio ❌
- [ ] AICA channel emulation (64 channels)
- [ ] ADPCM decode (Yamaha AICA format)
- [ ] AFS archive parser (for SONG01.AFS, VOICE01.AFS)
- [ ] Streaming audio playback
- [ ] Sound effect mixing

### Phase 7: Game Logic ❌
- [ ] Indirect call resolution (JSR @Rn targets)
- [ ] BIOS syscall stubs
- [ ] Timer/interrupt simulation
- [ ] Save data (VMU) stub
- [ ] Game-specific patches (known issues)

### Phase 8: Polish ❌
- [ ] Full SDL2 keyboard+gamepad mapping
- [ ] Resolution scaling (beyond 640x480)
- [ ] Widescreen support
- [ ] Frame rate unlocking
- [ ] Configuration file
- [ ] Windows/Linux/macOS builds

---

### Binary Analysis Summary

```
Game:        CRAZY TAXI (MK-51035 V1.004)
Developer:   SEGA ENTERPRISES
Release:     1999-12-19
Platform:    SEGA SEGAKATANA (Dreamcast)
Region:      U (USA)

Binary:      1ST_READ.BIN
Size:        1,468,208 bytes (1.4 MB)
Load addr:   0x8C010000
CPU:         Hitachi SH-4 (SH7091)
Functions:   11,561
Instructions: 734,104
FP ops:      34,349 (4.7%)
Decode rate: 58.9% (rest is inline data)
```

## Legal

This project is for educational and preservation purposes. You must own a legitimate copy of Crazy Taxi for Dreamcast. Game data files are not included in this repository.

## License

The recompilation tools and platform code are MIT licensed. The original game code and assets remain property of Sega.
