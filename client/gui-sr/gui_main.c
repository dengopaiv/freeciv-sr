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
#include <string.h>

#ifdef FREECIV_MSWINDOWS
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

/* utility */
#include "fc_cmdline.h"
#include "fciconv.h"
#include "fcintl.h"
#include "log.h"
#include "netintf.h"

/* gui main header */
#include "gui_sr.h"

/* client */
#include "audio.h"
#include "client_main.h"
#include "clinet.h"
#include "control.h"
#include "editgui_g.h"
#include "editor.h"
#include "gui_cbsetter.h"
#include "options.h"
#include "text.h"

/* gui-sr */
#include "sr_audio.h"
#include "sr_citydlg.h"
#include "sr_cmd.h"
#include "sr_log.h"
#include "sr_speech.h"
#include "sr_window.h"

#include "gui_main.h"

/* Network socket handed to us by the client core, or -1 if disconnected. */
static int net_sock = -1;

/* Simple idle-callback queue. Size picked well above any realistic burst. */
#define SR_IDLE_QUEUE_MAX 64
struct sr_idle_cb {
  void (*fn)(void *data);
  void *data;
};
static struct sr_idle_cb sr_idle_queue[SR_IDLE_QUEUE_MAX];
static int sr_idle_head = 0;
static int sr_idle_tail = 0;

void sr_drain_idle(void)
{
  while (sr_idle_head != sr_idle_tail) {
    struct sr_idle_cb cb = sr_idle_queue[sr_idle_head];

    sr_idle_head = (sr_idle_head + 1) % SR_IDLE_QUEUE_MAX;
    if (cb.fn != NULL) {
      cb.fn(cb.data);
    }
  }
}

const char *client_string = "gui-sr";

const char * const gui_character_encoding = "UTF-8";
const bool gui_use_transliteration = FALSE;

/**********************************************************************//**
  Do any necessary pre-initialization of the UI, if necessary.
**************************************************************************/
void gui_ui_init(void)
{
  sr_speech_init();
  /* Bring the window up before any sr_say: once the log EDIT is
     attached, subsequent sr_say calls become visible text the user
     (and NVDA) can navigate. */
  if (!sr_window_create()) {
    fc_fprintf(stderr, "[sr] failed to create the main window.\n");
    exit(EXIT_FAILURE);
  }
  sr_cmd_init();

  /* default_tileset_select() would normally call editor_init() at the
     end; we skip that path (postpone_tileset=TRUE) so the editor state
     never gets allocated and editor_ruleset_changed() crashes during
     the first ruleset sync. Call it here instead. */
  editor_init();

  /* T3/T5: every audio_play_sound() now passes through our hook first
     so we can speak captions and enforce mute prefixes. */
  audio_set_pre_play_cb(sr_audio_say_for_tag);

  sr_say("Freeciv screen reader client ready. "
         "Type start to begin a single-player game, or help for commands.", 1);
}

/**********************************************************************//**
  Entry point for whole freeciv client program.
**************************************************************************/
#ifdef _WIN32
#include <windows.h>

static LONG WINAPI sr_unhandled_exception(EXCEPTION_POINTERS *ep)
{
  PVOID addr = ep->ExceptionRecord->ExceptionAddress;
  HMODULE hMod = NULL;
  char modPath[MAX_PATH] = {0};
  unsigned long long rva = 0;

  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                         | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)addr, &hMod) && hMod != NULL) {
    GetModuleFileNameA(hMod, modPath, sizeof(modPath));
    rva = (unsigned long long)((char *)addr - (char *)hMod);
  }
  fprintf(stderr,
          "[sr-trace] UNHANDLED EXCEPTION code=0x%08lx at %p\n"
          "[sr-trace]   module=%s base=%p rva=0x%llx\n",
          (unsigned long)ep->ExceptionRecord->ExceptionCode,
          addr,
          modPath[0] ? modPath : "(unknown)",
          (void *)hMod, rva);
  fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}

static BOOL WINAPI sr_console_ctrl(DWORD ctrl)
{
  const char *name;

  switch (ctrl) {
    case CTRL_C_EVENT:        name = "CTRL_C";        break;
    case CTRL_BREAK_EVENT:    name = "CTRL_BREAK";    break;
    case CTRL_CLOSE_EVENT:    name = "CTRL_CLOSE";    break;
    case CTRL_LOGOFF_EVENT:   name = "CTRL_LOGOFF";   break;
    case CTRL_SHUTDOWN_EVENT: name = "CTRL_SHUTDOWN"; break;
    default:                  name = "OTHER";         break;
  }
  fprintf(stderr, "[sr-trace] console control event: %s\n", name);
  fflush(stderr);
  /* Returning FALSE lets the default handler terminate us. We return
     TRUE only for CTRL_CLOSE so the trace prints first; the process
     will still die shortly after. */
  return (ctrl == CTRL_CLOSE_EVENT);
}
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
  SetUnhandledExceptionFilter(sr_unhandled_exception);
  SetConsoleCtrlHandler(sr_console_ctrl, TRUE);
#endif
  fprintf(stderr, "[sr-trace] main: entering client_main\n");
  fflush(stderr);
  setup_gui_funcs();
  /* Skip default_tileset_select() entirely — gui-sr does no rendering, so
     there is no tileset to load. gui-qt uses the same TRUE form and loads
     later; we never load. Packet handlers that touch the tileset global are
     guarded for NULL. */
  {
    int rc = client_main(argc, argv, TRUE);

    fprintf(stderr, "[sr-trace] main: client_main returned %d\n", rc);
    fflush(stderr);
    return rc;
  }
}

/**********************************************************************//**
  Print extra usage information, including one line help on each option,
  to stderr.
**************************************************************************/
static void print_usage(const char *argv0)
{
  fc_fprintf(stderr,
             _("  --sr-speak-events BOOL   Speak a caption on every sound event (default on).\n"
               "  --sr-mute-prefixes LIST  Comma-separated sound-tag prefixes to mute, e.g. m_,e_chat_msg.\n"
               "  --sr-sound-volume N      Sound effects volume 0..100.\n"
               "  --no-sr-speak-events     Disable spoken captions (same as --sr-speak-events off).\n"
               "\nStandard sound flags still apply:\n"
               "  -S, --Sound FILE         Use the soundset FILE (without the .soundspec suffix).\n"
               "  -m, --music FILE         Use the musicset FILE (without the .musicspec suffix).\n"
               "  -P, --Plugin PLUGIN      Use the named audio plugin (sdl or none).\n\n"));

  /* TRANS: No full stop after the URL, could cause confusion. */
  fc_fprintf(stderr, _("Report bugs at %s\n"), BUG_URL);
}

static bool parse_bool_arg(const char *s)
{
  if (s == NULL) {
    return FALSE;
  }
  return (strcmp(s, "on") == 0 || strcmp(s, "1") == 0
          || strcmp(s, "true") == 0 || strcmp(s, "yes") == 0);
}

/**********************************************************************//**
  Parse and enact any gui-specific command-line options.
**************************************************************************/
static bool parse_options(int argc, char **argv)
{
  int i = 1;
  char *option = NULL;

  while (i < argc) {
    if (is_option("--help", argv[i])) {
      print_usage(argv[0]);

      return FALSE;
    } else if ((option = get_option_malloc("--sr-speak-events",
                                           argv, &i, argc, FALSE))) {
      gui_options.sr_speak_sound_events = parse_bool_arg(option);
      free(option);
      option = NULL;
    } else if (is_option("--no-sr-speak-events", argv[i])) {
      gui_options.sr_speak_sound_events = FALSE;
    } else if ((option = get_option_malloc("--sr-mute-prefixes",
                                           argv, &i, argc, FALSE))) {
      fc_strlcpy(gui_options.sr_sound_mute_prefixes, option,
                 sizeof(gui_options.sr_sound_mute_prefixes));
      free(option);
      option = NULL;
    } else if ((option = get_option_malloc("--sr-sound-volume",
                                           argv, &i, argc, FALSE))) {
      int v = atoi(option);

      if (v < 0) { v = 0; }
      if (v > 100) { v = 100; }
      gui_options.sound_effects_volume = v;
      free(option);
      option = NULL;
    } else {
      fc_fprintf(stderr, _("Unrecognized option: \"%s\"\n"), argv[i]);
      exit(EXIT_FAILURE);
    }

    i++;
  }

  return TRUE;
}

/**********************************************************************//**
  The main loop for the UI. This is called from main(), and when it
  exits the client will exit.
**************************************************************************/
int gui_ui_main(int argc, char *argv[])
{
  int rc;

  if (!parse_options(argc, argv)) {
    return EXIT_SUCCESS;
  }
  sr_say("Entering main loop.", 0);
  rc = sr_window_run_message_loop();
  sr_drain_idle();
  return rc;
}

/**********************************************************************//**
  Extra initializers for client options.

  Called from options_init() BEFORE options_load(), so values set here
  are first-boot defaults only — a user's saved settings (written to
  ~/.freeciv/freeciv-client-rc) take precedence on subsequent runs.

  We use this hook to flip every rendering / animation / auto-follow
  option to its cheapest setting. Rationale:

  1. `freeciv-sr` has no real map canvas — mapview.c, canvas.c and
     sprite.c are stubs that discard draw calls. Even so the engine
     consults draw_* flags when computing which tiles to refresh and
     when running animation timers. Turning every overlay off removes
     that wasted work.
  2. The fewer redraws and auto-centers, the fewer incidental status
     changes that could trigger chatty notifications and clutter the
     screen reader output.
  3. A blind player does not benefit from smooth unit motion or map
     panning; those only introduce lag between action and spoken
     feedback.
**************************************************************************/
void gui_options_extra_init(void)
{
  /* --- animation: disable all frame-based motion -------------------- */
  gui_options.smooth_move_unit_msec    = 0;
  gui_options.smooth_center_slide_msec = 0;
  gui_options.smooth_combat_step_msec  = 0;

  /* --- auto-center: the map view follows nothing; sr_tile speaks
     locations directly when the user asks. Auto-center also triggers
     mapview refreshes we don't need. ----------------------------------- */
  gui_options.auto_center_on_unit      = FALSE;
  gui_options.auto_center_on_automated = FALSE;
  gui_options.auto_center_on_combat    = FALSE;
  gui_options.auto_center_each_turn    = FALSE;

  /* --- draw_* map overlays: turn every one off. These only decorate
     the (non-existent) map canvas. Gameplay-critical state (unit
     positions, city locations, visibility) is held in the game model,
     not in these flags, so disabling is safe. ------------------------- */
  gui_options.draw_city_outlines       = FALSE;
  gui_options.draw_city_output         = FALSE;
  gui_options.draw_map_grid            = FALSE;
  gui_options.draw_city_names          = FALSE;
  gui_options.draw_city_growth         = FALSE;
  gui_options.draw_city_productions    = FALSE;
  gui_options.draw_city_buycost        = FALSE;
  gui_options.draw_city_trade_routes   = FALSE;
  gui_options.draw_terrain             = FALSE;
  gui_options.draw_coastline           = FALSE;
  gui_options.draw_paths               = FALSE;
  gui_options.draw_irrigation          = FALSE;
  gui_options.draw_mines               = FALSE;
  gui_options.draw_fortress_airbase    = FALSE;
  gui_options.draw_specials            = FALSE;
  gui_options.draw_huts                = FALSE;
  gui_options.draw_pollution           = FALSE;
  gui_options.draw_cities              = FALSE;
  gui_options.draw_units               = FALSE;
  gui_options.draw_focus_unit          = FALSE;
  gui_options.draw_fog_of_war          = FALSE;
  gui_options.draw_borders             = FALSE;
  gui_options.draw_native              = FALSE;
  gui_options.draw_full_citybar        = FALSE;
  gui_options.draw_unit_shields        = FALSE;
  gui_options.draw_unit_stack_size     = FALSE;

  /* --- misc visual/cursor noise ------------------------------------- */
  gui_options.solid_color_behind_units          = FALSE;
  gui_options.enable_cursor_changes             = FALSE;
  gui_options.update_city_text_in_refresh_tile  = FALSE;

  /* --- zoom: no-op in gui-sr, but keep it pinned so external code
     paths stay at 1.0 instead of applying whatever the last gtk user
     saved. ------------------------------------------------------------- */
  gui_options.zoom_set           = FALSE;
  gui_options.zoom_default_level = 1.0f;

  /* --- reqtree picture decoration is only consulted by the gtk / qt
     research dialogs, but flipping it keeps first-boot state uniform. - */
  gui_options.reqtree_show_icons   = FALSE;
  gui_options.reqtree_curved_lines = FALSE;
}

/**********************************************************************//**
  Do any necessary UI-specific cleanup
**************************************************************************/
void gui_ui_exit(void)
{
  sr_say("Freeciv closing.", 1);
  /* Destroy SR dialog state before the engine frees ruleset data, so
     no stale pointers survive in module statics (wl_edit, etc.). */
  sr_citydlg_shutdown();
  sr_window_destroy();
  sr_speech_exit();
}

/**********************************************************************//**
  Return our GUI type
**************************************************************************/
enum gui_type gui_get_gui_type(void)
{
  return GUI_SR;
}

/**********************************************************************//**
  Update the connected users list at pregame state.
**************************************************************************/
void gui_real_conn_list_dialog_update(void *unused)
{
  /* Called whenever the pregame connection list changes: new players
     joining, taking nations, toggling ready, etc. The server also
     emits a chat-channel announcement for every such event, so
     re-speaking here would double every line. Left as a no-op; the
     lobby is narrated through gui_real_output_window_append. */
}

/**********************************************************************//**
  Make a bell noise (beep).  This provides low-level sound alerts even
  if there is no real sound support.
**************************************************************************/
void gui_sound_bell(void)
{
#ifdef _WIN32
  /* System exclamation beep — plays independently of the audio plugin so
     the turn-bell is audible even with --Plugin none. */
  MessageBeep(MB_ICONEXCLAMATION);
#endif
  /* Also speak, because a blind player may miss the raw beep (headphones
     away, notification volume low, etc.). Non-interrupting so we don't
     clobber any narration in flight. */
  sr_say(_("Alert."), 0);
}

/**********************************************************************//**
  Wait for data on the given socket.  Call input_from_server() when data
  is ready to be read.

  This function is called after the client has successfully connected
  to the server.
**************************************************************************/
void gui_add_net_input(int sock)
{
  net_sock = sock;
  sr_window_set_net_socket(sock);
}

/**********************************************************************//**
  Stop waiting for any server network data.  See add_net_input().

  This function is called if the client disconnects from the server.
**************************************************************************/
void gui_remove_net_input(void)
{
  net_sock = -1;
  sr_window_set_net_socket(-1);
}

/**********************************************************************//**
  Set one of the unit icons (specified by idx) in the information area
  based on punit.

  punit is the unit the information should be taken from. Use NULL to
  clear the icon.

  idx specified which icon should be modified. Use idx == -1 to indicate
  the icon for the active unit. Or idx in [0..num_units_below - 1] for
  secondary (inactive) units on the same tile.
**************************************************************************/
void gui_set_unit_icon(int idx, struct unit *punit)
{
  /* PORTME */
}

/**********************************************************************//**
  Most clients use an arrow (e.g., sprites.right_arrow) to indicate when
  the units_below will not fit. This function is called to activate or
  deactivate the arrow.

  Is disabled by default.
**************************************************************************/
void gui_set_unit_icons_more_arrow(bool onoff)
{
  /* PORTME */
}

/**********************************************************************//**
  Called when the set of units in focus (get_units_in_focus()) changes.
  Standard updates like update_unit_info_label() are handled in the platform-
  independent code, so some clients will not need to do anything here.
**************************************************************************/
void gui_real_focus_units_changed(void)
{
  struct unit *punit = head_of_units_in_focus();

  if (punit == NULL) {
    sr_say("No unit selected.", 1);
  } else {
    const char *desc = unit_description(punit);

    sr_say(desc != NULL ? desc : "Unit selected.", 1);
  }
}

/**********************************************************************//**
  Enqueue a callback to be called during an idle moment.  The 'callback'
  function should be called sometimes soon, and passed the 'data' pointer
  as its data.
**************************************************************************/
void gui_add_idle_callback(void (callback)(void *), void *data)
{
  int next = (sr_idle_tail + 1) % SR_IDLE_QUEUE_MAX;

  if (next == sr_idle_head) {
    /* Queue full: run synchronously rather than drop. */
    log_error("sr idle queue full; running callback inline.");
    (callback)(data);
    return;
  }
  sr_idle_queue[sr_idle_tail].fn = callback;
  sr_idle_queue[sr_idle_tail].data = data;
  sr_idle_tail = next;
}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void gui_editgui_tileset_changed(void)
{}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void gui_editgui_refresh(void)
{}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void gui_editgui_popup_properties(const struct tile_list *tiles, int objtype)
{}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void gui_editgui_popdown_all(void)
{}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void gui_editgui_notify_object_changed(int objtype, int object_id,
                                       bool removal)
{}

/**********************************************************************//**
  Stub for editor function
**************************************************************************/
void gui_editgui_notify_object_created(int tag, int id)
{}

/**********************************************************************//**
  Updates a gui font style.
**************************************************************************/
void gui_gui_update_font(const char *font_name, const char *font_value)
{
  /* PORTME */
}

/**********************************************************************//**
  Insert build information to help
**************************************************************************/
void gui_insert_client_build_info(char *outbuf, size_t outlen)
{
  /* PORTME */
}
