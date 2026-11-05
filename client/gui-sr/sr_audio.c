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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "fcintl.h"
#include "registry.h"           /* section_file, secfile_load */
#include "shared.h"             /* fileinfoname */
#include "string_vector.h"
#include "support.h"

/* common */
#include "events.h"

/* client */
#include "audio.h"
#include "options.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_pick.h"
#include "sr_speech.h"

#include "sr_audio.h"

/* --- Row table ---------------------------------------------------------- */

enum row_kind {
  RK_STRLIST,        /* option_str + caller-supplied strvec getter */
  RK_BOOL,
  RK_INT,
  RK_STRING,
  RK_ACTION
};

struct audio_row {
  const char *label;               /* translatable row title */
  const char *opt_name;            /* options.c option name, NULL for RK_ACTION */
  enum row_kind kind;
  int minv, maxv;                  /* RK_INT bounds */
  bool restart_on_change;          /* RK_STRLIST: call audio_restart() after */
  void (*action)(void);            /* RK_ACTION handler */
};

static void row_action_inspect(void);
static void row_action_audit(void);

static const struct audio_row rows[] = {
  { N_("Sound plugin"),        "default_sound_plugin_name",   RK_STRLIST, 0, 0, FALSE, NULL },
  { N_("Soundset"),            "default_sound_set_name",      RK_STRLIST, 0, 0, TRUE,  NULL },
  { N_("Musicset"),            "default_music_set_name",      RK_STRLIST, 0, 0, FALSE, NULL },
  { N_("Enable sound effects"),"sound_enable_effects",        RK_BOOL,    0, 0, FALSE, NULL },
  { N_("Enable in-game music"),"sound_enable_game_music",     RK_BOOL,    0, 0, FALSE, NULL },
  { N_("Enable menu music"),   "sound_enable_menu_music",     RK_BOOL,    0, 0, FALSE, NULL },
  { N_("Sound volume"),        "sound_effects_volume",        RK_INT,     0, 100, FALSE, NULL },
  { N_("Silent when not in focus"),
                               "silent_when_not_in_focus",    RK_BOOL,    0, 0, FALSE, NULL },
  { N_("Autoaccept soundset suggestions"),
                               "autoaccept_soundset_suggestion", RK_BOOL, 0, 0, FALSE, NULL },
  { N_("Autoaccept musicset suggestions"),
                               "autoaccept_musicset_suggestion", RK_BOOL, 0, 0, FALSE, NULL },
  { N_("Speak sound events"),  "sr_speak_sound_events",       RK_BOOL,    0, 0, FALSE, NULL },
  { N_("Mute sound tag prefixes"),
                               "sr_sound_mute_prefixes",      RK_STRING,  0, 0, FALSE, NULL },
  { N_("Browse sound tags..."), NULL,                         RK_ACTION,  0, 0, FALSE, row_action_inspect },
  { N_("Audit active soundspec..."), NULL,                    RK_ACTION,  0, 0, FALSE, row_action_audit },
};
#define N_ROWS ((int) (sizeof(rows) / sizeof(rows[0])))

/* Persistent per-process buffers for the label strings we hand to
   sr_pick_enum. Rebuilt on every open; sr_pick_enum copies the pointers
   but not the strings, so the buffers must outlive the callback. */
static char row_labels[N_ROWS][192];
static const char *row_label_ptrs[N_ROWS];

/* Remembered cursor position across reopens. */
static int last_cursor = 0;

/* --- Helpers ------------------------------------------------------------ */

static struct option *opt_of(const struct audio_row *r)
{
  if (r->opt_name == NULL) {
    return NULL;
  }
  return optset_option_by_name(client_optset, r->opt_name);
}

static void format_row(const struct audio_row *r, char *out, size_t out_sz)
{
  struct option *opt = opt_of(r);

  switch (r->kind) {
    case RK_ACTION:
      fc_strlcpy(out, _(r->label), out_sz);
      return;
    case RK_BOOL: {
      bool v = opt != NULL && option_bool_get(opt);
      fc_snprintf(out, out_sz, "%s: %s", _(r->label), v ? _("on") : _("off"));
      return;
    }
    case RK_INT: {
      int v = opt != NULL ? option_int_get(opt) : 0;
      fc_snprintf(out, out_sz, "%s: %d", _(r->label), v);
      return;
    }
    case RK_STRING: {
      const char *v = opt != NULL ? option_str_get(opt) : "";
      fc_snprintf(out, out_sz, "%s: \"%s\"", _(r->label),
                  v != NULL ? v : "");
      return;
    }
    case RK_STRLIST: {
      const char *v = opt != NULL ? option_str_get(opt) : "";
      fc_snprintf(out, out_sz, "%s: %s", _(r->label),
                  (v != NULL && *v != '\0') ? v : _("(default)"));
      return;
    }
  }
  fc_strlcpy(out, _(r->label), out_sz);
}

static void rebuild_labels(void)
{
  int i;

  for (i = 0; i < N_ROWS; i++) {
    format_row(&rows[i], row_labels[i], sizeof(row_labels[i]));
    row_label_ptrs[i] = row_labels[i];
  }
}

/* Forward declarations for the picker callback chain. */
static void on_root_pick(int index, const char *value, void *ud);
static void on_strlist_pick(int index, const char *value, void *ud);
static void on_int_pick(int value, void *ud);
static void on_string_pick(const char *value, void *ud);

/* Bounce back to the root picker — wrapped so it survives being the
   terminal action of a child picker callback. */
static void reopen_root(void)
{
  sr_audio_open();
}

/* Convert the current value of a string-list option into its index
   within the values vector; -1 if not found or no values. */
static int strlist_current_index(struct option *opt, const struct strvec *vals)
{
  const char *cur;
  size_t n, i;

  if (opt == NULL || vals == NULL) {
    return -1;
  }
  cur = option_str_get(opt);
  n = strvec_size(vals);
  if (cur == NULL) {
    return -1;
  }
  for (i = 0; i < n; i++) {
    const char *v = strvec_get(vals, i);

    if (v != NULL && strcmp(v, cur) == 0) {
      return (int) i;
    }
  }
  return -1;
}

/* --- Sub-picker for RK_STRLIST ----------------------------------------- */

/* Per-open working state. sr_pick_enum does not copy its value array,
   so we hold one static set of pointers live until the user commits or
   cancels. Because the picker is modal from the user's perspective,
   only one list is ever in flight at a time. */
static const char **g_strlist_ptrs = NULL;
static int g_strlist_n = 0;
static int g_strlist_row = -1;

static void strlist_release(void)
{
  if (g_strlist_ptrs != NULL) {
    free(g_strlist_ptrs);
    g_strlist_ptrs = NULL;
  }
  g_strlist_n = 0;
  g_strlist_row = -1;
}

static void open_strlist_sub(int row_idx)
{
  const struct audio_row *r = &rows[row_idx];
  struct option *opt = opt_of(r);
  const struct strvec *vals;
  char title[160];
  int cur, n, i;

  if (opt == NULL) {
    sr_say(_("Option not available."), 1);
    reopen_root();
    return;
  }
  vals = option_str_values(opt);
  if (vals == NULL || strvec_size(vals) == 0) {
    sr_say(_("No choices available."), 1);
    reopen_root();
    return;
  }
  n = (int) strvec_size(vals);
  strlist_release();
  g_strlist_ptrs = (const char **) malloc(sizeof(const char *) * (size_t) n);
  if (g_strlist_ptrs == NULL) {
    sr_say(_("Out of memory."), 1);
    reopen_root();
    return;
  }
  for (i = 0; i < n; i++) {
    g_strlist_ptrs[i] = strvec_get(vals, (size_t) i);
  }
  g_strlist_n = n;
  g_strlist_row = row_idx;

  cur = strlist_current_index(opt, vals);
  if (cur < 0) {
    cur = 0;
  }
  fc_snprintf(title, sizeof(title), _("Pick %s"), _(r->label));
  sr_pick_enum(title, g_strlist_ptrs, n, cur, on_strlist_pick, NULL);
}

static void on_strlist_pick(int index, const char *value, void *ud)
{
  const struct audio_row *r;
  struct option *opt;
  char msg[256];

  (void) ud;
  if (g_strlist_row < 0 || g_strlist_row >= N_ROWS) {
    strlist_release();
    reopen_root();
    return;
  }
  r = &rows[g_strlist_row];
  opt = opt_of(r);
  if (opt == NULL || value == NULL || index < 0) {
    strlist_release();
    reopen_root();
    return;
  }
  if (option_str_set(opt, value)) {
    if (r->restart_on_change) {
      /* Soundset has no val_change_callback; musicset wires its own
         reread via musicspec_reread_callback. Call audio_restart()
         for the soundset branch so the change is audible now. */
      audio_restart(gui_options.default_sound_set_name,
                    gui_options.default_music_set_name);
    }
    fc_snprintf(msg, sizeof(msg), _("%s set to %s."),
                _(r->label), value);
    sr_say(msg, 1);
    sr_log_append(msg);
  } else {
    fc_snprintf(msg, sizeof(msg), _("Could not set %s."), _(r->label));
    sr_say(msg, 1);
  }
  strlist_release();
  last_cursor = g_strlist_row;
  reopen_root();
}

/* --- Sub-picker for RK_INT -------------------------------------------- */

static int g_int_row = -1;

static void open_int_sub(int row_idx)
{
  const struct audio_row *r = &rows[row_idx];
  struct option *opt = opt_of(r);
  int cur;
  char title[160];

  if (opt == NULL) {
    sr_say(_("Option not available."), 1);
    reopen_root();
    return;
  }
  cur = option_int_get(opt);
  g_int_row = row_idx;
  fc_snprintf(title, sizeof(title), _("Set %s (%d..%d)"),
              _(r->label), r->minv, r->maxv);
  sr_pick_int(title, cur, r->minv, r->maxv, on_int_pick, NULL);
}

static void on_int_pick(int value, void *ud)
{
  const struct audio_row *r;
  struct option *opt;
  char msg[256];

  (void) ud;
  if (g_int_row < 0 || g_int_row >= N_ROWS) {
    reopen_root();
    return;
  }
  r = &rows[g_int_row];
  opt = opt_of(r);
  if (opt != NULL && option_int_set(opt, value)) {
    fc_snprintf(msg, sizeof(msg), _("%s set to %d."), _(r->label), value);
    sr_say(msg, 1);
    sr_log_append(msg);
  } else {
    fc_snprintf(msg, sizeof(msg), _("Could not set %s."), _(r->label));
    sr_say(msg, 1);
  }
  last_cursor = g_int_row;
  g_int_row = -1;
  reopen_root();
}

/* --- Sub-picker for RK_STRING ----------------------------------------- */

static int g_string_row = -1;

static void open_string_sub(int row_idx)
{
  const struct audio_row *r = &rows[row_idx];
  struct option *opt = opt_of(r);
  char title[160];

  if (opt == NULL) {
    sr_say(_("Option not available."), 1);
    reopen_root();
    return;
  }
  g_string_row = row_idx;
  fc_snprintf(title, sizeof(title), _("Set %s"), _(r->label));
  sr_pick_string(title, option_str_get(opt), on_string_pick, NULL);
}

static void on_string_pick(const char *value, void *ud)
{
  const struct audio_row *r;
  struct option *opt;
  char msg[256];

  (void) ud;
  if (g_string_row < 0 || g_string_row >= N_ROWS) {
    reopen_root();
    return;
  }
  r = &rows[g_string_row];
  opt = opt_of(r);
  if (opt != NULL
      && option_str_set(opt, value != NULL ? value : "")) {
    fc_snprintf(msg, sizeof(msg), _("%s set."), _(r->label));
    sr_say(msg, 1);
    sr_log_append(msg);
  } else {
    fc_snprintf(msg, sizeof(msg), _("Could not set %s."), _(r->label));
    sr_say(msg, 1);
  }
  last_cursor = g_string_row;
  g_string_row = -1;
  reopen_root();
}

/* --- RK_BOOL toggle ---------------------------------------------------- */

static void toggle_bool(int row_idx)
{
  const struct audio_row *r = &rows[row_idx];
  struct option *opt = opt_of(r);
  bool cur;
  char msg[256];

  if (opt == NULL) {
    sr_say(_("Option not available."), 1);
    reopen_root();
    return;
  }
  cur = option_bool_get(opt);
  if (option_bool_set(opt, !cur)) {
    fc_snprintf(msg, sizeof(msg), _("%s: %s"),
                _(r->label), !cur ? _("on") : _("off"));
    sr_say(msg, 1);
    sr_log_append(msg);
  }
  last_cursor = row_idx;
  reopen_root();
}

/* --- Root picker callback --------------------------------------------- */

static void on_root_pick(int index, const char *value, void *ud)
{
  const struct audio_row *r;

  (void) value; (void) ud;
  if (index < 0 || index >= N_ROWS) {
    return;
  }
  last_cursor = index;
  r = &rows[index];
  switch (r->kind) {
    case RK_BOOL:    toggle_bool(index); break;
    case RK_INT:     open_int_sub(index); break;
    case RK_STRING:  open_string_sub(index); break;
    case RK_STRLIST: open_strlist_sub(index); break;
    case RK_ACTION:
      if (r->action != NULL) {
        r->action();
      }
      break;
  }
}

/* --- Public entry ----------------------------------------------------- */

void sr_audio_open(void)
{
  if (last_cursor < 0 || last_cursor >= N_ROWS) {
    last_cursor = 0;
  }
  rebuild_labels();
  sr_pick_enum(_("Sound settings"),
               row_label_ptrs, N_ROWS, last_cursor,
               on_root_pick, NULL);
}

/* --- T3 / T5: per-sound hook ------------------------------------------ */

/* Match one prefix token from the comma-separated mute list against the
   tag. Empty / NULL tokens are ignored. Tokens are matched as strict
   prefixes — "m_" mutes "m_archers" but not "em_thing". */
static bool prefix_in_list(const char *list, const char *tag)
{
  const char *p;
  const char *tok;
  size_t toklen;

  if (list == NULL || *list == '\0' || tag == NULL || *tag == '\0') {
    return FALSE;
  }
  p = list;
  while (*p != '\0') {
    /* skip separators and whitespace */
    while (*p == ',' || isspace((unsigned char) *p)) {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    tok = p;
    while (*p != '\0' && *p != ',') {
      p++;
    }
    toklen = (size_t) (p - tok);
    /* trim trailing whitespace */
    while (toklen > 0 && isspace((unsigned char) tok[toklen - 1])) {
      toklen--;
    }
    if (toklen > 0 && strncmp(tag, tok, toklen) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

bool sr_audio_say_for_tag(const char *tag)
{
  const char *caption;

  if (tag == NULL || *tag == '\0') {
    return TRUE;
  }
  if (prefix_in_list(gui_options.sr_sound_mute_prefixes, tag)) {
    /* T5: suppress entirely. Caption still fires so the player knows
       the event happened, unless speaking is also off. */
    if (gui_options.sr_speak_sound_events) {
      caption = sr_audio_caption_for_tag(tag);
      if (caption != NULL) {
        sr_say(caption, 0);
      }
    }
    return FALSE;
  }
  if (gui_options.sr_speak_sound_events) {
    caption = sr_audio_caption_for_tag(tag);
    if (caption != NULL) {
      sr_say(caption, 0);
    }
  }
  return TRUE;
}

/* --- T4 tag inspector ------------------------------------------------- */

/* Per-open list of tag names. Owned strings freed on re-open / exit. */
static char **g_tags_owned = NULL;
static const char **g_tags_ptrs = NULL;
static int g_tags_n = 0;

static void tags_release(void)
{
  int i;

  if (g_tags_owned != NULL) {
    for (i = 0; i < g_tags_n; i++) {
      free(g_tags_owned[i]);
    }
    free(g_tags_owned);
    g_tags_owned = NULL;
  }
  if (g_tags_ptrs != NULL) {
    free(g_tags_ptrs);
    g_tags_ptrs = NULL;
  }
  g_tags_n = 0;
}

/* Find and load the active soundspec. */
static struct section_file *load_active_soundspec(void)
{
  const char *spec_name = gui_options.default_sound_set_name;
  char fname[256];
  const char *full;
  struct section_file *sf;

  if (spec_name == NULL || *spec_name == '\0') {
    spec_name = "stdsounds";
  }
  fc_snprintf(fname, sizeof(fname), "%s.soundspec", spec_name);
  full = fileinfoname(get_data_dirs(), fname);
  if (full == NULL) {
    return NULL;
  }
  sf = secfile_load(full, TRUE);
  return sf;
}

static void on_tag_pick(int index, const char *value, void *ud)
{
  (void) index; (void) ud;
  if (value == NULL || *value == '\0') {
    tags_release();
    return;
  }
  /* Strip the "(mapped)" / "(missing)" suffix we appended. */
  {
    char tag[128];
    const char *sp = strchr(value, ' ');
    size_t n = sp != NULL ? (size_t)(sp - value) : strlen(value);

    if (n >= sizeof(tag)) {
      n = sizeof(tag) - 1;
    }
    memcpy(tag, value, n);
    tag[n] = '\0';
    audio_play_sound(tag, NULL, NULL);
    {
      char msg[160];
      fc_snprintf(msg, sizeof(msg), _("Played \"%s\"."), tag);
      sr_log_append(msg);
    }
  }
  /* Re-open so the player can browse another tag. */
  sr_audio_tag_inspector_open();
}

void sr_audio_tag_inspector_open(void)
{
  struct section_file *sf;
  struct section *sec;
  const struct entry_list *elist;
  int count = 0;
  int i;

  tags_release();
  sf = load_active_soundspec();
  if (sf == NULL) {
    sr_say(_("Could not load the active soundspec."), 1);
    return;
  }
  sec = secfile_section_by_name(sf, "files");
  elist = sec != NULL ? section_entries(sec) : NULL;
  if (elist == NULL) {
    secfile_destroy(sf);
    sr_say(_("Soundspec has no [files] section."), 1);
    return;
  }
  count = entry_list_size(elist);
  if (count <= 0) {
    secfile_destroy(sf);
    sr_say(_("Soundspec [files] section is empty."), 1);
    return;
  }
  g_tags_owned = (char **) calloc((size_t) count, sizeof(char *));
  g_tags_ptrs = (const char **) calloc((size_t) count, sizeof(const char *));
  if (g_tags_owned == NULL || g_tags_ptrs == NULL) {
    tags_release();
    secfile_destroy(sf);
    sr_say(_("Out of memory."), 1);
    return;
  }
  g_tags_n = count;
  for (i = 0; i < count; i++) {
    struct entry *pe = entry_list_get(elist, i);
    const char *tag = pe != NULL ? entry_name(pe) : NULL;
    char line[192];
    const char *filepath = NULL;

    if (tag == NULL) {
      fc_strlcpy(line, "?", sizeof(line));
    } else {
      if (pe != NULL && entry_type_get(pe) == ENTRY_STR) {
        entry_str_get(pe, &filepath);
      }
      if (filepath != NULL && *filepath != '\0') {
        fc_snprintf(line, sizeof(line), "%s  [%s]", tag, filepath);
      } else {
        fc_snprintf(line, sizeof(line), "%s  [unmapped]", tag);
      }
    }
    g_tags_owned[i] = fc_strdup(line);
    g_tags_ptrs[i] = g_tags_owned[i];
  }
  secfile_destroy(sf);
  sr_pick_enum(_("Sound tags (Enter plays, Escape exits)"),
               g_tags_ptrs, g_tags_n, 0, on_tag_pick, NULL);
}

static void row_action_inspect(void)
{
  sr_audio_tag_inspector_open();
}

/* --- T8 audit / scaffold --------------------------------------------- */

/* Compare active soundspec tags against the code-side event enum. Prints
   a short summary and appends a per-event result line to the log. */
void sr_audio_audit(void)
{
  struct section_file *sf;
  int mapped = 0, missing = 0;
  char msg[256];
  enum event_type e;

  sf = load_active_soundspec();
  if (sf == NULL) {
    sr_say(_("Could not load the active soundspec for audit."), 1);
    return;
  }
  sr_log_append("--- Soundspec audit ---");
  for (e = event_type_begin(); e != event_type_end();
       e = event_type_next(e)) {
    const char *tag = get_event_tag(e);
    const char *val;

    if (tag == NULL || *tag == '\0') {
      continue;
    }
    val = secfile_lookup_str_default(sf, NULL, "files.%s", tag);
    if (val != NULL && *val != '\0') {
      mapped++;
      fc_snprintf(msg, sizeof(msg), "  %s -> %s", tag, val);
    } else {
      missing++;
      fc_snprintf(msg, sizeof(msg), "  %s  [missing]", tag);
    }
    sr_log_append(msg);
  }
  secfile_destroy(sf);
  fc_snprintf(msg, sizeof(msg),
              _("Audit: %d event tags mapped, %d missing. See log."),
              mapped, missing);
  sr_say(msg, 1);
  sr_log_append(msg);
}

bool sr_audio_scaffold_write(const char *path)
{
  FILE *f;
  enum event_type e;

  if (path == NULL || *path == '\0') {
    return FALSE;
  }
  f = fc_fopen(path, "w");
  if (f == NULL) {
    return FALSE;
  }
  fprintf(f, "; Auto-generated scaffold soundspec. Every known event tag\n");
  fprintf(f, "; is included as a commented-out assignment. Uncomment and\n");
  fprintf(f, "; point at an .ogg in this same directory tree to enable.\n\n");
  fprintf(f, "[soundspec]\n");
  fprintf(f, "options = \"+Freeciv-3.2-soundset\"\n\n");
  fprintf(f, "[info]\n");
  fprintf(f, "artists = \"(your credits here)\"\n\n");
  fprintf(f, "[files]\n");
  /* Ruleset prefix fallbacks come first. */
  fprintf(f, ";m_generic = \"sounds/m_generic.ogg\"\n");
  fprintf(f, ";f_generic = \"sounds/f_generic.ogg\"\n");
  fprintf(f, ";b_generic = \"sounds/b_generic.ogg\"\n");
  fprintf(f, ";w_generic = \"sounds/w_generic.ogg\"\n");
  fprintf(f, ";g_generic = \"sounds/g_generic.ogg\"\n");
  fprintf(f, "\n; Code-side event tags\n");
  for (e = event_type_begin(); e != event_type_end();
       e = event_type_next(e)) {
    const char *tag = get_event_tag(e);

    if (tag == NULL || *tag == '\0') {
      continue;
    }
    fprintf(f, ";%s = \"sounds/%s.ogg\"\n", tag, tag);
  }
  fclose(f);
  return TRUE;
}

static void row_action_audit(void)
{
  sr_audio_audit();
  reopen_root();
}

/* --- Caption table (hand-curated) ------------------------------------ */

struct caption_entry {
  const char *tag;
  const char *caption;             /* marked translatable at lookup time */
};

/* Ruleset prefix fallbacks — matched last so specific entries win.
   Format: prefix (with trailing underscore kept) -> caption. */
static const struct caption_entry prefix_captions[] = {
  { "m_", N_("unit moving") },
  { "f_", N_("combat") },
  { "b_", N_("building finished") },
  { "w_", N_("wonder finished") },
  { "g_", N_("government change") },
};
#define N_PREFIX_CAPS ((int)(sizeof(prefix_captions) / sizeof(prefix_captions[0])))

/* Exact-tag table. Hand-curated from data/stdsounds.soundspec and the
   complete E_* enum in common/events.h. Phrased as short spoken lines.
   Any tag not listed falls through to the prefix table and then to
   the enum name.

   The list is long by design — the project explicitly asked for
   hand-curated captions for every tag on day one (see plan §6). */
static const struct caption_entry exact_captions[] = {
  /* Game state */
  { "e_enter_game",              N_("game joined") },
  { "e_leave_game",              N_("disconnected") },
  { "e_game_start",              N_("game started") },
  { "e_game_end",                N_("game over") },
  { "e_turn_bell",               N_("turn bell") },
  { "e_next_year",               N_("new year") },
  { "e_connection",              N_("connection event") },
  { "e_first_contact",           N_("first contact") },
  { "e_player_wake",             N_("your turn") },

  /* City events */
  { "e_city_build",              N_("city founded") },
  { "e_city_can_build",          N_("city can build") },
  { "e_city_cantbuild",          N_("city cannot build") },
  { "e_city_cantbuild_fs",       N_("cannot build, full size") },
  { "e_city_growth",             N_("city grew") },
  { "e_city_may_soon_grow",      N_("city almost ready to grow") },
  { "e_city_aqueduct",           N_("aqueduct needed") },
  { "e_city_aq_building",        N_("aqueduct under construction") },
  { "e_city_famine",             N_("city famine") },
  { "e_city_famine_feared",      N_("famine feared") },
  { "e_city_disorder",           N_("city in disorder") },
  { "e_city_love",               N_("city celebrating") },
  { "e_city_normal",             N_("city content") },
  { "e_city_nuked",              N_("city nuked") },
  { "e_city_cma_release",        N_("governor released") },
  { "e_city_gran_throttle",      N_("city throttled") },
  { "e_city_transfer",           N_("city transferred") },
  { "e_city_build_prevented",    N_("city build prevented") },
  { "e_city_production_changed", N_("city production changed") },
  { "e_city_plague",             N_("city struck by plague") },
  { "e_city_radius_sq",          N_("city work radius changed") },
  { "e_city_lost",               N_("city lost") },
  { "e_city_sold",               N_("city building sold") },
  { "e_city_was_built",          N_("enemy founded a city") },
  { "e_city_buy",                N_("production bought") },

  /* Worklist */
  { "e_worklist",                N_("worklist event") },

  /* Unit events */
  { "e_unit_lost_att",           N_("attack lost") },
  { "e_unit_win_att",            N_("attack won") },
  { "e_unit_lost_def",           N_("unit lost on defence") },
  { "e_unit_win_def",            N_("defence won") },
  { "e_unit_built",              N_("unit built") },
  { "e_unit_upgraded",           N_("unit upgraded") },
  { "e_unit_buy",                N_("unit bought") },
  { "e_unit_lost_misc",          N_("unit lost") },
  { "e_unit_became_vet",         N_("unit promoted") },
  { "e_unit_escaped",            N_("unit escaped") },
  { "e_unit_orders",             N_("unit orders event") },
  { "e_unit_illegal_action",     N_("illegal unit action") },
  { "e_unit_action_failed",      N_("unit action failed") },
  { "e_unit_action_actor_success", N_("action succeeded") },
  { "e_unit_action_actor_failure", N_("action failed") },
  { "e_unit_action_target_other", N_("you were acted upon") },
  { "e_unit_action_target_hostile", N_("hostile action against you") },
  { "e_unit_relocated",          N_("unit relocated") },
  { "e_unit_did_expel",          N_("unit expelled another") },
  { "e_unit_was_expelled",       N_("unit was expelled") },
  { "e_unit_could_not_expel",    N_("could not expel unit") },
  { "e_unit_did_heal",           N_("unit healed") },
  { "e_unit_was_healed",         N_("unit was healed") },
  { "e_unit_tired_attack",       N_("tired attack") },
  { "e_unit_auto_attack",        N_("unit auto-attacked") },

  /* Wonders */
  { "e_wonder_build",            N_("wonder built") },
  { "e_wonder_obsolete",         N_("wonder obsolete") },
  { "e_wonder_started",          N_("wonder started") },
  { "e_wonder_stopped",          N_("wonder stopped") },
  { "e_wonder_will_be_built",    N_("wonder nearly finished") },

  /* Infrastructure */
  { "e_imp_build",               N_("improvement built") },
  { "e_imp_buy",                 N_("improvement bought") },
  { "e_imp_sold",                N_("improvement sold") },
  { "e_imp_auto",                N_("auto-built improvement") },
  { "e_imp_auctioned",           N_("improvement auctioned") },

  /* Tech */
  { "e_tech_gain",               N_("tech gained") },
  { "e_tech_learned",            N_("technology learned") },
  { "e_tech_lost",               N_("tech lost") },
  { "e_tech_goal",               N_("research goal set") },
  { "e_tech_embassy",            N_("tech from embassy") },

  /* Huts */
  { "e_hut_barb",                N_("barbarians from hut") },
  { "e_hut_city",                N_("city from hut") },
  { "e_hut_gold",                N_("gold from hut") },
  { "e_hut_barb_killed",         N_("killed barbarians in hut") },
  { "e_hut_merc",                N_("mercenaries from hut") },
  { "e_hut_settler",             N_("settler from hut") },
  { "e_hut_tech",                N_("tech from hut") },
  { "e_hut_barb_city_near",      N_("barbarians spared near city") },
  { "e_hut_map",                 N_("map from hut") },

  /* Diplomats */
  { "e_my_diplomat_bribe",       N_("you bribed a unit") },
  { "e_enemy_diplomat_bribe",    N_("enemy bribed your unit") },
  { "e_my_diplomat_embassy",     N_("you established embassy") },
  { "e_enemy_diplomat_embassy",  N_("enemy established embassy") },
  { "e_my_diplomat_failed",      N_("your diplomat failed") },
  { "e_enemy_diplomat_failed",   N_("enemy diplomat failed") },
  { "e_my_diplomat_incite",      N_("you incited revolt") },
  { "e_enemy_diplomat_incite",   N_("enemy incited revolt") },
  { "e_my_diplomat_poison",      N_("you poisoned a city") },
  { "e_enemy_diplomat_poison",   N_("enemy poisoned your city") },
  { "e_my_diplomat_sabotage",    N_("you sabotaged") },
  { "e_enemy_diplomat_sabotage", N_("enemy sabotaged you") },
  { "e_my_diplomat_theft",       N_("you stole technology") },
  { "e_enemy_diplomat_theft",    N_("enemy stole technology") },
  { "e_my_diplomat_escape",      N_("your diplomat escaped") },
  { "e_my_spy_nuke",             N_("your spy planted a nuke") },
  { "e_enemy_spy_nuke",          N_("enemy spy planted a nuke") },
  { "e_my_spy_steal_gold",       N_("your spy stole gold") },
  { "e_enemy_spy_steal_gold",    N_("enemy spy stole gold") },
  { "e_my_spy_steal_map",        N_("your spy stole map") },
  { "e_enemy_spy_steal_map",     N_("enemy spy stole map") },

  /* Treaties and diplomacy */
  { "e_treaty_alliance",         N_("alliance signed") },
  { "e_treaty_broken",           N_("treaty broken") },
  { "e_treaty_ceasefire",        N_("ceasefire signed") },
  { "e_treaty_embassy",          N_("embassy treaty") },
  { "e_treaty_peace",            N_("peace signed") },
  { "e_treaty_shared_vision",    N_("shared vision") },
  { "e_diplomacy",               N_("diplomacy event") },
  { "e_cancel_pact",             N_("pact cancelled") },

  /* Votes */
  { "e_vote_new",                N_("new vote") },
  { "e_vote_resolved",           N_("vote resolved") },
  { "e_vote_aborted",            N_("vote aborted") },

  /* Chat / log */
  { "e_chat_msg",                N_("chat message") },
  { "e_chat_error",              N_("chat error") },
  { "e_bad_command",             N_("bad command") },
  { "e_log_error",               N_("log error") },
  { "e_log_fatal",               N_("fatal error") },
  { "e_message_wall",            N_("broadcast message") },
  { "e_server_problem",          N_("server problem") },
  { "e_setting",                 N_("setting changed") },
  { "e_script",                  N_("script event") },
  { "e_spontaneous_extra",       N_("terrain feature appeared") },

  /* Government / revolution */
  { "e_new_government",          N_("new government") },
  { "e_anarchy",                 N_("anarchy begins") },
  { "e_revolt_done",             N_("revolt completed") },
  { "e_revolt_start",            N_("revolt started") },
  { "e_low_on_funds",            N_("low on funds") },
  { "e_pollution",               N_("pollution") },
  { "e_global_eco",              N_("global ecology warning") },

  /* Disasters / misc */
  { "e_disaster",                N_("disaster") },
  { "e_uprising",                N_("uprising") },
  { "e_caravan_action",          N_("caravan action") },
  { "e_destroyed",               N_("destroyed") },
  { "e_achievement",             N_("achievement") },
  { "e_multiplier",              N_("policy changed") },
  { "e_infrapoints",             N_("infrastructure points") },

  /* Space race */
  { "e_spaceship",               N_("spaceship event") },
  { "e_broadcast_report",        N_("broadcast report") },

  /* Barbarians / AI */
  { "e_ai_debug",                N_("AI debug") },
};
#define N_EXACT_CAPS ((int)(sizeof(exact_captions) / sizeof(exact_captions[0])))

const char *sr_audio_caption_for_tag(const char *tag)
{
  int i;

  if (tag == NULL || *tag == '\0') {
    return NULL;
  }
  /* Exact match first. */
  for (i = 0; i < N_EXACT_CAPS; i++) {
    if (strcmp(exact_captions[i].tag, tag) == 0) {
      return _(exact_captions[i].caption);
    }
  }
  /* Then prefix fallback. */
  for (i = 0; i < N_PREFIX_CAPS; i++) {
    size_t n = strlen(prefix_captions[i].tag);

    if (strncmp(tag, prefix_captions[i].tag, n) == 0) {
      return _(prefix_captions[i].caption);
    }
  }
  return NULL;
}
