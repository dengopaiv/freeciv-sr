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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#endif

/* utility */
#include "fcintl.h"
#include "support.h"

/* common */
#include "calendar.h"
#include "city.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "nation.h"
#include "player.h"
#include "research.h"
#include "tech.h"
#include "tile.h"
#include "unit.h"
#include "unitlist.h"

/* client */
#include "climap.h"
#include "client_main.h"
#include "control.h"
#include "text.h"

/* gui-sr */
#include "sr_tile.h"
#include "sr_window.h"

#include "sr_tree.h"

#ifdef _WIN32

static HTREEITEM g_section_item[SR_TREE_N_SECTIONS] = { NULL };
static int g_sections_ready = 0;

/* ==================================================================== */
/* Helpers                                                              */
/* ==================================================================== */

static HWND tree_hwnd(void)
{
  return sr_window_tree_hwnd();
}

static wchar_t *utf8_to_wide_alloc(const char *utf8)
{
  int wlen;
  wchar_t *w;

  if (utf8 == NULL) {
    return NULL;
  }
  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (wlen <= 0) {
    return NULL;
  }
  w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
  if (w == NULL) {
    return NULL;
  }
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
  return w;
}

/* Remove all children under parent_item. */
static void clear_children(HTREEITEM parent_item)
{
  HWND tv = tree_hwnd();
  HTREEITEM child;

  if (tv == NULL) {
    return;
  }
  while ((child = TreeView_GetChild(tv, parent_item)) != NULL) {
    TreeView_DeleteItem(tv, child);
  }
}

/* Append a plain-text child under parent_item. Returns the new item. */
static HTREEITEM add_child(HTREEITEM parent_item, const char *utf8_text,
                           LPARAM lparam)
{
  HWND tv = tree_hwnd();
  TVINSERTSTRUCTW tv_ins;
  wchar_t *w;
  HTREEITEM item;

  if (tv == NULL || utf8_text == NULL) {
    return NULL;
  }
  w = utf8_to_wide_alloc(utf8_text);
  if (w == NULL) {
    return NULL;
  }
  ZeroMemory(&tv_ins, sizeof(tv_ins));
  tv_ins.hParent      = parent_item;
  tv_ins.hInsertAfter = TVI_LAST;
  tv_ins.item.mask    = TVIF_TEXT | TVIF_PARAM;
  tv_ins.item.pszText = w;
  tv_ins.item.lParam  = lparam;
  item = TreeView_InsertItem(tv, &tv_ins);
  free(w);
  return item;
}

/* Printf-style variant. */
static HTREEITEM add_childf(HTREEITEM parent_item, LPARAM lparam,
                            const char *fmt, ...)
{
  char buf[1024];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return add_child(parent_item, buf, lparam);
}

/* Set the section header's own label (the N-count suffix: "Units (8)"). */
static void set_section_text(enum sr_tree_section s, const char *utf8_text)
{
  HWND tv = tree_hwnd();
  TVITEMW ti;
  wchar_t *w;

  if (tv == NULL || s < 0 || s >= SR_TREE_N_SECTIONS || !g_sections_ready) {
    return;
  }
  w = utf8_to_wide_alloc(utf8_text);
  if (w == NULL) {
    return;
  }
  ZeroMemory(&ti, sizeof(ti));
  ti.mask     = TVIF_TEXT | TVIF_HANDLE;
  ti.hItem    = g_section_item[s];
  ti.pszText  = w;
  TreeView_SetItem(tv, &ti);
  free(w);
}

/* ==================================================================== */
/* Section creation                                                     */
/* ==================================================================== */

void sr_tree_create_sections(void)
{
  HWND tv = tree_hwnd();
  static const char *const titles[SR_TREE_N_SECTIONS] = {
    "Game",
    "Units",
    "Cities",
    "Review cursor",
    "Known nations",
    "Research",
    "Pregame"
  };
  int i;
  TVINSERTSTRUCTW ti;

  if (tv == NULL || g_sections_ready) {
    return;
  }
  /* Clear whatever sr_window seeded, then re-insert in our canonical
     order so lParam indices match SR_TREE_* enum values. */
  TreeView_DeleteAllItems(tv);

  for (i = 0; i < SR_TREE_N_SECTIONS; i++) {
    wchar_t *w = utf8_to_wide_alloc(titles[i]);

    ZeroMemory(&ti, sizeof(ti));
    ti.hParent      = TVI_ROOT;
    ti.hInsertAfter = TVI_LAST;
    ti.item.mask    = TVIF_TEXT | TVIF_PARAM;
    ti.item.pszText = w != NULL ? w : (wchar_t *)L"?";
    ti.item.lParam  = (LPARAM)i;
    g_section_item[i] = TreeView_InsertItem(tv, &ti);
    if (w != NULL) {
      free(w);
    }
  }
  g_sections_ready = 1;
}

void sr_tree_focus_section(enum sr_tree_section section)
{
  HWND tv = tree_hwnd();

  if (tv == NULL || section < 0 || section >= SR_TREE_N_SECTIONS
      || !g_sections_ready) {
    return;
  }
  TreeView_SelectItem(tv, g_section_item[section]);
  SetFocus(tv);
}

/* ==================================================================== */
/* Section rebuilders                                                   */
/* ==================================================================== */

void sr_tree_rebuild_game(void)
{
  struct player *pl;
  char title_buf[64];

  if (!g_sections_ready) { return; }
  clear_children(g_section_item[SR_TREE_GAME]);

  pl = client_player();

  add_childf(g_section_item[SR_TREE_GAME], 0,
             "Turn: %d (%s)", game.info.turn, calendar_text());

  if (pl != NULL) {
    add_childf(g_section_item[SR_TREE_GAME], 0,
               "Gold: %d (%+d income)",
               pl->economic.gold, player_get_expected_income(pl));
    add_childf(g_section_item[SR_TREE_GAME], 0,
               "Rates: Tax %d / Luxury %d / Science %d",
               pl->economic.tax, pl->economic.luxury, pl->economic.science);
    if (government_of_player(pl) != NULL) {
      add_childf(g_section_item[SR_TREE_GAME], 0,
                 "Government: %s",
                 government_name_translation(government_of_player(pl)));
    }
    add_childf(g_section_item[SR_TREE_GAME], 0,
               "Nation: %s (%s)",
               nation_adjective_for_player(pl), player_name(pl));
    add_childf(g_section_item[SR_TREE_GAME], 0,
               "Score: %d", pl->score.game);
  } else {
    add_child(g_section_item[SR_TREE_GAME], "No player yet", 0);
  }

  fc_snprintf(title_buf, sizeof(title_buf),
              "Game  —  Turn %d", game.info.turn);
  set_section_text(SR_TREE_GAME, title_buf);
}

static const char *unit_activity_text(const struct unit *punit)
{
  switch (punit->activity) {
    case ACTIVITY_IDLE:      return "idle";
    case ACTIVITY_FORTIFIED: return "fortified";
    case ACTIVITY_FORTIFYING: return "fortifying";
    case ACTIVITY_SENTRY:    return "sentry";
    case ACTIVITY_GOTO:      return "goto";
    case ACTIVITY_EXPLORE:   return "exploring";
    case ACTIVITY_MINE:      return "mining";
    case ACTIVITY_IRRIGATE:  return "irrigating";
    case ACTIVITY_PLANT:     return "planting";
    case ACTIVITY_CULTIVATE: return "cultivating";
    case ACTIVITY_TRANSFORM: return "transforming";
    case ACTIVITY_CLEAN:     return "cleaning";
    case ACTIVITY_PILLAGE:   return "pillaging";
    case ACTIVITY_GEN_ROAD:  return "building road";
    case ACTIVITY_BASE:      return "building base";
    default:                 return "busy";
  }
}

void sr_tree_rebuild_units(void)
{
  struct player *pl;
  char title_buf[64];
  int count = 0;

  if (!g_sections_ready) { return; }
  clear_children(g_section_item[SR_TREE_UNITS]);

  pl = client_player();
  if (pl == NULL) {
    set_section_text(SR_TREE_UNITS, "Units");
    return;
  }

  unit_list_iterate(pl->units, punit) {
    int tx = 0, ty = 0;
    HTREEITEM hrow;
    const struct unit_type *utype = unit_type_get(punit);
    struct tile *ut = unit_tile(punit);
    const char *terrain_name =
        (ut != NULL && tile_terrain(ut) != NULL)
        ? terrain_name_translation(tile_terrain(ut)) : "?";

    if (ut != NULL) {
      index_to_map_pos(&tx, &ty, tile_index(ut));
    }
    hrow = add_childf(g_section_item[SR_TREE_UNITS], (LPARAM)punit->id,
                      "%s at %d,%d %s — %d/%d moves — %s",
                      utype_name_translation(utype),
                      tx, ty, terrain_name,
                      punit->moves_left / SINGLE_MOVE,
                      utype->move_rate / SINGLE_MOVE,
                      unit_activity_text(punit));
    if (hrow != NULL) {
      add_childf(hrow, 0, "HP %d / %d, veteran %d",
                 punit->hp, utype->hp, punit->veteran);
      if (punit->homecity > 0) {
        struct city *home = player_city_by_number(pl, punit->homecity);
        if (home != NULL) {
          add_childf(hrow, 0, "Home: %s", city_name_get(home));
        }
      }
    }
    count++;
  } unit_list_iterate_end;

  if (count == 0) {
    add_child(g_section_item[SR_TREE_UNITS], "No units", 0);
  }
  fc_snprintf(title_buf, sizeof(title_buf), "Units (%d)", count);
  set_section_text(SR_TREE_UNITS, title_buf);
}

void sr_tree_rebuild_cities(void)
{
  struct player *pl;
  char title_buf[64];
  int count = 0;

  if (!g_sections_ready) { return; }
  clear_children(g_section_item[SR_TREE_CITIES]);

  pl = client_player();
  if (pl == NULL) {
    set_section_text(SR_TREE_CITIES, "Cities");
    return;
  }

  city_list_iterate(pl->cities, pcity) {
    HTREEITEM hrow;
    const char *prod_name = "—";
    int turns = 0;

    if (VUT_UTYPE == pcity->production.kind) {
      prod_name = utype_name_translation(pcity->production.value.utype);
    } else if (VUT_IMPROVEMENT == pcity->production.kind) {
      prod_name =
          improvement_name_translation(pcity->production.value.building);
    }
    turns = city_production_turns_to_build(pcity, TRUE);

    hrow = add_childf(g_section_item[SR_TREE_CITIES], (LPARAM)pcity->id,
                      "%s — size %d — building %s (%d turns)",
                      city_name_get(pcity), city_size_get(pcity),
                      prod_name, turns);
    if (hrow != NULL) {
      add_childf(hrow, 0,
                 "Food %+d, Shields %+d, Trade %d",
                 pcity->surplus[O_FOOD],
                 pcity->surplus[O_SHIELD],
                 pcity->surplus[O_TRADE]);
      add_childf(hrow, 0,
                 "Happy %d, Content %d, Unhappy %d, Angry %d",
                 pcity->feel[CITIZEN_HAPPY][FEELING_FINAL],
                 pcity->feel[CITIZEN_CONTENT][FEELING_FINAL],
                 pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL],
                 pcity->feel[CITIZEN_ANGRY][FEELING_FINAL]);
    }
    count++;
  } city_list_iterate_end;

  if (count == 0) {
    add_child(g_section_item[SR_TREE_CITIES], "No cities", 0);
  }
  fc_snprintf(title_buf, sizeof(title_buf), "Cities (%d)", count);
  set_section_text(SR_TREE_CITIES, title_buf);
}

void sr_tree_rebuild_review(const struct tile *ptile)
{
  if (!g_sections_ready) { return; }
  clear_children(g_section_item[SR_TREE_REVIEW]);

  if (ptile == NULL) {
    set_section_text(SR_TREE_REVIEW, "Review cursor  (off — press L)");
    return;
  }

  {
    int tx, ty;
    enum known_type known = client_tile_get_known(ptile);

    index_to_map_pos(&tx, &ty, tile_index(ptile));
    add_childf(g_section_item[SR_TREE_REVIEW], 0,
               "Position: %d, %d (continent %d)",
               tx, ty, tile_continent(ptile));

    if (known == TILE_UNKNOWN) {
      add_child(g_section_item[SR_TREE_REVIEW], "Unknown tile", 0);
    } else {
      add_childf(g_section_item[SR_TREE_REVIEW], 0,
                 "Terrain: %s",
                 tile_get_info_text(ptile, TRUE, 0));
      {
        const char *yield = get_tile_output_text((struct tile *)ptile);
        if (yield != NULL && *yield != '\0') {
          add_childf(g_section_item[SR_TREE_REVIEW], 0, "Yield: %s", yield);
        }
      }
      {
        const char *infra = get_infrastructure_text(ptile->extras);
        if (infra != NULL && *infra != '\0') {
          add_childf(g_section_item[SR_TREE_REVIEW], 0,
                     "Infrastructure: %s", infra);
        }
      }
      if (tile_owner(ptile) != NULL) {
        add_childf(g_section_item[SR_TREE_REVIEW], 0,
                   "Territory of %s",
                   nation_adjective_for_player(tile_owner(ptile)));
      }
      if (tile_city(ptile) != NULL) {
        add_childf(g_section_item[SR_TREE_REVIEW], 0,
                   "City: %s (size %d)",
                   city_name_get(tile_city(ptile)),
                   city_size_get(tile_city(ptile)));
      }
      if (unit_list_size(ptile->units) > 0) {
        add_childf(g_section_item[SR_TREE_REVIEW], 0,
                   "Units on tile: %d", unit_list_size(ptile->units));
      }
      if (known == TILE_KNOWN_UNSEEN) {
        add_child(g_section_item[SR_TREE_REVIEW], "Currently fogged", 0);
      }
    }
  }

  set_section_text(SR_TREE_REVIEW, "Review cursor");
}

void sr_tree_rebuild_nations(void)
{
  char title_buf[64];
  int count = 0;
  struct player *me = client_player();

  if (!g_sections_ready) { return; }
  clear_children(g_section_item[SR_TREE_NATIONS]);

  players_iterate_alive(pplayer) {
    const char *rel = "unknown";

    if (pplayer == me) {
      /* Skip self; we're in the Game section. */
      continue;
    }
    if (me != NULL) {
      struct player_diplstate *ds = player_diplstate_get(me, pplayer);

      if (ds != NULL) {
        switch (ds->type) {
          case DS_ARMISTICE:  rel = "armistice";  break;
          case DS_WAR:        rel = "at war";     break;
          case DS_CEASEFIRE:  rel = "ceasefire";  break;
          case DS_PEACE:      rel = "peace";      break;
          case DS_ALLIANCE:   rel = "alliance";   break;
          case DS_NO_CONTACT: rel = "no contact"; break;
          case DS_TEAM:       rel = "team";       break;
          default:            rel = "unknown";    break;
        }
      }
    }
    add_childf(g_section_item[SR_TREE_NATIONS], (LPARAM)player_number(pplayer),
               "%s — %s", nation_adjective_for_player(pplayer), rel);
    count++;
  } players_iterate_alive_end;

  if (count == 0) {
    add_child(g_section_item[SR_TREE_NATIONS], "No other nations known", 0);
  }
  fc_snprintf(title_buf, sizeof(title_buf), "Known nations (%d)", count);
  set_section_text(SR_TREE_NATIONS, title_buf);
}

void sr_tree_rebuild_research(void)
{
  struct player *pl = client_player();
  struct research *r;

  if (!g_sections_ready) { return; }
  clear_children(g_section_item[SR_TREE_RESEARCH]);

  if (pl == NULL) {
    set_section_text(SR_TREE_RESEARCH, "Research");
    return;
  }
  r = research_get(pl);
  if (r == NULL) {
    set_section_text(SR_TREE_RESEARCH, "Research");
    return;
  }

  {
    double pct = 0.0;
    const char *target = get_science_target_text(&pct);

    add_childf(g_section_item[SR_TREE_RESEARCH], 0,
               "Researching: %s", target != NULL ? target : "—");
  }
  if (r->tech_goal != A_UNSET && r->tech_goal != A_UNKNOWN) {
    add_childf(g_section_item[SR_TREE_RESEARCH], 0,
               "Goal: %s", get_science_goal_text(r->tech_goal));
  }
  add_childf(g_section_item[SR_TREE_RESEARCH], 0,
             "Bulbs: %d / %d",
             r->bulbs_researched,
             research_total_bulbs_required(r, r->researching, FALSE));
  set_section_text(SR_TREE_RESEARCH, "Research");
}

void sr_tree_rebuild_pregame(void)
{
  if (!g_sections_ready) { return; }
  clear_children(g_section_item[SR_TREE_PREGAME]);

  if (client_state() != C_S_PREPARING) {
    /* Hide the section visually by blanking its label; the tree item
       still exists but has no children. */
    set_section_text(SR_TREE_PREGAME, "Pregame  (inactive)");
    return;
  }

  add_childf(g_section_item[SR_TREE_PREGAME], 0,
             "Ruleset: %s", game.control.name[0] != '\0' ? game.control.name : "?");
  add_childf(g_section_item[SR_TREE_PREGAME], 0,
             "Players: %d", player_count());
  set_section_text(SR_TREE_PREGAME, "Pregame");
}

void sr_tree_rebuild_all(void)
{
  sr_tree_rebuild_game();
  sr_tree_rebuild_units();
  sr_tree_rebuild_cities();
  sr_tree_rebuild_nations();
  sr_tree_rebuild_research();
  sr_tree_rebuild_pregame();
  /* Review is driven by sr_cmd.c's look-mode state — no rebuild here. */
}

#else  /* !_WIN32 */

void sr_tree_create_sections(void) {}
void sr_tree_rebuild_game(void) {}
void sr_tree_rebuild_units(void) {}
void sr_tree_rebuild_cities(void) {}
void sr_tree_rebuild_review(const struct tile *ptile) { (void)ptile; }
void sr_tree_rebuild_nations(void) {}
void sr_tree_rebuild_research(void) {}
void sr_tree_rebuild_pregame(void) {}
void sr_tree_rebuild_all(void) {}
void sr_tree_focus_section(enum sr_tree_section s) { (void)s; }

#endif
