# Building & running the screen-reader Freeciv (`freeciv-sr.exe`)

This is the screen-reader-friendly Win32 build of Freeciv. The toolchain is
**llvm-mingw + vcpkg (x64-mingw-static) + standalone ICU**.

## Prerequisites

- [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), by default at
  `C:\Qt\Tools\llvm-mingw1706_64`. Set `FREECIV_SR_MINGW` to its `bin` folder
  if it is elsewhere.
- [vcpkg](https://github.com/microsoft/vcpkg), found through `VCPKG_ROOT`.
- [Meson](https://mesonbuild.com) and Python.
- Git for Windows. Its `sh` and `gzip` are found automatically; set
  `FREECIV_SR_SH` to use a different folder.
- Three MSYS2 `mingw64` packages, extracted under `build-deps/mingw-pkgs/` so
  that each `mingw64` folder sits at the path below. They aren't in the repo
  because they're large:
  - `mingw-w64-x86_64-icu` at `icu-extract/mingw64`
  - `mingw-w64-x86_64-gcc-libs` at `gcc-libs-extract/mingw64`
  - `mingw-w64-x86_64-libwinpthread` at `winpthread-extract/mingw64`
- Optional runtime DLLs: see `build-deps/runtime-extras/README.md`.

One script — `build-sr.ps1` — handles every phase of the build. Flags pick
which phase(s) to run.

## Quick reference

```powershell
# From the repo root
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-sr.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\run-sr.ps1
```

No flags = **compile + stage** (the inner loop you run after every code edit).

## Phases

| Flag | What it does | When to use |
|------|--------------|-------------|
| `-Deps` | `vcpkg install curl libiconv pkgconf sqlite3 zlib` | Once, at first setup. |
| `-Configure` | Generate `build-deps/native-mingw.generated.ini`, then rebuild `build-sr/` with `meson setup`. If setup fails, the previous `build-sr/` is restored. | After adding/removing source files, changing meson options, or moving vcpkg. |
| `-Compile` | `meson compile -C build-sr` + refresh runtime DLLs in `build-sr/`. | After every source edit. |
| `-Stage` | Copy exe + runtime DLLs into `dist-sr/`. | After compile, before `run-sr.ps1`. |
| `-Full` | All four, in order. | From-scratch bootstrap. |

Flags are combinable: `.\build-sr.ps1 -Configure -Compile -Stage`.

### Output of compile
- `build-sr/freeciv-sr.exe` — the screen-reader client
- `build-sr/freeciv-server.exe` — the bundled server
- `build-sr/libfreeciv.dll` — shared library both exes need

If you've added a new `.c` file (e.g. `client/gui-sr/sr_loadgame.c`),
make sure it's listed in **both** `client/gui-sr/Makefile.am` *and* the
root `meson.build` (search for `'client/gui-sr/`), then run
`.\build-sr.ps1 -Configure -Compile -Stage`.

### Output of stage
A self-contained `dist-sr/` you can run from anywhere: the exe plus every
required runtime DLL (ICU, libgcc, libstdc++, libwinpthread, libfreeciv,
and the vendored extras). Runtime DLLs are also kept next to
`build-sr/freeciv-sr.exe` so launching that copy directly works too.

## Run

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\run-sr.ps1
```

Runs `dist-sr/freeciv-sr.exe` with `FREECIV_DATA_PATH` pointed at the
in-tree `data/` directory, tees stderr to `dist-sr/_runtime.log`.
Extra args are forwarded to the exe.

## Verifying you're running the latest build

After staging, these should match:

```powershell
Get-Item .\build-sr\freeciv-sr.exe, .\dist-sr\freeciv-sr.exe |
  Select-Object FullName, LastWriteTime
```

If `dist-sr` is older than `build-sr`, rerun with `-Stage`. If `build-sr`
is older than your edits, the compile didn't re-link — usually meson
missed a new file, so rerun with `-Configure`.

## Smoke test (no-launch sanity check)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\smoke-sr.ps1
```

Spawns the client, pipes a batch of commands, exits. Should finish in
~5 s with no crashes. Output goes to `dist-sr/_test-*.log`.

## Runtime extras (`build-deps/runtime-extras/`)

Some DLLs live next to the exe but are **not** produced by the build
and **not** picked up by vcpkg: Dr. MinGW's crash handler
(`exchndl.dll`, `mgwhelp.dll`) and the NVDA controller client
(`nvdaControllerClient64.dll`). They aren't in the repo. Put them in
`build-deps/runtime-extras/`; `-Compile` copies them into `build-sr/`
and `-Stage` copies them into `dist-sr/`, so a `meson setup --wipe`
doesn't lose them.

To refresh: download the latest Dr. MinGW x64 zip from
<https://github.com/jrfonseca/drmingw/releases> and extract
`exchndl.dll` + `mgwhelp.dll` into that folder. Copy
`nvdaControllerClient64.dll` from your NVDA install
(`%APPDATA%\nvda\` or `%ProgramFiles%\NVDA\`). Missing files are
skipped with a log line; the build still succeeds.

## Map tool (`freeciv-sr-tools.exe`)

A separate companion binary that ships in `dist-sr/` next to the main
exe. Its first sub-mode is a **random map generator**: a chain of
keyboard-only pickers (ruleset, mapsize, generator, landmass,
temperature, huts, AI opponents, seed, save filename) that drives the
bundled `freeciv-server.exe` headlessly and writes a `.sav.bz2` straight
into your saves folder.

```powershell
.\dist-sr\freeciv-sr-tools.exe
```

The save is a **pregame snapshot** with the seed baked in. Loading it
in `freeciv-sr.exe` via `Load game…` (Ctrl+O) drops you into the
pregame lobby attached to one of the AI players; press **Ctrl+Shift+N**
(or Game → Pick nation…) to open the nation picker, then `begin` to
start. The map generates from the saved seed, so reloading the same
file always produces the same map. Regular in-game saves and autosaves
preserve all map settings automatically (they're part of the
savegame's `game.info` block).

Future sub-modes (Phase 5b: tile-by-tile editor) plug into the same
top-level mode picker — the binary name is generic on purpose.

## Where save files live

The bundled server writes to:
```
%APPDATA%\.freeciv\saves\
```
That's the directory the **Load game…** dialog (Ctrl+O) scans. Move
files in/out of there to test the load flow.

## Common pitfalls

- **"meson: command not found"** — open a *new* shell after installing
  meson, or invoke it via its full path.
- **Missing `libicuuc78.dll` at startup** — you ran `freeciv-sr.exe`
  without staging. Either run `.\build-sr.ps1 -Stage` first, or launch
  via `run-sr.ps1` which uses the staged copy.
- **Edits don't show up in the running game** — you forgot `-Stage`.
  The OS is happily running an older `dist-sr/freeciv-sr.exe`.
- **Linker error after adding a new file** — file not registered in
  both `Makefile.am` and `meson.build`, or `-Configure` wasn't re-run
  since the registration.
