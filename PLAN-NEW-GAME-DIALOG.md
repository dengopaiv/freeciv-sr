# New Game dialog — phased overhaul

## Background

`client/gui-sr/sr_newgame.c` currently exposes 16 rows but suffers from three
problems:

1. **Values silently don't match what the server accepts.** `mapsize` sends
   "small/normal/large/huge" — the server only understands `FULLSIZE`,
   `PLAYER`, `XYSIZE`. `barbarians` sends "hutsonly" — should be `HUTS_ONLY`.
   `generator` has an invalid `"pseudofractal"` entry. Server rejects, falls
   back to default, user thinks their choice worked.
2. **Companions are missing.** `mapsize=FULLSIZE` needs `size`, `PLAYER` needs
   `tilesperplayer`, `XYSIZE` needs `xsize`/`ysize`. None are exposed, so no
   matter what "size" the user picks, the actual map dimensions are whatever
   the server default is.
3. **The "everyone clumps on one continent" complaint** is caused by the
   missing `startpos` row. The server's island generator special-cases
   `startpos`: without `SINGLE`, even ISLAND lumps players together.

User direction: versatility is a feature, 20–30 rows is fine. Phases are
independent and each lands separately and is building-testable.

## Phase 1 — Correctness + missing rows + presets

Scope: make existing rows actually work, add the rows that are missing, and
seed 8–10 macro presets so the user never has to build a conflicting combo
from scratch.

Row changes:

| Row | Action | Values / range |
|---|---|---|
| `mapsize` | **fix** | `FULLSIZE`, `PLAYER`, `XYSIZE` (real enum) |
| `size` | **add** (FULLSIZE companion) | int 1–2048, default 4 (thousand tiles) |
| `tilesperplayer` | **add** (PLAYER companion) | int 1–1000, default 100 |
| `xsize` / `ysize` | **add** (XYSIZE companions) | int 16–5000, default 64 |
| `startpos` | **add** | `DEFAULT`, `SINGLE`, `2or3`, `ALL`, `VARIABLE` |
| `separatepoles` | **add** | bool, default on |
| `startcity` | **add** | bool, default off |
| `generator` | **fix** | `SCENARIO`, `RANDOM`, `FRACTAL`, `ISLAND`, `FAIR`, `FRACTURE` (drop bogus `pseudofractal`) |
| `barbarians` | **fix** | `DISABLED`, `HUTS_ONLY`, `NORMAL`, `FREQUENT`, `HORDES` |
| `Preset…` | **add** (top of list) | 8–10 named macros |

Preset table (each applies 5–10 options atomically):

- **Classic continent** — `generator=FRACTAL`, `startpos=DEFAULT`, `mapsize=FULLSIZE`, `size=4`, `landmass=30`.
- **Small islands** — `generator=ISLAND`, `startpos=SINGLE`, `mapsize=PLAYER`, `tilesperplayer=150`, `landmass=30`.
- **Fair duel** — `generator=FAIR`, `startpos=SINGLE`, `mapsize=PLAYER`, `tilesperplayer=400`, `aifill=1`.
- **Archipelago** — `generator=ISLAND`, `startpos=2or3`, `mapsize=PLAYER`, `tilesperplayer=200`, `landmass=25`.
- **Pangaea** — `generator=FRACTAL`, `startpos=ALL`, `mapsize=FULLSIZE`, `size=4`, `landmass=60`.
- **Huge long war** — `generator=FRACTAL`, `mapsize=FULLSIZE`, `size=20`, `endturn=2000`, `aifill=10`.
- **Peaceful expansion** — `generator=FRACTAL`, `barbarians=DISABLED`, `aifill=3`, `landmass=50`, `endturn=5000`.
- **Fracture crags** — `generator=FRACTURE`, `mapsize=FULLSIZE`, `size=6`.

Files touched: `client/gui-sr/sr_newgame.c` only.

Exit criteria: pick "Small islands" preset → each player starts on their own
island. All existing row picks continue to work. Server log shows no setting
rejections after Start game.

## Phase 2 — Preflight conflict resolver

Walk a rule table on Start game, before `client_start_server_and_set_page`,
and auto-adjust or speak a warning for known conflicts:

- `generator=ISLAND` + `landmass > 80` → cap `landmass=80` (else server silently
  falls back to FRACTAL, `server/generator/mapgen.c:2436`).
- `generator=ISLAND` + `startpos ∉ {DEFAULT, SINGLE}` → force `startpos=SINGLE`
  (else server forces `2or3`, `mapgen.c:1421`).
- `mapsize=XYSIZE` + isometric topology + odd `xsize` or `ysize` → bump to next
  even.
- `startcity=FALSE` + `startunits=""` → warn, cannot play.
- `aifill > 30` → clamp.

Each adjustment is spoken via `sr_say` before proceeding; hard conflicts abort
Start with a spoken reason.

Exit criteria: pick ISLAND + `landmass=90` + Start → hear "Landmass too high
for Island generator, reducing to 80" and game starts.

## Phase 3 — Server-rejection echo

Currently server rejections arrive as chat messages that the SR client does
not voice. Hook the chat-to-speech path in `client/gui-sr/` to match lines
that begin with "Value out of range" / "setting" / "illegal" and route them
to `sr_say` so silent failures become audible.

Exit criteria: force an out-of-range value via custom CLI (bypassing Phase 1/2
validators) and hear the server's rejection spoken.

## Phase 4 — Dependency-aware row labels

Companion rows stay always visible (consistent with "versatility over
simplicity") but annotate their label with `(active)` or
`(inactive — mapsize=PLAYER would enable this)`. No rows are hidden; the
suffix tells the user which companion the server is actually consulting.

Also:
- `temperature` marked inactive for `generator=SCENARIO`.
- `startunits` marked `(goes to founded city)` when `startcity=TRUE`.

Implementation: recompute in `populate_list()` every rebuild.

Exit criteria: toggle mapsize through its 3 methods and watch the
(active)/(inactive) markers track on `size`, `tilesperplayer`, `xsize`,
`ysize` in real time.

## Phase 5 — Focus-time help

Hook `LBN_SELCHANGE` on the new-game listbox. For each `NG_ROW_*` emit one
non-interrupting sentence via `sr_say(msg, 0)` so arrow-key navigation
doubles as a tutorial. Help strings drawn from `server/settings.c` N_(…)
docs, trimmed to one sentence each.

Exit criteria: arrow through every row and hear a meaningful explanation
on focus.

---

Each phase is independent. Land in order; revisit order only if player
feedback shifts priorities.
