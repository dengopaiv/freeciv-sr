# freeciv-sr project notes

Working notes for this fork. `BUILD-SR.md` covers building and running.

## What this is

`client/gui-sr/` is a standalone, accessible Freeciv client, comparable in scope to `gui-gtk-3.22`, `gui-qt` and `gui-sdl2`.

- It isn't an NVDA synthesizer or add-on. It only uses NVDA to speak.
- It isn't a layer on top of another client. Every dialog is implemented directly in `gui-sr`, and every game feature the other clients expose should eventually exist here too.

## Repository

- `origin` is this fork and `upstream` is `freeciv/freeciv`. Work happens on `sr-client`, the default branch.
- Update from upstream by merging new release tags into `sr-client`, not by rebasing.
- Tracked files use LF, like upstream. Set `core.autocrlf=false` in a new clone: converting edited files to CRLF makes every line look changed and breaks merges.
- Development before November 2026 wasn't under version control, so the first commit on `sr-client` is one large snapshot.
- Not in git: `build-sr/`, `dist-sr/`, `build-deps/mingw-pkgs/` and the runtime DLLs.

## Adding features

**Reference client:** look at `client/gui-gtk-3.22/` first, usually under the same filename (`citydlg.c`, `dialogs.c`, `gamedlgs.c`), and match its `send_packet_*` calls. An April 2026 audit found it covers every gameplay feature; `gui-qt` and `gui-sdl2` differ only in HUD and widget style.

**Dialogs:** use the keyboard pickers in `client/gui-sr/sr_pick.h` instead of modal widgets.

- `sr_pick_int` is a single-line integer editor.
- `sr_pick_enum` is a listbox of string choices.
- Both are non-blocking: they open a popup and call back on commit. Build multi-step flows by chaining callbacks, with state in a heap-allocated struct passed as `userdata`. `ratesdlg.c` chains tax, luxury and science this way.
- Keep the real bodies inside `#ifdef _WIN32`, matching the guards in `sr_pick.h`.

**Commands:** register a `{ word, alias, fn }` entry in `simple_cmds[]` in `client/gui-sr/sr_cmd.c`. Menu and Ctrl-key hooks in `menu.c` call the same handlers.

**Speech and log:** `sr_say(utf8, 1)` makes an interrupting announcement, and `sr_log_append(utf8)` writes to the reviewable log.

**New source files:** register them in both `client/gui-sr/Makefile.am` and the root `meson.build`, then run `build-sr.ps1 -Configure`.

## UX direction

An average player should never need the command line or server chat commands to play. Every common action should have its own command word, menu hook or key, backed by a picker or a packet send. Advanced users can still type `/` commands.

Remaining chat-command fallbacks are tolerated technical debt, such as `/aitoggle` and the AI difficulty commands in `plrdlg.c`, or the difficulty command in `sr_newgame.c`. When proposing work, prefer "what picker does this need?" over "what should the player type?"

## Map editing

- `freeciv-sr-tools.exe` (`client/gui-sr-tools/`) is a separate program with a random map generator built from pickers. Further modes plug into its top-level mode picker.
- The main client also has a command-driven editor in `client/gui-sr/sr_mapedit.c`: the `editor`, `terrain`, `extra`, `savescen`, `newedit` and `oceanize` commands, plus terrain shortcuts.
- A tile editor in the tools program was planned but not built. The editor is still open for more work, ideally wrapped in keyboard pickers over the existing packet-level editor API.

## NVDA DLLs

`client/gui-sr/sr_speech.c` loads `nvdaControllerClient64.dll` by plain name with `LoadLibraryA`, so the normal DLL search order applies. Copy it next to `freeciv-sr.exe` from an NVDA install: `%APPDATA%\nvda\`, `%ProgramFiles%\NVDA\` or `%ProgramFiles(x86)%\NVDA\`. `build-deps/runtime-extras/` is the place to keep it so that the build stages it automatically.

## Status

Parity phases recorded as finished in April 2026; check the code before relying on these, because the original plan file no longer exists:

1. Gameplay blockers: rates, multipliers, government chooser, worklist editor.
2. City depth: citizens, happiness, CMA, global worklists.
3. Unit and action polish: rally points, detailed actions, full player dialog actions.
4. Infrastructure and message polish: infrastructure dialog, message dialog, intel wonder list, demographics, top five cities.
5. Random map generator in the tools program.
6. Moving command-line-only features to pickers: AI toggle and level pickers, plus the `demographics`, `topcities`, `wonders` and `achievements` commands.

Open plans live in `PLAN-NEW-GAME-DIALOG.md` and `PLAN-OPTION-PROFILES.md`. In the profiles plan, phases 2 to 4 aren't implemented yet.
