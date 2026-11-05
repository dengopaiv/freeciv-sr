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
#include <string.h>

#include <windows.h>
#include <oleacc.h>

/* utility */
#include "fcintl.h"
#include "support.h"

/* common */
#include "unit.h"
#include "unitlist.h"
#include "player.h"

/* client */
#include "client_main.h"
#include "control.h"
#include "text.h"       /* unit_description */

/* gui-sr */
#include "sr_cmd.h"
#include "sr_speech.h"

#include "sr_play.h"

#define SR_PLAY_CLASS_NAME L"FreecivSRMapView"

static HWND g_play = NULL;

/* ====================================================================== */
/* Announcement                                                           */
/* ====================================================================== */

/* Count my units with moves remaining and idle activity. */
static int count_waiting_units(void)
{
  struct player *me = client_player();
  int n = 0;

  if (me == NULL) {
    return 0;
  }
  unit_list_iterate(me->units, punit) {
    if (punit->moves_left > 0 && punit->activity == ACTIVITY_IDLE) {
      n++;
    }
  } unit_list_iterate_end;
  return n;
}

void sr_play_announce_situation(void)
{
  enum client_states st = client_state();
  char buf[512];

  if (st == C_S_INITIAL || st == C_S_DISCONNECTED) {
    sr_say("Map view. No game. Open Game menu to start a local game.", 0);
    return;
  }
  if (st == C_S_PREPARING) {
    sr_say("Pregame lobby. Open Game menu, then Begin, to start.", 0);
    return;
  }
  if (st == C_S_OVER) {
    sr_say("Game over.", 0);
    return;
  }
  /* C_S_RUNNING */
  {
    int waiting = count_waiting_units();
    struct unit *punit;

    if (get_num_units_in_focus() > 0) {
      punit = head_of_units_in_focus();
      if (punit != NULL) {
        const char *desc = unit_description(punit);

        if (waiting > 1) {
          fc_snprintf(buf, sizeof(buf),
                      "Your turn. %s. %d units awaiting orders.",
                      desc != NULL ? desc : "Unit selected", waiting);
        } else {
          fc_snprintf(buf, sizeof(buf),
                      "Your turn. %s.",
                      desc != NULL ? desc : "Unit selected");
        }
        sr_say(buf, 0);
        return;
      }
    }
    if (waiting > 0) {
      fc_snprintf(buf, sizeof(buf),
                  "Your turn. %d units awaiting orders. Press period to select.",
                  waiting);
    } else {
      fc_snprintf(buf, sizeof(buf),
                  "Your turn. No units ready. Press Enter to end turn.");
    }
    sr_say(buf, 0);
  }
}

/* ====================================================================== */
/* Key handling                                                           */
/* ====================================================================== */

/* Given a plain direction word ("n", "ne", ...) and modifiers, return
   the command string the dispatcher should see. Shift = far-, Alt = say-.
   The returned pointer is into one of the static tables below, safe to
   return from vk_to_cmd. */
static const char *direction_cmd(const char *dir, BOOL shift, BOOL alt)
{
  /* Indexed 0..7 to match n ne e se s sw w nw in this order. */
  static const char *const PLAIN[] = {
    "n", "ne", "e", "se", "s", "sw", "w", "nw"
  };
  static const char *const FAR_[] = {
    "far-n", "far-ne", "far-e", "far-se",
    "far-s", "far-sw", "far-w", "far-nw"
  };
  static const char *const SAY_[] = {
    "say-n", "say-ne", "say-e", "say-se",
    "say-s", "say-sw", "say-w", "say-nw"
  };
  int i;

  for (i = 0; i < 8; i++) {
    if (strcmp(dir, PLAIN[i]) == 0) {
      /* Shift wins over Alt if both held. */
      return shift ? FAR_[i] : (alt ? SAY_[i] : PLAIN[i]);
    }
  }
  return dir;
}

/* Map a virtual-key code to an sr_cmd word, or return NULL. Modifiers:
   ctrl is TRUE iff Ctrl is held; shift and alt similarly.
   For direction keys, Shift → "far-<dir>" (edge-jump) and
   Alt → "say-<dir>" (speak the adjacent tile, no movement). */
static const char *vk_to_cmd(WPARAM vk, BOOL ctrl, BOOL shift, BOOL alt)
{
  /* Modifier-qualified combos win first. Nothing yet needs them here —
     Ctrl+* combos are all on the accelerator table in sr_window. */
  if (ctrl) {
    return NULL;
  }

  switch (vk) {
    /* Cardinal directions from arrow keys. */
    case VK_UP:     return direction_cmd("n",  shift, alt);
    case VK_DOWN:   return direction_cmd("s",  shift, alt);
    case VK_LEFT:   return direction_cmd("w",  shift, alt);
    case VK_RIGHT:  return direction_cmd("e",  shift, alt);

    /* 8-way numpad (NumLock ON). Classic Civ. */
    case VK_NUMPAD8: return direction_cmd("n",  shift, alt);
    case VK_NUMPAD9: return direction_cmd("ne", shift, alt);
    case VK_NUMPAD6: return direction_cmd("e",  shift, alt);
    case VK_NUMPAD3: return direction_cmd("se", shift, alt);
    case VK_NUMPAD2: return direction_cmd("s",  shift, alt);
    case VK_NUMPAD1: return direction_cmd("sw", shift, alt);
    case VK_NUMPAD4: return direction_cmd("w",  shift, alt);
    case VK_NUMPAD7: return direction_cmd("nw", shift, alt);

    /* NumLock-off variants (VK_HOME etc. only reach here because the
       accelerator table doesn't eat them). */
    case VK_HOME:   return direction_cmd("nw", shift, alt);
    case VK_PRIOR:  return direction_cmd("ne", shift, alt);
    case VK_END:    return direction_cmd("sw", shift, alt);
    case VK_NEXT:   return direction_cmd("se", shift, alt);
    case VK_CLEAR:  return NULL;   /* numpad 5 = no-op; center view via C */

    /* Unit-action single-letter keys. */
    case 'F': return "fortify";
    case 'S': return "sentry";
    case 'B': return "build";
    case 'D': return "done";      /* D is "done" in our vocabulary;
                                     disband is via menu (requires confirm) */
    case 'H': return "home";
    case 'I': return "irrigate";
    case 'M': return "mine";
    case 'R': return "road";
    case 'P': return "pillage";
    case 'E': return "explore";
    case 'C': return "center";

    /* Screen-reader extensions. */
    case 'T': return "tile";
    case 'L': return "look";
    case 'J': return "jump";
    case 'W': return "where";
    case 'U': return "unit";
    case 'V': return "viewcity";
    case 'G': return "goto";

    /* Battle groups — plain digit recalls group N, Shift+digit assigns
       the current focus to group N. Fires only with map pane focus so
       edit controls (save filename, chat) still receive plain digits
       for normal typing. */
    case '1': return shift ? "bgsave1" : "bg1";
    case '2': return shift ? "bgsave2" : "bg2";
    case '3': return shift ? "bgsave3" : "bg3";
    case '4': return shift ? "bgsave4" : "bg4";

    /* Unit cycling. */
    case VK_OEM_PERIOD: return "next";
    case VK_OEM_COMMA:  return "prev";

    /* Turn control. */
    case VK_SPACE:  return "done";
    case VK_RETURN: return "end";

    /* Escape: cancel (exits review mode if active; otherwise cancels
       current action). */
    case VK_ESCAPE: return "cancel";

    default: return NULL;
  }
}

/* ====================================================================== */
/* Window procedure                                                       */
/* ====================================================================== */

static LRESULT CALLBACK sr_play_wnd_proc(HWND hWnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam)
{
  switch (msg) {

    case WM_GETDLGCODE:
      /* Tell IsDialogMessageW that we handle all keyboard input ourselves.
         Without this, arrow keys become dialog navigation and letter keys
         may be eaten as mnemonics. */
      return DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTALLKEYS;

    case WM_GETOBJECT:
      /* Tell NVDA / UI Automation that this is an interactive pane so it
         stays in focus mode and passes keys through to us. */
      if ((DWORD)lParam == OBJID_CLIENT) {
        return DefWindowProcW(hWnd, msg, wParam, lParam);
      }
      break;

    case WM_SETFOCUS:
      sr_play_announce_situation();
      return 0;

    case WM_KILLFOCUS:
      return 0;

    case WM_SYSKEYDOWN:   /* Alt+key arrives here, not in WM_KEYDOWN. */
    case WM_KEYDOWN: {
      BOOL ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      BOOL shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
      BOOL alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;
      const char *cmd;


      /* Enter means "commit goto" when goto mode is armed; otherwise
         it ends the turn via the usual dispatcher. */
      if (wParam == VK_RETURN && !ctrl && sr_cmd_is_goto_active()) {
        sr_cmd_dispatch_line("goto-commit");
        return 0;
      }

      cmd = vk_to_cmd(wParam, ctrl, shift, alt);
      if (cmd != NULL) {
        sr_cmd_dispatch_line(cmd);
        return 0;
      }
      /* Let DefWindowProc handle unhandled WM_SYSKEYDOWN (e.g. Alt
         alone opens menu) — falling through to break does that. */
      break;
    }

    case WM_CHAR:
      /* We eat keys via WM_KEYDOWN. Silence the beep. */
      return 0;

    case WM_ERASEBKGND:
      /* Nothing to draw. Tell Windows the background is done. */
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      RECT rc;

      GetClientRect(hWnd, &rc);
      /* A blank surface with a thin label so sighted observers know
         where keyboard focus currently sits. NVDA reads the window's
         accessible name (set via WM_GETTEXT / SetWindowText); the drawn
         text is purely a visual hint. */
      FillRect(hdc, &rc, (HBRUSH)(COLOR_APPWORKSPACE + 1));
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, RGB(220, 220, 220));
      DrawTextW(hdc, L"Map View  —  arrow keys move, T = tile, L = look",
                -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      EndPaint(hWnd, &ps);
      return 0;
    }

    default:
      break;
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* ====================================================================== */
/* Public API                                                             */
/* ====================================================================== */

int sr_play_register_class(HINSTANCE hi)
{
  WNDCLASSEXW wc;

  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize        = sizeof(wc);
  wc.style         = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc   = sr_play_wnd_proc;
  wc.hInstance     = hi;
  wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;                 /* we handle WM_ERASEBKGND   */
  wc.lpszClassName = SR_PLAY_CLASS_NAME;

  return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND sr_play_create(HWND parent)
{
  g_play = CreateWindowExW(
      WS_EX_CLIENTEDGE,
      SR_PLAY_CLASS_NAME,
      L"Map View",                         /* accessible name NVDA reads */
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      0, 0, 0, 0,
      parent, NULL, GetModuleHandleW(NULL), NULL);
  return g_play;
}

HWND sr_play_hwnd(void)
{
  return g_play;
}

#endif  /* _WIN32 */
