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

/* utility */
#include "fcintl.h"
#include "support.h"

/* common */
#include "calendar.h"
#include "game.h"

/* gui main header */
#include "gui_sr.h"

/* client */
#include "client_main.h"
#include "climisc.h"
#include "control.h"
#include "mapctrl_common.h"

/* gui-sr */
#include "sr_mapedit.h"
#include "sr_notify.h"
#include "sr_sidebar.h"
#include "sr_speech.h"
#include "sr_status.h"
#include "sr_tile.h"
#include "sr_tree.h"

#include "mapview.h"

/* Track the tile the focused unit was last announced on, so we only
   speak when it actually changes position. */
static const struct tile *last_announced_tile = NULL;

/************************************************************************//**
  Typically an info box is provided to tell the player about the state
  of their civilization.  This function is called when the label is
  changed.
****************************************************************************/
void update_info_label(void)
{
  sr_sidebar_update_game();
  sr_status_update();
}

/************************************************************************//**
  Update the information label which gives info on the current unit
  and the tile under the current unit, for specified unit.  Note that
  in practice punit is always the focus unit.

  Clears label if punit is NULL.

  Typically also updates the cursor for the map_canvas (this is
  related because the info label may includes "select destination"
  prompt etc).  And it may call update_unit_pix_label() to update the
  icons for units on this tile.
****************************************************************************/
void update_unit_info_label(struct unit_list *punitlist)
{
  struct unit *punit;

  (void)punitlist;
  sr_sidebar_update_unit();
  sr_notify_recompute_waiting();
  sr_status_update();

  /* Announce the new tile when the focused unit moves. */
  punit = head_of_units_in_focus();
  if (punit != NULL) {
    const struct tile *utile = unit_tile(punit);

    if (utile != NULL && utile != last_announced_tile) {
      last_announced_tile = utile;
      sr_tile_say(utile, TRUE);  /* terse: just terrain + extras */
    }
  } else if (last_announced_tile != NULL) {
    last_announced_tile = NULL;
  }
}

/************************************************************************//**
  Update the mouse cursor. Cursor type depends on what user is doing and
  pointing.
****************************************************************************/
void update_mouse_cursor(enum cursor_type new_cursor_type)
{
  /* PORTME */
}

/************************************************************************//**
  Update the timeout display.  The timeout is the time until the turn
  ends, in seconds.
****************************************************************************/
void gui_update_timeout_label(void)
{
  /* PORTME */

  /* set some widget based on get_timeout_label_text() */
}

/************************************************************************//**
  If do_restore is FALSE it should change the turn button style (to
  draw the user's attention to it).  If called regularly from a timer
  this will give a blinking turn done button.  If do_restore is TRUE
  this should reset the turn done button to the default style.
****************************************************************************/
void update_turn_done_button(bool do_restore)
{
  static bool flip = FALSE;

  if (!get_turn_done_button_state()) {
    return;
  }

  if ((do_restore && flip) || !do_restore) {
    /* ... */

    flip = !flip;
  }
  /* PORTME */
}

/************************************************************************//**
  Set information for the indicator icons typically shown in the main
  client window.  The parameters tell which sprite to use for the
  indicator.
****************************************************************************/
void set_indicator_icons(struct sprite *bulb, struct sprite *sol,
                         struct sprite *flake, struct sprite *gov)
{
  /* PORTME */
}

/************************************************************************//**
  Return a canvas that is the overview window.
****************************************************************************/
struct canvas *get_overview_window(void)
{
  /* PORTME */
  return NULL;
}

/************************************************************************//**
  Mark the rectangular region as "dirty" so that we know to flush it
  later.
****************************************************************************/
void dirty_rect(int canvas_x, int canvas_y,
                int pixel_width, int pixel_height)
{
  /* PORTME */
}

/************************************************************************//**
  Mark the entire screen area as "dirty" so that we can flush it later.
****************************************************************************/
void dirty_all(void)
{
  /* PORTME */
}

/************************************************************************//**
  Flush all regions that have been previously marked as dirty.  See
  dirty_rect() and dirty_all(). This function is generally called after we've
  processed a batch of drawing operations.
****************************************************************************/
void flush_dirty(void)
{
  /* PORTME */
}

/************************************************************************//**
  Do any necessary synchronization to make sure the screen is up-to-date.
  The canvas should have already been flushed to screen via flush_dirty() -
  all this function does is make sure the hardware has caught up.
****************************************************************************/
void gui_flush(void)
{
  /* PORTME */
}

/************************************************************************//**
  Update (refresh) the locations of the mapview scrollbars (if it uses
  them).
****************************************************************************/
void update_map_canvas_scrollbars(void)
{
  /* PORTME */
}

/************************************************************************//**
  Update the size of the sliders on the scrollbars.
****************************************************************************/
void update_map_canvas_scrollbars_size(void)
{
  /* PORTME */
}

/************************************************************************//**
  Update (refresh) all city descriptions on the mapview.
****************************************************************************/
void update_city_descriptions(void)
{
  update_map_canvas_visible();
}

/************************************************************************//**
  Draw a cross-hair overlay on a tile.
****************************************************************************/
void put_cross_overlay_tile(struct tile *ptile)
{
  /* PORTME */
}

/************************************************************************//**
 Area Selection
****************************************************************************/
void draw_selection_rectangle(int canvas_x, int canvas_y, int w, int h)
{
  /* PORTME */
}

/**********************************************************************//**
  Refresh map canvas size information
**************************************************************************/
void gui_map_canvas_size_refresh(void)
{
  /* Needed only with full screen zoom mode.
   * Not needed, nor implemented, in this client. */
  fc_assert(FALSE);
}

/************************************************************************//**
  This function is called when the tileset is changed.
****************************************************************************/
void tileset_changed(void)
{
  /* PORTME */
  /* Here you should do any necessary redraws (for instance, the city
   * dialogs usually need to be resized). */
}

/************************************************************************//**
  Return the dimensions of the area (container widget; maximum size) for
  the overview.
****************************************************************************/
void get_overview_area_dimensions(int *width, int *height)
{
  /* PORTME */
  *width = 0;
  *height = 0;
}

/************************************************************************//**
  Called when the map size changes. This may be used to change the
  size of the GUI element holding the overview canvas. The
  overview.width and overview.height are updated if this function is
  called.
****************************************************************************/
void overview_size_changed(void)
{
  /* PORTME */
}

/************************************************************************//**
  Sets the position of the overview scroll window based on mapview position.
****************************************************************************/
void update_overview_scroll_window_pos(int x, int y)
{
  (void)x;
  (void)y;
  /* No visual overview in the audio client. */
}

/************************************************************************//**
  New turn callback
****************************************************************************/
void gui_start_turn(void)
{
  char buf[128];

  /* PAGE_GAME's transition is queued through update_queue.c and can
     land very late; fire pending newedit setup here so oceanize +
     edit-mode-on happen on turn 1 rather than at quit time. The
     pending_game flag is a one-shot, so if PAGE_GAME's callback
     beats us to it there's no double-execution. */
  sr_mapedit_flush_at_game_if_pending();

  sr_sidebar_update_game();
  sr_sidebar_update_unit();
  sr_notify_recompute_waiting();
  sr_status_update();

  fc_snprintf(buf, sizeof(buf), "Turn %d begins.", game.info.turn);
  sr_notify_turn_digest(buf);
}
