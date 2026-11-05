/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

/* utility */
#include "fcintl.h"
#include "support.h"              /* fc_snprintf, fc_strlcpy */

/* client */
#include "chatline_common.h"
#include "client_main.h"          /* savefile global */
#include "connectdlg_common.h"
#include "options.h"
#include "pages_g.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_pick.h"
#include "sr_play.h"
#include "sr_speech.h"
#include "sr_window.h"

#include "sr_newgame.h"

#define SR_NEWGAME_CLASS   L"FreecivSRNewGame"
#define IDC_NG_LIST        9301
#define IDC_NG_START       9302
#define IDC_NG_CANCEL      9303

/* ---- row definitions --------------------------------------------------- */

enum ng_row_kind {
  NG_ROW_PRESET,            /* Phase 1: macro of 5-10 settings */
  NG_ROW_RULESET,
  NG_ROW_AIFILL,
  NG_ROW_GENERATOR,
  NG_ROW_STARTPOS,          /* Phase 1: per-continent placement */
  NG_ROW_MAPSIZE,
  NG_ROW_SIZE,              /* Phase 1: companion for FULLSIZE */
  NG_ROW_TILESPERPLAYER,    /* Phase 1: companion for PLAYER */
  NG_ROW_XSIZE,             /* Phase 1: companion for XYSIZE */
  NG_ROW_YSIZE,             /* Phase 1: companion for XYSIZE */
  NG_ROW_TOPOLOGY,
  NG_ROW_WRAP,
  NG_ROW_LANDMASS,
  NG_ROW_SEPARATEPOLES,     /* Phase 1 */
  NG_ROW_TEMPERATURE,
  NG_ROW_HUTS,
  NG_ROW_DIFFICULTY,
  NG_ROW_BARBARIANS,
  NG_ROW_ONSETBARBS,
  NG_ROW_ENDTURN,
  NG_ROW_TIMEOUT,
  NG_ROW_ENDSPACESHIP,
  NG_ROW_STARTCITY,         /* Phase 1 */
  NG_ROW_STARTUNITS,
  NG_ROW_COUNT
};

/* For enum-style rows, a fixed list of valid values. The strings must
   match the server's support-name exactly (see server/settings.c's
   NAME_CASE entries): case-sensitive match is what `/set` does. */
static const char *const MAPSIZE_VALUES[] = {
  /* server mapsize_name at settings.c:239 */
  "FULLSIZE", "PLAYER", "XYSIZE"
};
static const char *const GENERATOR_VALUES[] = {
  /* server generator_name at settings.c:302 */
  "SCENARIO", "RANDOM", "FRACTAL", "ISLAND", "FAIR", "FRACTURE"
};
static const char *const STARTPOS_VALUES[] = {
  /* server startpos_name at settings.c:318 */
  "DEFAULT", "SINGLE", "2or3", "ALL", "VARIABLE"
};
static const char *const DIFFICULTY_VALUES[] = {
  /* Chat-command names, not /set values. */
  "novice", "easy", "normal", "hard", "cheating", "experimental"
};
static const char *const BARBARIANS_VALUES[] = {
  /* server barbarians_name at settings.c:491 */
  "DISABLED", "HUTS_ONLY", "NORMAL", "FREQUENT", "HORDES"
};
/* Server accepts numeric index 0..3 for topology/wrap. */
static const char *const TOPOLOGY_VALUES[] = {
  "square", "hex", "iso", "iso-hex"
};
static const char *const WRAP_VALUES[] = {
  "none", "east-west", "north-south", "both"
};
static const char *const ENDSPACESHIP_VALUES[] = {
  "disabled", "enabled"
};

/* Bootstrap ruleset list used before PACKET_RULESET_CHOICES arrives. */
static const char *const BOOTSTRAP_RULESETS[] = {
  "classic", "default", "civ1", "civ2", "civ2civ3",
  "alien", "sandbox", "multiplayer", "granularity"
};

/* ---- presets ----------------------------------------------------------
   Each preset atomically writes a set of ng_state fields so players don't
   have to build a conflicting combo row-by-row. Negative sentinels mean
   "leave this field alone" — use them when a preset should tweak only
   part of the state. */

#define PRESET_KEEP (-999999)

struct ng_preset {
  const char *label;        /* translatable */
  int mapsize_idx;          /* FULLSIZE/PLAYER/XYSIZE index or PRESET_KEEP */
  int generator_idx;
  int startpos_idx;
  int aifill;
  int size_val;
  int tilesperplayer_val;
  int landmass;
  int barbarians_idx;
  int endturn;
  const char *start_units;  /* NULL = keep */
  int startcity;            /* -1 = keep, 0/1 = set */
};

/* Helper macros to keep the table readable. */
#define _K PRESET_KEEP
static const struct ng_preset NG_PRESETS[] = {
  /* label,                         ms,gn,sp,ai,sz,tpp,lm, bar, et,    units,  sc */
  { N_("Classic continent"),         0, 2, 0, _K, 4,   _K, 30,  _K,  _K, NULL,  -1 },
  { N_("Small islands (one per player)"),
                                     1, 3, 1, _K,_K,  150, 30,  _K,  _K, NULL,  -1 },
  { N_("Archipelago (2-3 per island)"),
                                     1, 3, 2, _K,_K,  200, 25,  _K,  _K, NULL,  -1 },
  { N_("Fair duel"),                 1, 4, 1,  1,_K,  400, 40,  _K,  _K, NULL,  -1 },
  { N_("Pangaea (everyone together)"),
                                     0, 2, 3, _K, 4,   _K, 60,  _K,  _K, NULL,  -1 },
  { N_("Huge long war"),             0, 2, 0, 10,20,   _K, 30,   2, 2000, NULL, -1 },
  { N_("Peaceful expansion"),        0, 2, 0,  3, 4,   _K, 50,   0, 5000, NULL, -1 },
  { N_("Fracture crags"),            0, 5, 0, _K, 6,   _K, 35,  _K,  _K, NULL,  -1 },
};
#define N_NG_PRESETS ((int)(sizeof(NG_PRESETS) / sizeof(NG_PRESETS[0])))
#undef _K

/* Persistent label storage for the preset sr_pick_enum call. */
static char g_preset_lines[N_NG_PRESETS][96];
static const char *g_preset_ptrs[N_NG_PRESETS];

/* ---- start-units spec (per server/settings.c:2003 GEN_STRING startunits) -
   Valid characters are the letters of "cwxksfdDaA"; each letter seeds one
   starting unit of the matching role. Case is significant (d != D, a != A).
   The FIRST character's unit must be native to a "Starter" terrain or the
   server rejects the whole string. Default on the server side is "ccwwx".
-------------------------------------------------------------------------- */

struct start_role {
  char code;
  const char *short_name;      /* One-token name used in breakdown */
  const char *example;         /* Concrete example unit in classic */
};

static const struct start_role START_ROLES[] = {
  { 'c', N_("Settler"),         N_("Settlers") },
  { 'w', N_("Worker"),          N_("Engineers") },
  { 'x', N_("Explorer"),        N_("Explorer") },
  { 'k', N_("Gameloss unit"),   N_("King") },
  { 's', N_("Diplomat"),        N_("Diplomat") },
  { 'f', N_("Ferryboat"),       N_("Trireme") },
  { 'd', N_("Basic defender"),  N_("Warriors") },
  { 'D', N_("Strong defender"), N_("Phalanx") },
  { 'a', N_("Fast attacker"),   N_("Horsemen") },
  { 'A', N_("Strong attacker"), N_("Catapult") },
};
#define N_START_ROLES ((int)(sizeof(START_ROLES) / sizeof(START_ROLES[0])))

#define SR_START_VALID_CHARS "cwxksfdDaA"

/* Preset codes — every one must use only SR_START_VALID_CHARS or the
   server's startunits_callback() will reject it and silently fall back
   to GAME_DEFAULT_START_UNITS ("ccwwx"). */
struct start_preset {
  const char *code;
  const char *label;           /* translatable */
};

static const struct start_preset START_PRESETS[] = {
  { "ccwwx",    N_("Classic (2 Settlers, 2 Workers, 1 Explorer)") },
  { "ccwx",     N_("Light (2 Settlers, 1 Worker, 1 Explorer)") },
  { "cx",       N_("Minimal (1 Settler, 1 Explorer)") },
  { "ccwwxd",   N_("Defended (Classic + Warriors)") },
  { "ccwwxD",   N_("Strong defence (Classic + Phalanx)") },
  { "ccwwxs",   N_("Diplomatic (Classic + Diplomat)") },
  { "ccwwxf",   N_("Naval (Classic + Trireme)") },
  { "ccwwxa",   N_("Aggressive (Classic + Horsemen)") },
  { "cccwwxx",  N_("Expansion (3 Settlers, 2 Workers, 2 Explorers)") },
  { "cwxkd",    N_("With king (Settler, Worker, Explorer, King, Warriors)") },
};
#define N_START_PRESETS ((int)(sizeof(START_PRESETS) / sizeof(START_PRESETS[0])))

/* Extra row that opens a free-text editor for advanced users. */
#define START_PRESET_CUSTOM_IDX N_START_PRESETS

/* Persistent buffers for labels handed to sr_pick_enum. One extra slot
   for the "Custom..." entry. */
static char g_start_preset_lines[N_START_PRESETS + 1][128];
static const char *g_start_preset_ptrs[N_START_PRESETS + 1];

/* Look up a role by code letter. Returns NULL for unknown characters. */
static const struct start_role *start_role_by_code(char c)
{
  int i;

  for (i = 0; i < N_START_ROLES; i++) {
    if (START_ROLES[i].code == c) {
      return &START_ROLES[i];
    }
  }
  return NULL;
}

/* Format a human-readable breakdown of `code` into `out`. E.g. "ccwwx"
   -> "2 Settlers + 2 Workers + 1 Explorer". Empty code -> "(none)". */
static void describe_start_units(const char *code, char *out, size_t out_sz)
{
  int counts[N_START_ROLES];
  int i;
  int first = 1;
  size_t k;

  if (code == NULL || *code == '\0') {
    fc_strlcpy(out, _("(none)"), out_sz);
    return;
  }
  for (i = 0; i < N_START_ROLES; i++) { counts[i] = 0; }
  for (k = 0; code[k] != '\0'; k++) {
    const struct start_role *r = start_role_by_code(code[k]);

    if (r != NULL) {
      counts[(int)(r - START_ROLES)]++;
    }
  }
  out[0] = '\0';
  for (i = 0; i < N_START_ROLES; i++) {
    char piece[64];

    if (counts[i] <= 0) {
      continue;
    }
    fc_snprintf(piece, sizeof(piece), "%s%d %s%s",
                first ? "" : ", ",
                counts[i],
                _(START_ROLES[i].short_name),
                counts[i] == 1 ? "" : "s");
    fc_strlcat(out, piece, out_sz);
    first = 0;
  }
  if (first) {
    /* Code held only unknown characters. */
    fc_strlcpy(out, _("(invalid — will be ignored by server)"), out_sz);
  }
}


/* ---- state ------------------------------------------------------------- */

struct ng_state {
  HWND dlg;
  HWND list;
  HWND btn_start;
  HWND btn_cancel;
  HWND prev_focus;

  /* Current row values (indices into the enum lists, or raw int/string). */
  char ruleset[64];
  int aifill;
  int mapsize_idx;
  int generator_idx;
  int startpos_idx;
  int topology_idx;
  int wrap_idx;
  int difficulty_idx;
  int barbarians_idx;
  int onset_barbs;
  int huts;
  int landmass;
  int temperature;
  int endturn;
  int timeout;
  int endspaceship_idx;
  int size_val;           /* companion for mapsize=FULLSIZE */
  int tilesperplayer_val; /* companion for mapsize=PLAYER */
  int xsize_val;          /* companion for mapsize=XYSIZE */
  int ysize_val;          /* companion for mapsize=XYSIZE */
  int separatepoles;      /* bool (0/1) */
  int startcity;          /* bool (0/1) */
  char start_units[16];

  /* Server-supplied ruleset list, if any. */
  char **server_rulesets;
  int n_server_rulesets;
};

static struct ng_state *g_ng = NULL;

/* Settings captured from the dialog, flushed after PAGE_START. */
struct ng_pending {
  int active;
  char ruleset[64];
  int aifill;
  char mapsize[32];
  char generator[32];
  char startpos[32];
  int topology_idx;
  int wrap_idx;
  char difficulty[32];
  char barbarians[32];
  int onset_barbs;
  int huts;
  int landmass;
  int temperature;
  int endturn;
  int timeout;
  int endspaceship_idx;
  int size_val;
  int tilesperplayer_val;
  int xsize_val;
  int ysize_val;
  int separatepoles;
  int startcity;
  char start_units[16];
};
static struct ng_pending pending;

/* ---- helpers ----------------------------------------------------------- */

static void free_server_rulesets(struct ng_state *s)
{
  int i;

  if (s->server_rulesets == NULL) {
    return;
  }
  for (i = 0; i < s->n_server_rulesets; i++) {
    free(s->server_rulesets[i]);
  }
  free(s->server_rulesets);
  s->server_rulesets = NULL;
  s->n_server_rulesets = 0;
}

static wchar_t *utf8_to_wide(const char *s)
{
  int wlen;
  wchar_t *w;

  if (s == NULL) { return NULL; }
  wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if (wlen <= 0) { return NULL; }
  w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
  if (w == NULL) { return NULL; }
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wlen);
  return w;
}

/* ---- Phase 4: dependency-aware suffixes ---------------------------
   Companion rows show "(active)" or "(inactive — how to activate)" so
   the user always knows which field the server is actually consulting.
   Every row stays visible and editable; the suffix just tells the
   truth about what's live. */

/* mapsize method indices — keep in sync with MAPSIZE_VALUES[]. */
#define PH4_MS_FULLSIZE 0
#define PH4_MS_PLAYER   1
#define PH4_MS_XYSIZE   2

static const char *mapsize_companion_suffix(const struct ng_state *s,
                                            int for_method)
{
  if (s->mapsize_idx == for_method) {
    return "  (active)";
  }
  switch (for_method) {
    case PH4_MS_FULLSIZE: return "  (inactive - needs mapsize FULLSIZE)";
    case PH4_MS_PLAYER:   return "  (inactive - needs mapsize PLAYER)";
    case PH4_MS_XYSIZE:   return "  (inactive - needs mapsize XYSIZE)";
  }
  return "";
}

static void row_label(int row, char *out, size_t outlen)
{
  struct ng_state *s = g_ng;

  switch (row) {
    case NG_ROW_PRESET:
      snprintf(out, outlen,
               "Preset...  (macro: applies several rows at once)");
      break;
    case NG_ROW_RULESET:
      snprintf(out, outlen, "Ruleset:  %s", s->ruleset);
      break;
    case NG_ROW_AIFILL:
      snprintf(out, outlen, "AI opponents:  %d", s->aifill);
      break;
    case NG_ROW_MAPSIZE:
      snprintf(out, outlen, "Map size method:  %s",
               MAPSIZE_VALUES[s->mapsize_idx]);
      break;
    case NG_ROW_SIZE:
      snprintf(out, outlen,
               "  size (thousands of tiles):  %d%s",
               s->size_val,
               mapsize_companion_suffix(s, PH4_MS_FULLSIZE));
      break;
    case NG_ROW_TILESPERPLAYER:
      snprintf(out, outlen,
               "  tilesperplayer:  %d%s",
               s->tilesperplayer_val,
               mapsize_companion_suffix(s, PH4_MS_PLAYER));
      break;
    case NG_ROW_XSIZE:
      snprintf(out, outlen, "  xsize (width):  %d%s",
               s->xsize_val,
               mapsize_companion_suffix(s, PH4_MS_XYSIZE));
      break;
    case NG_ROW_YSIZE:
      snprintf(out, outlen, "  ysize (height):  %d%s",
               s->ysize_val,
               mapsize_companion_suffix(s, PH4_MS_XYSIZE));
      break;
    case NG_ROW_GENERATOR:
      snprintf(out, outlen, "Map generator:  %s",
               GENERATOR_VALUES[s->generator_idx]);
      break;
    case NG_ROW_STARTPOS:
      snprintf(out, outlen, "Start positions:  %s",
               STARTPOS_VALUES[s->startpos_idx]);
      break;
    case NG_ROW_TOPOLOGY:
      snprintf(out, outlen, "Topology:  %s",
               TOPOLOGY_VALUES[s->topology_idx]);
      break;
    case NG_ROW_WRAP:
      snprintf(out, outlen, "Wrap:  %s",
               WRAP_VALUES[s->wrap_idx]);
      break;
    case NG_ROW_DIFFICULTY:
      snprintf(out, outlen, "Difficulty:  %s",
               DIFFICULTY_VALUES[s->difficulty_idx]);
      break;
    case NG_ROW_BARBARIANS:
      snprintf(out, outlen, "Barbarians:  %s",
               BARBARIANS_VALUES[s->barbarians_idx]);
      break;
    case NG_ROW_ONSETBARBS:
      /* onsetbarbs is ignored by the server when barbarians=DISABLED;
         barbarians_idx 0 is DISABLED in BARBARIANS_VALUES. */
      snprintf(out, outlen, "Onset barbarians (turn):  %d%s",
               s->onset_barbs,
               s->barbarians_idx == 0
                   ? "  (inactive - barbarians disabled)" : "");
      break;
    case NG_ROW_HUTS:
      snprintf(out, outlen, "Goody huts (per 1000 tiles):  %d", s->huts);
      break;
    case NG_ROW_LANDMASS:
      snprintf(out, outlen, "Landmass (percent):  %d", s->landmass);
      break;
    case NG_ROW_SEPARATEPOLES:
      snprintf(out, outlen, "Separate poles:  %s",
               s->separatepoles ? "on" : "off");
      break;
    case NG_ROW_TEMPERATURE:
      /* SCENARIO generator uses the scenario's own climate. */
      snprintf(out, outlen, "Temperature:  %d%s",
               s->temperature,
               s->generator_idx == 0
                   ? "  (inactive - SCENARIO uses the map's own climate)"
                   : "");
      break;
    case NG_ROW_ENDTURN:
      snprintf(out, outlen, "End turn:  %d", s->endturn);
      break;
    case NG_ROW_TIMEOUT:
      snprintf(out, outlen,
               "Turn timeout (seconds, 0 = none):  %d", s->timeout);
      break;
    case NG_ROW_ENDSPACESHIP:
      snprintf(out, outlen, "End on spaceship:  %s",
               ENDSPACESHIP_VALUES[s->endspaceship_idx]);
      break;
    case NG_ROW_STARTCITY:
      snprintf(out, outlen,
               "Start with a founded city:  %s",
               s->startcity ? "yes" : "no");
      break;
    case NG_ROW_STARTUNITS: {
      char breakdown[160];

      describe_start_units(s->start_units, breakdown, sizeof(breakdown));
      /* With startcity=on, server still spawns the listed units but
         they appear on/around the pre-founded city rather than being
         the reason your start position exists. Subtle, worth flagging. */
      snprintf(out, outlen, "Start units:  %s  (%s)%s",
               s->start_units[0] != '\0' ? s->start_units : "-",
               breakdown,
               s->startcity ? "  (spawns at the founded city)" : "");
      break;
    }
    default:
      snprintf(out, outlen, "?");
      break;
  }
}

static void populate_list(void)
{
  struct ng_state *s = g_ng;
  int sel;
  int i;

  if (s == NULL || s->list == NULL) { return; }
  sel = (int)SendMessageW(s->list, LB_GETCURSEL, 0, 0);
  SendMessageW(s->list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(s->list, LB_RESETCONTENT, 0, 0);
  for (i = 0; i < NG_ROW_COUNT; i++) {
    char buf[160];
    wchar_t *w;

    row_label(i, buf, sizeof(buf));
    w = utf8_to_wide(buf);
    if (w != NULL) {
      SendMessageW(s->list, LB_ADDSTRING, 0, (LPARAM)w);
      free(w);
    }
  }
  if (sel < 0 || sel >= NG_ROW_COUNT) { sel = 0; }
  SendMessageW(s->list, LB_SETCURSEL, (WPARAM)sel, 0);
  SendMessageW(s->list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(s->list, NULL, TRUE);
}

/* ---- sub-picker callbacks --------------------------------------------- */

static void on_pick_int_aifill(int value, void *ud)
{
  (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->aifill = value;
  populate_list();
}

static void on_pick_int_onset(int value, void *ud)
{
  (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->onset_barbs = value;
  populate_list();
}

static void on_pick_enum_mapsize(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->mapsize_idx = idx;
  populate_list();
}

static void on_pick_enum_generator(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->generator_idx = idx;
  populate_list();
}

static void on_pick_enum_difficulty(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->difficulty_idx = idx;
  populate_list();
}

static void on_pick_enum_barbarians(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->barbarians_idx = idx;
  populate_list();
}

static void on_pick_enum_topology(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->topology_idx = idx;
  populate_list();
}

static void on_pick_enum_wrap(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->wrap_idx = idx;
  populate_list();
}

static void on_pick_enum_endspaceship(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->endspaceship_idx = idx;
  populate_list();
}

static void on_pick_enum_startpos(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->startpos_idx = idx;
  populate_list();
}

static void on_pick_int_size(int value, void *ud)
{
  (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->size_val = value;
  populate_list();
}

static void on_pick_int_tpp(int value, void *ud)
{
  (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->tilesperplayer_val = value;
  populate_list();
}

static void on_pick_int_xsize(int value, void *ud)
{
  (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->xsize_val = value;
  populate_list();
}

static void on_pick_int_ysize(int value, void *ud)
{
  (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->ysize_val = value;
  populate_list();
}

static void on_pick_enum_separatepoles(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->separatepoles = idx ? 1 : 0;
  populate_list();
}

static void on_pick_enum_startcity(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->startcity = idx ? 1 : 0;
  populate_list();
}

/* Apply a preset's non-KEEP fields to the current ng_state. */
static void apply_preset(const struct ng_preset *p)
{
  struct ng_state *s = g_ng;

  if (s == NULL || p == NULL) { return; }
  if (p->mapsize_idx       != PRESET_KEEP) { s->mapsize_idx       = p->mapsize_idx; }
  if (p->generator_idx     != PRESET_KEEP) { s->generator_idx     = p->generator_idx; }
  if (p->startpos_idx      != PRESET_KEEP) { s->startpos_idx      = p->startpos_idx; }
  if (p->aifill            != PRESET_KEEP) { s->aifill            = p->aifill; }
  if (p->size_val          != PRESET_KEEP) { s->size_val          = p->size_val; }
  if (p->tilesperplayer_val != PRESET_KEEP) { s->tilesperplayer_val = p->tilesperplayer_val; }
  if (p->landmass          != PRESET_KEEP) { s->landmass          = p->landmass; }
  if (p->barbarians_idx    != PRESET_KEEP) { s->barbarians_idx    = p->barbarians_idx; }
  if (p->endturn           != PRESET_KEEP) { s->endturn           = p->endturn; }
  if (p->start_units       != NULL) {
    fc_strlcpy(s->start_units, p->start_units, sizeof(s->start_units));
  }
  if (p->startcity >= 0) { s->startcity = p->startcity; }
}

static void on_pick_enum_preset(int idx, const char *v, void *ud)
{
  char msg[160];

  (void)v; (void)ud;
  if (g_ng == NULL || idx < 0 || idx >= N_NG_PRESETS) { return; }
  apply_preset(&NG_PRESETS[idx]);
  fc_snprintf(msg, sizeof(msg), _("Preset applied: %s."),
              _(NG_PRESETS[idx].label));
  sr_say(msg, 1);
  sr_log_append(msg);
  populate_list();
}

static void open_picker_preset(void)
{
  int i;

  for (i = 0; i < N_NG_PRESETS; i++) {
    fc_strlcpy(g_preset_lines[i], _(NG_PRESETS[i].label),
               sizeof(g_preset_lines[i]));
    g_preset_ptrs[i] = g_preset_lines[i];
  }
  sr_pick_enum(_("Game preset"), g_preset_ptrs, N_NG_PRESETS, 0,
               on_pick_enum_preset, NULL);
}

static void on_pick_int_huts(int value, void *ud)
{
  (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->huts = value;
  populate_list();
}

static void on_pick_int_landmass(int value, void *ud)
{
  (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->landmass = value;
  populate_list();
}

static void on_pick_int_temperature(int value, void *ud)
{
  (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->temperature = value;
  populate_list();
}

static void on_pick_int_endturn(int value, void *ud)
{
  (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->endturn = value;
  populate_list();
}

static void on_pick_int_timeout(int value, void *ud)
{
  (void)ud;
  if (g_ng == NULL) { return; }
  g_ng->timeout = value;
  populate_list();
}

static void on_pick_enum_ruleset(int idx, const char *v, void *ud)
{
  (void)idx; (void)ud;
  if (g_ng == NULL || v == NULL) { return; }
  strncpy(g_ng->ruleset, v, sizeof(g_ng->ruleset) - 1);
  g_ng->ruleset[sizeof(g_ng->ruleset) - 1] = '\0';
  populate_list();
}

static int find_in_list(const char *const *list, int n, const char *v)
{
  int i;

  for (i = 0; i < n; i++) {
    if (strcmp(list[i], v) == 0) { return i; }
  }
  return -1;
}

static void open_picker_for_row(int row)
{
  struct ng_state *s = g_ng;

  if (s == NULL) { return; }
  switch (row) {
    case NG_ROW_RULESET:
      if (s->server_rulesets != NULL && s->n_server_rulesets > 0) {
        int cur = 0;
        int i;
        for (i = 0; i < s->n_server_rulesets; i++) {
          if (strcmp(s->server_rulesets[i], s->ruleset) == 0) {
            cur = i; break;
          }
        }
        sr_pick_enum("Ruleset",
                     (const char * const *)s->server_rulesets,
                     s->n_server_rulesets, cur,
                     on_pick_enum_ruleset, NULL);
      } else {
        int n = (int)(sizeof(BOOTSTRAP_RULESETS)
                      / sizeof(BOOTSTRAP_RULESETS[0]));
        int cur = find_in_list(BOOTSTRAP_RULESETS, n, s->ruleset);
        if (cur < 0) { cur = 0; }
        sr_pick_enum("Ruleset", BOOTSTRAP_RULESETS, n, cur,
                     on_pick_enum_ruleset, NULL);
      }
      break;
    case NG_ROW_AIFILL:
      sr_pick_int("AI opponents (0-30)", s->aifill, 0, 30,
                  on_pick_int_aifill, NULL);
      break;
    case NG_ROW_MAPSIZE:
      sr_pick_enum("Map size", MAPSIZE_VALUES,
                   (int)(sizeof(MAPSIZE_VALUES) / sizeof(MAPSIZE_VALUES[0])),
                   s->mapsize_idx, on_pick_enum_mapsize, NULL);
      break;
    case NG_ROW_GENERATOR:
      sr_pick_enum("Map generator", GENERATOR_VALUES,
                   (int)(sizeof(GENERATOR_VALUES)
                         / sizeof(GENERATOR_VALUES[0])),
                   s->generator_idx, on_pick_enum_generator, NULL);
      break;
    case NG_ROW_DIFFICULTY:
      sr_pick_enum("Difficulty", DIFFICULTY_VALUES,
                   (int)(sizeof(DIFFICULTY_VALUES)
                         / sizeof(DIFFICULTY_VALUES[0])),
                   s->difficulty_idx, on_pick_enum_difficulty, NULL);
      break;
    case NG_ROW_BARBARIANS:
      sr_pick_enum("Barbarians", BARBARIANS_VALUES,
                   (int)(sizeof(BARBARIANS_VALUES)
                         / sizeof(BARBARIANS_VALUES[0])),
                   s->barbarians_idx, on_pick_enum_barbarians, NULL);
      break;
    case NG_ROW_ONSETBARBS:
      sr_pick_int("Barbarian onset turn (0-1000)", s->onset_barbs, 0, 1000,
                  on_pick_int_onset, NULL);
      break;
    case NG_ROW_TOPOLOGY:
      sr_pick_enum("Topology", TOPOLOGY_VALUES,
                   (int)(sizeof(TOPOLOGY_VALUES)
                         / sizeof(TOPOLOGY_VALUES[0])),
                   s->topology_idx, on_pick_enum_topology, NULL);
      break;
    case NG_ROW_WRAP:
      sr_pick_enum("Wrap", WRAP_VALUES,
                   (int)(sizeof(WRAP_VALUES) / sizeof(WRAP_VALUES[0])),
                   s->wrap_idx, on_pick_enum_wrap, NULL);
      break;
    case NG_ROW_HUTS:
      sr_pick_int("Goody huts (0-500)", s->huts, 0, 500,
                  on_pick_int_huts, NULL);
      break;
    case NG_ROW_LANDMASS:
      sr_pick_int("Landmass percent (1-100)", s->landmass, 1, 100,
                  on_pick_int_landmass, NULL);
      break;
    case NG_ROW_TEMPERATURE:
      sr_pick_int("Temperature (0-100)", s->temperature, 0, 100,
                  on_pick_int_temperature, NULL);
      break;
    case NG_ROW_ENDTURN:
      sr_pick_int("End turn (1-32767)", s->endturn, 1, 32767,
                  on_pick_int_endturn, NULL);
      break;
    case NG_ROW_TIMEOUT:
      sr_pick_int("Turn timeout seconds (0-8640000; 0 = none)",
                  s->timeout, 0, 8640000,
                  on_pick_int_timeout, NULL);
      break;
    case NG_ROW_ENDSPACESHIP:
      sr_pick_enum("End on spaceship", ENDSPACESHIP_VALUES,
                   (int)(sizeof(ENDSPACESHIP_VALUES)
                         / sizeof(ENDSPACESHIP_VALUES[0])),
                   s->endspaceship_idx, on_pick_enum_endspaceship, NULL);
      break;
    case NG_ROW_STARTPOS:
      sr_pick_enum("Start positions", STARTPOS_VALUES,
                   (int)(sizeof(STARTPOS_VALUES)
                         / sizeof(STARTPOS_VALUES[0])),
                   s->startpos_idx, on_pick_enum_startpos, NULL);
      break;
    case NG_ROW_SIZE:
      sr_pick_int("Map size in thousands of tiles (1-2048)",
                  s->size_val, 1, 2048, on_pick_int_size, NULL);
      break;
    case NG_ROW_TILESPERPLAYER:
      sr_pick_int("Tiles per player (1-1000)",
                  s->tilesperplayer_val, 1, 1000, on_pick_int_tpp, NULL);
      break;
    case NG_ROW_XSIZE:
      sr_pick_int("Map width xsize (16-5000)",
                  s->xsize_val, 16, 5000, on_pick_int_xsize, NULL);
      break;
    case NG_ROW_YSIZE:
      sr_pick_int("Map height ysize (16-5000)",
                  s->ysize_val, 16, 5000, on_pick_int_ysize, NULL);
      break;
    case NG_ROW_SEPARATEPOLES: {
      /* Two-option picker for a boolean — clearer than an int toggle. */
      static const char *const BOOL_ONOFF[] = { "off", "on" };

      sr_pick_enum("Separate poles", BOOL_ONOFF, 2,
                   s->separatepoles ? 1 : 0,
                   on_pick_enum_separatepoles, NULL);
      break;
    }
    case NG_ROW_STARTCITY: {
      static const char *const BOOL_NOYES[] = { "no", "yes" };

      sr_pick_enum("Start with a founded city", BOOL_NOYES, 2,
                   s->startcity ? 1 : 0,
                   on_pick_enum_startcity, NULL);
      break;
    }
    case NG_ROW_PRESET:
      open_picker_preset();
      break;
    case NG_ROW_STARTUNITS:
      /* Handled by open_picker_startunits() — its callback writes to
         start_units rather than ruleset. */
      break;
    default:
      break;
  }
}

/* Store whatever the user typed, validated against the server spec.
   Invalid characters are stripped (with a spoken warning) rather than
   rejecting the whole line — the player's intent is usually clear and
   partial strings are a common mistake. Empty after stripping resets
   to the server default "ccwwx". */
static void on_pick_string_startunits(const char *v, void *ud)
{
  char clean[sizeof(g_ng->start_units)];
  size_t i, j = 0;
  int dropped = 0;

  (void)ud;
  if (g_ng == NULL) { return; }
  if (v == NULL) { v = ""; }
  for (i = 0; v[i] != '\0' && j + 1 < sizeof(clean); i++) {
    if (strchr(SR_START_VALID_CHARS, v[i]) != NULL) {
      clean[j++] = v[i];
    } else if (v[i] != ' ' && v[i] != '\t') {
      dropped++;
    }
  }
  clean[j] = '\0';
  if (clean[0] == '\0') {
    fc_strlcpy(g_ng->start_units, "ccwwx", sizeof(g_ng->start_units));
    sr_say(_("Empty or invalid — reset to default ccwwx."), 1);
  } else {
    fc_strlcpy(g_ng->start_units, clean, sizeof(g_ng->start_units));
    if (dropped > 0) {
      char msg[160];
      fc_snprintf(msg, sizeof(msg),
                  _("Dropped %d invalid characters. Valid letters: %s."),
                  dropped, SR_START_VALID_CHARS);
      sr_say(msg, 1);
    } else {
      char breakdown[160];
      char msg[256];

      describe_start_units(g_ng->start_units, breakdown, sizeof(breakdown));
      fc_snprintf(msg, sizeof(msg), _("Start units: %s (%s)."),
                  g_ng->start_units, breakdown);
      sr_say(msg, 1);
    }
  }
  populate_list();
}

static void on_pick_enum_startunits(int idx, const char *v, void *ud)
{
  char title[128];

  (void)v; (void)ud;
  if (g_ng == NULL) { return; }
  if (idx >= 0 && idx < N_START_PRESETS) {
    fc_strlcpy(g_ng->start_units, START_PRESETS[idx].code,
               sizeof(g_ng->start_units));
    populate_list();
    return;
  }
  if (idx == START_PRESET_CUSTOM_IDX) {
    fc_snprintf(title, sizeof(title),
                _("Start units (letters from %s)"),
                SR_START_VALID_CHARS);
    sr_pick_string(title, g_ng->start_units,
                   on_pick_string_startunits, NULL);
    return;
  }
}

static int find_start_preset_index(const char *code)
{
  int i;

  if (code == NULL) { return -1; }
  for (i = 0; i < N_START_PRESETS; i++) {
    if (strcmp(START_PRESETS[i].code, code) == 0) {
      return i;
    }
  }
  return -1;
}

/* Rebuild the label array every time the picker opens so labels track
   any future localisation / preset edits. Appends a "Custom..." row. */
static void build_start_preset_labels(void)
{
  int i;

  for (i = 0; i < N_START_PRESETS; i++) {
    fc_snprintf(g_start_preset_lines[i], sizeof(g_start_preset_lines[i]),
                "%s  [%s]", _(START_PRESETS[i].label),
                START_PRESETS[i].code);
    g_start_preset_ptrs[i] = g_start_preset_lines[i];
  }
  fc_strlcpy(g_start_preset_lines[N_START_PRESETS],
             _("Custom... (type your own letter code)"),
             sizeof(g_start_preset_lines[N_START_PRESETS]));
  g_start_preset_ptrs[N_START_PRESETS] =
      g_start_preset_lines[N_START_PRESETS];
}

static void open_picker_startunits(void)
{
  int cur = find_start_preset_index(g_ng->start_units);

  if (cur < 0) {
    /* Current code is a one-off — let Custom... be the default cursor
       so Enter goes straight to the free-text editor. */
    cur = START_PRESET_CUSTOM_IDX;
  }
  build_start_preset_labels();
  sr_pick_enum(_("Start units (preset or custom)"),
               g_start_preset_ptrs, N_START_PRESETS + 1, cur,
               on_pick_enum_startunits, NULL);
}

/* ---- Phase 5: focus-time spoken help -------------------------------
   One sentence per row, spoken interrupting on LBN_SELCHANGE so the
   player can arrow through the dialog and get a running explanation
   without reading anything. Help strings are pulled from the server's
   own N_() option docs (server/settings.c) trimmed to one sentence. */

static const char *const NG_ROW_HELP[NG_ROW_COUNT] = {
  [NG_ROW_PRESET]        = N_("Preset: apply several compatible settings"
                              " at once. Press Enter."),
  [NG_ROW_RULESET]       = N_("Ruleset defines units, buildings, and game"
                              " rules. Classic matches vanilla Freeciv."),
  [NG_ROW_AIFILL]        = N_("AI players are added until this count of"
                              " players is reached. 0 to 30."),
  [NG_ROW_GENERATOR]     = N_("Map-making algorithm. SCENARIO loads a map,"
                              " FRACTAL makes earthlike continents, ISLAND"
                              " puts one player per island."),
  [NG_ROW_STARTPOS]      = N_("How players spread across continents."
                              " SINGLE gives each player their own."),
  [NG_ROW_MAPSIZE]       = N_("Method for defining the map size: FULLSIZE"
                              " uses size, PLAYER uses tilesperplayer,"
                              " XYSIZE uses xsize and ysize."),
  [NG_ROW_SIZE]          = N_("Map area in thousands of tiles. Active only"
                              " when mapsize is FULLSIZE."),
  [NG_ROW_TILESPERPLAYER] = N_("Land tiles per player. Active only when"
                               " mapsize is PLAYER."),
  [NG_ROW_XSIZE]         = N_("Map width in tiles. Active only when mapsize"
                              " is XYSIZE."),
  [NG_ROW_YSIZE]         = N_("Map height in tiles. Active only when mapsize"
                              " is XYSIZE."),
  [NG_ROW_TOPOLOGY]      = N_("Map shape: square, hex, isometric, or"
                              " iso-hex."),
  [NG_ROW_WRAP]          = N_("Whether map edges wrap: none, east-west,"
                              " north-south, or both."),
  [NG_ROW_LANDMASS]      = N_("Percentage of map that is land, 15 to 85."
                              " ISLAND generator silently falls back above"
                              " 80."),
  [NG_ROW_SEPARATEPOLES] = N_("Keep polar regions as their own continents"
                              " rather than attached to land."),
  [NG_ROW_TEMPERATURE]   = N_("Planet temperature, 0 frozen to 100 hot,"
                              " default 50."),
  [NG_ROW_HUTS]          = N_("Goody huts per 1000 tiles. 0 disables"
                              " them."),
  [NG_ROW_DIFFICULTY]    = N_("AI skill level: novice, easy, normal, hard,"
                              " cheating, experimental."),
  [NG_ROW_BARBARIANS]    = N_("Barbarian activity: DISABLED, HUTS_ONLY,"
                              " NORMAL, FREQUENT, HORDES."),
  [NG_ROW_ONSETBARBS]    = N_("Turn number on which barbarians begin"
                              " appearing."),
  [NG_ROW_ENDTURN]       = N_("Game ends automatically after this many"
                              " turns."),
  [NG_ROW_TIMEOUT]       = N_("Wall-clock seconds per turn. 0 means no"
                              " turn timer."),
  [NG_ROW_ENDSPACESHIP]  = N_("If enabled, a spaceship reaching Alpha"
                              " Centauri ends the game."),
  [NG_ROW_STARTCITY]     = N_("If yes, each player begins with a city"
                              " already founded on the starting tile."),
  [NG_ROW_STARTUNITS]    = N_("Starting unit roles. Each letter seeds one"
                              " unit. Press Enter for presets or a free"
                              " text editor."),
};

static void say_row_help(int row)
{
  if (row < 0 || row >= NG_ROW_COUNT) { return; }
  if (NG_ROW_HELP[row] == NULL) { return; }
  sr_say(_(NG_ROW_HELP[row]), 1);
}

/* ---- preflight (Phase 2) ---------------------------------------------
   Before shipping settings to the server, walk a table of known
   conflicts. Two outcomes: auto-adjust (speak what changed and proceed)
   or hard-abort (speak why and leave the dialog open so the user can
   fix it). All thresholds come from server/settings.c and server/
   generator/mapgen.c so the rules track upstream without divergence. */

struct preflight {
  bool hard_abort;
  char log[1024];
};

static void preflight_append(struct preflight *pf, const char *msg)
{
  if (pf->log[0] != '\0') {
    fc_strlcat(pf->log, " ", sizeof(pf->log));
  }
  fc_strlcat(pf->log, msg, sizeof(pf->log));
}

/* Generator / startpos / mapsize / topology indices — keep in sync with
   the VALUES arrays at the top of this file. */
#define GEN_SCENARIO   0
#define GEN_RANDOM     1
#define GEN_FRACTAL    2
#define GEN_ISLAND     3
#define GEN_FAIR       4
#define GEN_FRACTURE   5

#define SPOS_DEFAULT   0
#define SPOS_SINGLE    1

#define MS_FULLSIZE    0
#define MS_PLAYER      1
#define MS_XYSIZE      2

#define TOPO_ISO       2
#define TOPO_ISO_HEX   3

static void preflight_check(struct ng_state *s, struct preflight *pf)
{
  char buf[160];

  memset(pf, 0, sizeof(*pf));

  /* 1) ISLAND / FAIR generators silently fall back to FRACTAL when
     landmass > 80 (server/generator/mapgen.c:2336). Cap here so the
     user actually gets what they asked for. */
  if ((s->generator_idx == GEN_ISLAND || s->generator_idx == GEN_FAIR)
      && s->landmass > 80) {
    fc_snprintf(buf, sizeof(buf),
                _("%s generator needs landmass 80 or less;"
                  " reducing from %d to 80."),
                GENERATOR_VALUES[s->generator_idx], s->landmass);
    preflight_append(pf, buf);
    s->landmass = 80;
  }

  /* 2) ISLAND / FAIR generators force startpos to SINGLE unless it is
     already DEFAULT. Without this, ALL / 2or3 / VARIABLE get silently
     downgraded to 2or3 (server/generator/mapgen.c:1421). */
  if ((s->generator_idx == GEN_ISLAND || s->generator_idx == GEN_FAIR)
      && s->startpos_idx != SPOS_DEFAULT
      && s->startpos_idx != SPOS_SINGLE) {
    fc_snprintf(buf, sizeof(buf),
                _("%s generator needs start positions SINGLE;"
                  " changing from %s."),
                GENERATOR_VALUES[s->generator_idx],
                STARTPOS_VALUES[s->startpos_idx]);
    preflight_append(pf, buf);
    s->startpos_idx = SPOS_SINGLE;
  }

  /* 3) ISLAND generator on a too-small XYSIZE map falls back to FRACTAL
     (mapgen.c:2344 approx). Bump to 40x40 minimum. */
  if (s->generator_idx == GEN_ISLAND && s->mapsize_idx == MS_XYSIZE) {
    if (s->xsize_val < 40) {
      fc_snprintf(buf, sizeof(buf),
                  _("Island generator needs xsize at least 40;"
                    " bumping from %d."),
                  s->xsize_val);
      preflight_append(pf, buf);
      s->xsize_val = 40;
    }
    if (s->ysize_val < 40) {
      fc_snprintf(buf, sizeof(buf),
                  _("Island generator needs ysize at least 40;"
                    " bumping from %d."),
                  s->ysize_val);
      preflight_append(pf, buf);
      s->ysize_val = 40;
    }
  }

  /* 4) Isometric topologies require even dimensions on XYSIZE maps.
     Server rejects odd values outright (settings.c:1246). */
  if (s->mapsize_idx == MS_XYSIZE
      && (s->topology_idx == TOPO_ISO
          || s->topology_idx == TOPO_ISO_HEX)) {
    if (s->xsize_val % 2 != 0) {
      fc_snprintf(buf, sizeof(buf),
                  _("Isometric topology needs even xsize;"
                    " bumping %d to %d."),
                  s->xsize_val, s->xsize_val + 1);
      preflight_append(pf, buf);
      s->xsize_val++;
    }
    if (s->ysize_val % 2 != 0) {
      fc_snprintf(buf, sizeof(buf),
                  _("Isometric topology needs even ysize;"
                    " bumping %d to %d."),
                  s->ysize_val, s->ysize_val + 1);
      preflight_append(pf, buf);
      s->ysize_val++;
    }
  }

  /* 5) aifill clamp. sr_pick_int already bounds it, but presets write
     the field directly — defence in depth. */
  if (s->aifill > 30) {
    fc_snprintf(buf, sizeof(buf),
                _("AI opponents capped to 30 (was %d)."), s->aifill);
    preflight_append(pf, buf);
    s->aifill = 30;
  }
  if (s->aifill < 0) { s->aifill = 0; }

  /* 6) landmass bounds from common/map.h (15..85). */
  if (s->landmass < 15) { s->landmass = 15; }
  if (s->landmass > 85) { s->landmass = 85; }

  /* 7) SCENARIO generator without a loaded scenario map — the server
     will hang or error. Warn loudly but don't block; some rulesets ship
     with their own scenario that we can't detect from the client. */
  if (s->generator_idx == GEN_SCENARIO) {
    preflight_append(pf,
        _("Warning: SCENARIO generator needs a scenario map loaded"
          " separately."));
  }

  /* 8) No units and no starting city is unwinnable. Hard abort. */
  if (!s->startcity && s->start_units[0] == '\0') {
    preflight_append(pf,
        _("Cannot start: empty startunits with no starting city leaves"
          " nothing to play."));
    pf->hard_abort = TRUE;
  }
}

/* ---- commit / cancel --------------------------------------------------- */

static void do_start(void)
{
  struct ng_state *s = g_ng;
  struct preflight pf;

  if (s == NULL) { return; }

  /* Phase 2: resolve conflicts first, possibly mutating s in place. */
  preflight_check(s, &pf);
  if (pf.log[0] != '\0') {
    sr_say(pf.log, 1);
    sr_log_append(pf.log);
    populate_list();  /* show adjusted values before (or instead of) start */
  }
  if (pf.hard_abort) {
    sr_say(_("Cannot start the game. Adjust and press Start again."), 1);
    return;
  }

  /* Make sure no leftover --file from a previous load attempt taints
     this fresh new-game spawn. */
  if (savefile != NULL) {
    free(savefile);
    savefile = NULL;
  }

  memset(&pending, 0, sizeof(pending));
  pending.active = 1;
  strncpy(pending.ruleset, s->ruleset, sizeof(pending.ruleset) - 1);
  pending.aifill = s->aifill;
  strncpy(pending.mapsize, MAPSIZE_VALUES[s->mapsize_idx],
          sizeof(pending.mapsize) - 1);
  strncpy(pending.generator, GENERATOR_VALUES[s->generator_idx],
          sizeof(pending.generator) - 1);
  strncpy(pending.startpos, STARTPOS_VALUES[s->startpos_idx],
          sizeof(pending.startpos) - 1);
  strncpy(pending.difficulty, DIFFICULTY_VALUES[s->difficulty_idx],
          sizeof(pending.difficulty) - 1);
  strncpy(pending.barbarians, BARBARIANS_VALUES[s->barbarians_idx],
          sizeof(pending.barbarians) - 1);
  pending.onset_barbs = s->onset_barbs;
  pending.topology_idx = s->topology_idx;
  pending.wrap_idx = s->wrap_idx;
  pending.huts = s->huts;
  pending.landmass = s->landmass;
  pending.temperature = s->temperature;
  pending.endturn = s->endturn;
  pending.timeout = s->timeout;
  pending.endspaceship_idx = s->endspaceship_idx;
  pending.size_val = s->size_val;
  pending.tilesperplayer_val = s->tilesperplayer_val;
  pending.xsize_val = s->xsize_val;
  pending.ysize_val = s->ysize_val;
  pending.separatepoles = s->separatepoles;
  pending.startcity = s->startcity;
  strncpy(pending.start_units, s->start_units,
          sizeof(pending.start_units) - 1);

  sr_say("Starting local server.", 1);
  sr_log_append("New Game: starting local server, will apply settings.");
  sr_newgame_close();
  client_start_server_and_set_page(PAGE_START);
}

/* ---- window proc ------------------------------------------------------- */

static LRESULT CALLBACK ng_wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      if (g_ng == NULL) { return -1; }
      g_ng->list = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_NG_LIST, hi, NULL);
      g_ng->btn_start = CreateWindowExW(
          0, L"BUTTON", L"Start game",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_NG_START, hi, NULL);
      g_ng->btn_cancel = CreateWindowExW(
          0, L"BUTTON", L"Cancel",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_NG_CANCEL, hi, NULL);
      populate_list();
      return 0;
    }
    case WM_SIZE: {
      RECT rc;
      int pad = 8;
      int btn_h = 28;

      GetClientRect(h, &rc);
      if (g_ng == NULL) { return 0; }
      if (g_ng->list != NULL) {
        MoveWindow(g_ng->list, pad, pad,
                   rc.right - 2 * pad,
                   rc.bottom - 3 * pad - btn_h, TRUE);
      }
      if (g_ng->btn_start != NULL) {
        MoveWindow(g_ng->btn_start, pad, rc.bottom - pad - btn_h,
                   140, btn_h, TRUE);
      }
      if (g_ng->btn_cancel != NULL) {
        MoveWindow(g_ng->btn_cancel, pad + 140 + pad,
                   rc.bottom - pad - btn_h, 100, btn_h, TRUE);
      }
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(w);

      if (g_ng == NULL) { break; }
      if (id == IDC_NG_LIST && HIWORD(w) == LBN_DBLCLK) {
        int sel = (int)SendMessageW(g_ng->list, LB_GETCURSEL, 0, 0);
        if (sel == NG_ROW_STARTUNITS) {
          open_picker_startunits();
        } else {
          open_picker_for_row(sel);
        }
        return 0;
      }
      if (id == IDC_NG_LIST && HIWORD(w) == LBN_SELCHANGE) {
        /* Phase 5: speak a one-sentence explanation as the user arrows
           between rows. Interrupting so each row cancels the previous
           help line rather than queueing a tower of text. */
        int sel = (int)SendMessageW(g_ng->list, LB_GETCURSEL, 0, 0);
        say_row_help(sel);
        return 0;
      }
      if (id == IDC_NG_START && HIWORD(w) == BN_CLICKED) {
        do_start();
        return 0;
      }
      if (id == IDC_NG_CANCEL && HIWORD(w) == BN_CLICKED) {
        pending.active = 0;
        sr_newgame_close();
        return 0;
      }
      if (id == IDCANCEL) {  /* Escape via IsDialogMessage. */
        pending.active = 0;
        sr_newgame_close();
        return 0;
      }
      break;
    }
    case WM_KEYDOWN:
      if (w == VK_ESCAPE) {
        pending.active = 0;
        sr_newgame_close();
        return 0;
      }
      break;
    case WM_CLOSE:
      pending.active = 0;
      sr_newgame_close();
      return 0;
    case WM_DESTROY:
      if (g_ng != NULL) {
        g_ng->dlg = NULL;
        g_ng->list = NULL;
        g_ng->btn_start = NULL;
        g_ng->btn_cancel = NULL;
      }
      return 0;
    default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

/* Intercept Enter on the listbox to open the picker. Also VK_ESCAPE to
   close the dialog. */
static WNDPROC g_list_default = NULL;

static LRESULT CALLBACK ng_list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  /* Tell IsDialogMessage that we want Enter delivered to us (otherwise
     it would fire the default Start button when focus is on the list).
     Only claim DLGC_WANTMESSAGE for Enter/Escape so Tab still moves
     focus between controls normally. */
  if (m == WM_GETDLGCODE) {
    LRESULT base = g_list_default != NULL
                   ? CallWindowProcW(g_list_default, h, m, w, l)
                   : DefWindowProcW(h, m, w, l);
    MSG *pmsg = (MSG *)l;

    if (pmsg != NULL
        && (pmsg->wParam == VK_RETURN || pmsg->wParam == VK_ESCAPE)) {
      return base | DLGC_WANTMESSAGE;
    }
    return base;
  }
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) {
      int sel = (int)SendMessageW(h, LB_GETCURSEL, 0, 0);
      if (sel == NG_ROW_STARTUNITS) {
        open_picker_startunits();
      } else {
        open_picker_for_row(sel);
      }
      return 0;
    }
    if (w == VK_ESCAPE) {
      sr_newgame_close();
      return 0;
    }
  }
  if (g_list_default != NULL) {
    return CallWindowProcW(g_list_default, h, m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}

static int g_class_registered = 0;
static int ensure_class(HINSTANCE hi)
{
  WNDCLASSEXW wc;

  if (g_class_registered) { return 1; }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = ng_wnd_proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_NEWGAME_CLASS;
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    g_class_registered = 1;
    return 1;
  }
  return 0;
}

/* ---- public API -------------------------------------------------------- */

void sr_newgame_open(void)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  struct ng_state *s;

  if (!ensure_class(hi)) { return; }
  if (g_ng != NULL && g_ng->dlg != NULL) {
    SetForegroundWindow(g_ng->dlg);
    if (g_ng->list != NULL) { SetFocus(g_ng->list); }
    return;
  }

  /* Clear any stale pending flush from a previous session. */
  memset(&pending, 0, sizeof(pending));

  s = (struct ng_state *)calloc(1, sizeof(*s));
  if (s == NULL) { return; }
  s->prev_focus = GetFocus();
  strncpy(s->ruleset, "classic", sizeof(s->ruleset) - 1);
  s->aifill = 6;
  /* New mapsize semantics: 0=FULLSIZE, 1=PLAYER, 2=XYSIZE. FULLSIZE is
     the server default (common/map.h:MAP_DEFAULT_MAPSIZE). */
  s->mapsize_idx = 0;    /* FULLSIZE */
  s->generator_idx = 2;  /* FRACTAL (server default) */
  s->startpos_idx = 0;   /* DEFAULT — generator chooses */
  s->topology_idx = 0;   /* square */
  s->wrap_idx = 1;       /* east-west (Freeciv default) */
  s->difficulty_idx = 2; /* normal */
  s->barbarians_idx = 2; /* NORMAL */
  s->onset_barbs = 60;
  s->huts = 15;
  s->landmass = 30;
  s->temperature = 50;
  s->endturn = 5000;
  s->timeout = 0;        /* no turn timer — SR-friendly default */
  s->endspaceship_idx = 1; /* enabled */
  s->size_val = 4;                /* 4000 tiles, server default */
  s->tilesperplayer_val = 100;    /* server default */
  s->xsize_val = 64;              /* server default */
  s->ysize_val = 64;              /* server default */
  s->separatepoles = 1;           /* server default TRUE */
  s->startcity = 0;               /* server default FALSE */
  /* Server default is "ccwwx" (common/game.h:GAME_DEFAULT_START_UNITS).
     Note: only the characters in "cwxksfdDaA" are accepted — our old
     preset "ccwmx" had an invalid 'm' and was silently rejected. */
  fc_strlcpy(s->start_units, "ccwwx", sizeof(s->start_units));

  g_ng = s;

  s->dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME,
      SR_NEWGAME_CLASS, L"New Game",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
      CW_USEDEFAULT, CW_USEDEFAULT, 480, 380,
      NULL, NULL, hi, NULL);
  if (s->dlg == NULL) {
    free(s); g_ng = NULL; return;
  }

  /* Subclass the listbox for Enter / Escape handling. */
  if (s->list != NULL) {
    g_list_default = (WNDPROC)SetWindowLongPtrW(
        s->list, GWLP_WNDPROC, (LONG_PTR)ng_list_proc);
  }

  sr_window_register_popup(s->dlg);

  ShowWindow(s->dlg, SW_SHOWNORMAL);
  UpdateWindow(s->dlg);
  if (s->list != NULL) { SetFocus(s->list); }
}

void sr_newgame_close(void)
{
  struct ng_state *s = g_ng;

  if (s == NULL) { return; }
  if (s->dlg != NULL) {
    sr_window_unregister_popup(s->dlg);
    DestroyWindow(s->dlg);
  }
  if (s->prev_focus != NULL && IsWindow(s->prev_focus)) {
    SetFocus(s->prev_focus);
  } else if (sr_play_hwnd() != NULL) {
    SetFocus(sr_play_hwnd());
  }
  free_server_rulesets(s);
  free(s);
  g_ng = NULL;
}

void sr_newgame_set_rulesets(int n, char **names)
{
  struct ng_state *s = g_ng;
  int i;

  if (s == NULL || n <= 0 || names == NULL) { return; }
  free_server_rulesets(s);
  s->server_rulesets = (char **)calloc((size_t)n, sizeof(char *));
  if (s->server_rulesets == NULL) { return; }
  for (i = 0; i < n; i++) {
    s->server_rulesets[i] = names[i] != NULL ? strdup(names[i]) : NULL;
  }
  s->n_server_rulesets = n;
}

void sr_newgame_flush_if_pending(void)
{
  char buf[64];

  if (!pending.active) { return; }
  pending.active = 0;

  sr_log_append("New Game: applying settings.");

  /* Ruleset first (pregame only). */
  if (pending.ruleset[0] != '\0') {
    set_ruleset(pending.ruleset);
  }

  snprintf(buf, sizeof(buf), "%d", pending.aifill);
  desired_settable_option_update("aifill", buf, FALSE);
  desired_settable_option_update("mapsize", pending.mapsize, FALSE);
  desired_settable_option_update("generator", pending.generator, FALSE);
  desired_settable_option_update("startpos", pending.startpos, FALSE);
  desired_settable_option_update("barbarians", pending.barbarians, FALSE);
  snprintf(buf, sizeof(buf), "%d", pending.onset_barbs);
  desired_settable_option_update("onsetbarbs", buf, FALSE);

  /* Send only the companion that matches the chosen mapsize method.
     (The others would be silently ignored; sending all three risks a
     rejection message per unused one.) */
  if (strcmp(pending.mapsize, "FULLSIZE") == 0) {
    snprintf(buf, sizeof(buf), "%d", pending.size_val);
    desired_settable_option_update("size", buf, FALSE);
  } else if (strcmp(pending.mapsize, "PLAYER") == 0) {
    snprintf(buf, sizeof(buf), "%d", pending.tilesperplayer_val);
    desired_settable_option_update("tilesperplayer", buf, FALSE);
  } else if (strcmp(pending.mapsize, "XYSIZE") == 0) {
    snprintf(buf, sizeof(buf), "%d", pending.xsize_val);
    desired_settable_option_update("xsize", buf, FALSE);
    snprintf(buf, sizeof(buf), "%d", pending.ysize_val);
    desired_settable_option_update("ysize", buf, FALSE);
  }

  /* Numeric-form flags are the robust cross-ruleset input. */
  snprintf(buf, sizeof(buf), "%d", pending.topology_idx);
  desired_settable_option_update("topology", buf, FALSE);
  snprintf(buf, sizeof(buf), "%d", pending.wrap_idx);
  desired_settable_option_update("wrap", buf, FALSE);
  snprintf(buf, sizeof(buf), "%d", pending.huts);
  desired_settable_option_update("huts", buf, FALSE);
  snprintf(buf, sizeof(buf), "%d", pending.landmass);
  desired_settable_option_update("landmass", buf, FALSE);
  snprintf(buf, sizeof(buf), "%d", pending.temperature);
  desired_settable_option_update("temperature", buf, FALSE);
  snprintf(buf, sizeof(buf), "%d", pending.endturn);
  desired_settable_option_update("endturn", buf, FALSE);
  snprintf(buf, sizeof(buf), "%d", pending.timeout);
  desired_settable_option_update("timeout", buf, FALSE);
  desired_settable_option_update("endspaceship",
      pending.endspaceship_idx == 1 ? "enabled" : "disabled", FALSE);
  desired_settable_option_update("separatepoles",
      pending.separatepoles ? "enabled" : "disabled", FALSE);
  desired_settable_option_update("startcity",
      pending.startcity ? "enabled" : "disabled", FALSE);

  if (pending.start_units[0] != '\0') {
    desired_settable_option_update("startunits", pending.start_units, FALSE);
  }
  resend_desired_settable_options();

  /* Difficulty is a chat verb, not a /set option. */
  if (pending.difficulty[0] != '\0') {
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "/%s", pending.difficulty);
    send_chat(cmd);
  }

  /* Don't auto-/start here. The user explicitly triggers /start via
     Alt+G, B (Begin game). Auto-starting from PAGE_START creates race
     conditions during server respawn on second and subsequent new
     games. */
  sr_say("Pregame lobby. Press Control Shift N to pick your nation,"
         " then Alt G then B to begin.", 1);
}

#else  /* !_WIN32 */

void sr_newgame_open(void) {}
void sr_newgame_close(void) {}
void sr_newgame_set_rulesets(int n, char **names) { (void)n; (void)names; }
void sr_newgame_flush_if_pending(void) {}

#endif  /* _WIN32 */
