# crazytaxi - Crazy Taxi, statically recompiled

Crazy Taxi (Dreamcast, 1999) translated from SH-4 machine code into C and built
as a native x86-64 binary. Not an emulator: there is no interpreter and no JIT,
and nothing decodes an SH-4 instruction at runtime. The game's 12,750 functions
are turned into C functions ahead of time and compiled by MSVC.

Built on [dcrecomp](https://github.com/sp00nznet/dcrecomp), which does the
translating and provides the hardware layer, and follows its house style: game
repos hold only the bring-up code and title-specific data, fixes to the
recompiler go to dcrecomp.

## Status

Alpha, no tagged release. **It does not draw a frame yet**, so there are no
screenshots. It boots, sets up its interrupt handler, reads the disc and gets a
long way into initialisation, then stops in a wait loop it never leaves. For
the framework rendering a game, see the ChuChu Rocket project.

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
py -3 tools\smoke_test.py
```

It checks the binary loads, the handler is registered, the `0x8C1583F8` loop
clears, the TA sees packets, and no call on the interrupt path goes unresolved.
5/5 pass (2026-10-20). Without the disc it prints `SKIP` and exits 0, which is
what CI and netlab's test VMs see.

## Getting Started

You need your own Crazy Taxi (US) disc image (`.cue` + `.bin`, or `.gdi`).
Nothing in this repository is game data, and nothing here downloads any.

### Quick start

1. Download this repository ([zip](https://github.com/sp00nznet/crazytaxi/archive/refs/heads/main.zip))
   and unzip it. Put your disc's `.cue`/`.bin` files in the same folder if you
   like - Setup finds them there.
2. Double-click **`Setup.cmd`**. It checks for Python, Git, CMake, the Visual
   Studio C++ build tools and vcpkg, and **asks** before installing any that are
   missing (it says how big each is). If it can't find your disc, it asks for
   the path.
3. When it says `Done.`, double-click **`Play Crazy Taxi.cmd`**.

If it stops, it says what to do in one sentence; the details are in
`setup.log`. Run it again after fixing that: steps already done are skipped.
The first build takes 10-20 minutes.

### Step by step

These are the commands Setup runs. Prerequisites:

- Windows 10/11, x64
- Python 3.8+ (`py -3 --version`)
- Git, CMake 3.16+
- Visual Studio 2022 (or its Build Tools) with *Desktop development with C++*
- vcpkg with SDL2 and GLEW: `vcpkg install sdl2:x64-windows glew:x64-windows`

1. Get the code, with the dcrecomp submodule:

   ```
   git clone --recursive https://github.com/sp00nznet/crazytaxi
   cd crazytaxi
   ```

2. Extract the disc:

   ```
   py -3 dcrecomp\tools\extract_gdi.py "Crazy Taxi (US).cue" disc_extract
   ```
   ```
   === Disc layout ===
     track  1: LBA       0..    449  data
     track  2: LBA     450..  1,125  audio
     track  3: LBA  45,000..549,149  data
   ...
   Done! Extracted 105,229,316 bytes total
   ```

3. Translate the executable to C (writes the gitignored `src/game/` and
   `include/game/`). `entries.txt` lists the entry points only reachable at
   runtime (dcrecomp `docs/entries.md`):

   ```
   py -3 dcrecomp\tools\static_recompile.py disc_extract\1ST_READ.BIN src\game include\game --entries entries.txt
   py -3 tools\generate_stubs.py
   ```
   ```
   Static recompilation complete!
     Functions: 12750
     Mid-function entries: 13442
     Source files: 26
     Output: src\game/
   ...
     Added 31 declarations
   ```

4. Build:

   ```
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake
   cmake --build build --config Release --parallel
   ```

   The result is `build\Release\crazytaxi.exe`.

Usual trip-ups:

- **`python` opens the Microsoft Store**, or does nothing: that's Windows' app
  alias. Use `py -3`, or turn the alias off in *Settings > Apps > Advanced app
  settings > App execution aliases*.
- **A tool you just installed isn't found**: PATH changes reach new windows
  only. Open a new terminal.
- **No window when you run it**: CMake didn't find SDL2, so it built the
  headless configuration. Pass the vcpkg toolchain file, and delete `build\`
  if it was configured without it.
- **A zip download has an empty `dcrecomp\` folder**: run Setup (it fetches the
  right commit), or use `git clone --recursive`.

## Usage

```
build\Release\crazytaxi.exe             # run; reads disc_extract\ from the current folder
build\Release\crazytaxi.exe <datadir>   # read the extracted disc from elsewhere
py -3 tools\smoke_test.py                # boot test: 5 checks, prints n/5 passed
```

Debug switches (watchpoints, stack sampling, screenshots, the ABI check) are in
[docs/debugging.md](docs/debugging.md).

## Building from source

The step-by-step route above is the build. `src/main.c` is the bring-up file:
entry point, RAM size, BIOS vectors and the interrupt handler registration -
the only hand-written game-specific C here. `tools/` holds the stub generator,
the smoke test, the setup script, and two disc-parsing debug scripts.

On the lab, [recomp-netlab](https://github.com/sp00nznet/recomp-netlab) builds
it with `netlab build crazytaxi` (clang-cl, headless configuration: SDL2 isn't
on the builders) and `netlab check crazytaxi --on testbox` runs the smoke test.
The disc and `disc_extract\` are never sent.

## Credits and prior art

This exists because other people did the hard parts first. If you use any of
this, credit them too.

- **[KatanaRecomp](https://github.com/sonicfreak1337/KatanaRecomp)**
  (sonicfreak1337) - the other Dreamcast static recompiler, and further along.
  Worth reading before assuming anything here is novel.
- **[Flycast](https://github.com/flyinghead/flycast)** (flyinghead, GPLv2) - the
  reference for how this hardware actually behaves. dcrecomp's hardware
  layer is derived from it (dcrecomp's NOTICE), which is why both repos are
  GPL-2.0.
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

GPL-2.0 - see [LICENSE](LICENSE) and [NOTICE](NOTICE). It builds against
dcrecomp, which is GPL-2.0 because parts of its hardware layer are derived from
Flycast. This repository was MIT until 2026-10-20.

That covers what is in this repository: the build files, `src/main.c`,
`entries.txt`, `tools/` and the documentation. It grants no rights to anything
produced by running the recompiler over a commercial disc image - that output
is a translation of Sega's executable and remains Sega's. Bring your own copy
and generate it yourself.
