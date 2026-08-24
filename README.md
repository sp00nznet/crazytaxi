# crazytaxi - Crazy Taxi, statically recompiled

Crazy Taxi (Dreamcast, 1999) translated from SH-4 machine code into C and built
as a native x86-64 binary. Not an emulator: there is no interpreter and no JIT,
and nothing decodes an SH-4 instruction at runtime. The game's 12,750 functions
are turned into C functions ahead of time and compiled by MSVC.

Built on [dcrecomp](https://github.com/sp00nznet/dcrecomp), which does the
translating and provides the hardware layer.

**This one does not draw a frame yet.** It boots, sets up its interrupt handler,
reads the disc and gets a long way into initialisation, then stops in a wait
loop it never leaves. If you want to see the framework actually rendering a
game, that is the ChuChu Rocket project, which does.

## State

Working:

- links and runs: 12,750 functions, no unresolved calls on the interrupt path
- registers the VBlank handler the game expects at VBR+0x600, and takes
  interrupts
- extracts and reads the GD-ROM filesystem
- gets through the first initialisation wait loop (the one at `0x8C1583F8`)
- the tile accelerator receives its first packets

Not working:

- **initialisation never returns.** It sits in `impl_8C1582AE`, reached from
  the game's init entry at `func_8C148962` by way of `func_8C02A698` and
  `func_8C0296FE`. Whatever that loop is waiting for, we are not providing it.
- everything downstream of that: no geometry, no frame, no sound, no input.

There is a smoke test that pins the above down, so a regression is obvious:

```
python tools/smoke_test.py
```

It checks the binary loads, the handler is registered, the `0x8C1583F8` loop
clears, the TA sees packets, and no call on the interrupt path goes unresolved.
All five pass today.

## Building

You need your own copy of the game. Nothing in this repository will produce a
playable binary on its own, and no game data is distributed here.

```
# 1. Extract the disc.
python dcrecomp/tools/extract_gdi.py disc.cue disc_extract

# 2. Translate the executable to C. Writes src/game/ and include/game/.
python dcrecomp/tools/static_recompile.py
python tools/generate_stubs.py

# 3. Build.
cmake -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release --parallel 8
```

SDL2 and GLEW come from vcpkg. Without the toolchain file CMake silently
configures a headless build, so if you get no window, that is why.

The recompiler lives in `dcrecomp/tools/` and is the only copy. `tools/` here
holds what is specific to this game: the stub generator, the smoke test, and a
couple of disc-parsing debug scripts.

`src/main.c` is the bring-up file: entry point, RAM size, BIOS vectors, and the
interrupt handler registration. It is the only hand-written game-specific code
here.

## Debugging

dcrecomp carries the tools that get used on this. All off by default:

| Variable | What it does |
| --- | --- |
| `DCRECOMP_HWREG` | first write to each hardware register |
| `DCRECOMP_STACKEVERY=N` | the call chain interrupted, every Nth interrupt |
| `DCRECOMP_WATCH=addr` | write watchpoint, with the chain that did it |
| `DCRECOMP_SCREENSHOT=f.ppm` | write the frame to a PPM |
| `-DABI_CHECK=ON` | flag a function returning with a callee-saved register or SP changed |

A crash prints the faulting context, a backtrace walked from it, and the last
24 indirect-call targets - usually the only useful part, because MSVC's tail
calls flatten the native stack even when it is intact.

`DCRECOMP_STACKEVERY` is what identified the loop above: the same call chain on
every sample means the game has stopped making progress, and the chain names
the function to read.

## Credits and prior art

This exists because other people did the hard parts first. If you use any of
this, credit them too.

- **[KatanaRecomp](https://github.com/sonicfreak1337/KatanaRecomp)**
  (sonicfreak1337) - the other Dreamcast static recompiler, and further along.
  Worth reading before assuming anything here is novel.
- **[Flycast](https://github.com/flyinghead/flycast)** (flyinghead, GPLv2) - the
  reference for how this hardware actually behaves. No Flycast code is used
  here, but the PowerVR2, Holly and AICA register semantics were learned from
  it.
- **[N64Recomp](https://github.com/N64Recomp/N64Recomp)** (Wiseguy) -
  established the static-recompilation-as-preservation approach this copies.
- **[tbg-decomp](https://github.com/lhsazevedo/tokyo-bus-guide-decomp)**
  (lhsazevedo) - matching decompilation of Tokyo Bus Guide, plus an SH-4 object
  simulator. The best prior art for validating SH-4 translation.
- **[KallistiOS](https://github.com/KallistiOS/KallistiOS)** and
  **[dreamcast.wiki](https://www.dreamcast.wiki/)** - hardware documentation.
- Hitachi's **SH-4 Software Manual** - the division step and the FPU encodings
  in dcrecomp are implemented from it directly.

Crazy Taxi is a Sega game. This project is not affiliated with or endorsed by
Sega, and no game code or data is distributed here.

## Licence

MIT - see [LICENSE](LICENSE).

That covers what is actually in this repository: the build files, `src/main.c`,
`tools/`, and the documentation. It grants no rights to anything produced by
running the recompiler over a commercial disc image - that output is a
translation of Sega's executable and remains Sega's. Bring your own copy and
generate it yourself.
