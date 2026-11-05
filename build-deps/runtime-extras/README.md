# Runtime extras

DLLs shipped alongside `freeciv-sr.exe` that are **not** produced by the
build and **not** picked up automatically by `vcpkg` / the toolchain.
`build-sr.ps1 -Compile` and `build-sr.ps1 -Stage` copy everything in
this folder into `build-sr/` and `dist-sr/` so the extras survive
`meson setup --wipe`.

| File | Source | Purpose |
|------|--------|---------|
| `exchndl.dll` | Dr. MinGW release zip — https://github.com/jrfonseca/drmingw/releases | Crash handler — writes `freeciv-sr.RPT` with a symbolized stack trace on unhandled exceptions. |
| `mgwhelp.dll` | Same Dr. MinGW release zip. | Symbol-resolution helper used by `exchndl.dll`. Must sit next to it. |
| `nvdaControllerClient64.dll` | Local NVDA install (`%APPDATA%\nvda\` or `%ProgramFiles%\NVDA\`). | Lets `sr_speech.c` route speech through a running NVDA. |

To refresh on a new machine: download the latest Dr. MinGW Windows x64
release, extract the two `.dll` files above into this folder, and copy
`nvdaControllerClient64.dll` from your NVDA install.

The copy step in `build-sr.ps1` skips any missing file silently (with
a log line), so a fresh clone without NVDA still builds and runs —
speech just falls back to stderr.
