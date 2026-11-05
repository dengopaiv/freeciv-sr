# Option profiles — phased plan

## Background

`freeciv-sr` exposes ~150 client options. After the minimal-rendering
defaults landed, we now have two distinct personas:

- **screen-reader user** — wants draw_* off, animations off, captions on.
- **sighted user trying out the repo from GitHub** — wants visuals and
  animations back, maybe different ruleset or difficulty defaults.

A profile facility lets either persona pick a working configuration
without hand-editing 40 rows. Because `freeciv-sr` is a command-driven
client (stdin command queue + embedded `Ctrl+Shift+C` command EDIT), a
profile is **a script of commands** — not a binary options dump. This
matches the existing idiom: anything the user can type, a profile can
replay. Users can also hand-edit profiles in any text editor.

## Phase 1 — Script-style save/load (this phase)

New commands in `client/gui-sr/sr_cmd.c`:

- `setopt NAME VALUE` — set one client option. Supports `bool`, `int`,
  `string`, `enum` types; refuses unknown or unchangeable options with
  a spoken reason.
- `saveprofile NAME` — walks `client_optset`, emits one
  `setopt NAME VALUE` line per changeable option into
  `$FREECIV_STORAGE/sr-profiles/<NAME>.profile`.
- `loadprofile NAME` — reads the profile line-by-line and dispatches
  each line through `sr_cmd_dispatch_line()`. Comments (`;`) and blank
  lines are skipped.
- `listprofiles` — lists `.profile` files in the profile dir.

New file: `client/gui-sr/sr_profile.c/h`.

Exit criteria: `setopt smooth_move_unit_msec 0` toggles the option,
`saveprofile accessible` writes a readable text file,
`loadprofile accessible` reapplies every line, `listprofiles` prints
available profiles.

## Phase 2 — Bundled starter profiles

On first boot, if `$FREECIV_STORAGE/sr-profiles/` is missing or empty,
seed it with three scripts:

- `accessible.profile` — our current minimal-rendering defaults, plus
  `sr_speak_sound_events=on` and a conservative `sr_sound_mute_prefixes`.
- `visual.profile` — every `draw_*` on, `smooth_*` ≥ 200 ms,
  `auto_center_on_unit=on`. Restores what a sighted user would expect
  from a GTK client.
- `duel.profile` — new-game presets pointed at `aifill=1`, `generator=FAIR`,
  `startpos=SINGLE`, `barbarians=DISABLED`.

Files ship in `data/sr-profiles/` and get copied on first boot. Users
can edit the copies freely; re-copying happens only when the target
file doesn't exist.

Exit criteria: fresh user dir → three profiles available on first
`listprofiles`.

## Phase 3 — Auto-load and auto-save

Two new client options:

- `sr_auto_profile` (string) — if non-empty, `loadprofile <value>` runs
  during `gui_ui_init` after `options_load`.
- `sr_save_profile_on_exit` (string) — if non-empty, `saveprofile <value>`
  runs during `gui_ui_exit`.

Common recipe: both set to `"last"` keeps your session persistent
without overwriting the curated starter profiles.

Exit criteria: toggle an option, restart, see it stick.

## Phase 4 — Profile diff

`diffprofile A B` — walks both files, speaks and logs the first N
options whose values differ. Also `diffprofile A` compares profile A to
the currently running state. Useful when troubleshooting "why does this
feel different after I switched profile".

Exit criteria: running diff on `accessible` vs `visual` returns a
realistic list of the ~30 differing options.

## Phase 5 — Selective save

`saveprofile NAME sound` — only save options in category `COC_SOUND`.
Accepted category keywords: `sound`, `graphics`, `interface`, `map`,
`overview`, `network`, `font`. Lets users save focused fragments and
layer them: load base `visual`, then overlay `my-sounds`.

Exit criteria: `saveprofile s1 sound` writes a small file that only
sets `default_sound_*` and friends.

---

Each phase is independent. Phase 1 is the minimum useful version;
Phases 2–5 are convenience layers that can land in any order once
the save/load machinery exists.
