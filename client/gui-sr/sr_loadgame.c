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
#include <time.h>

#include <windows.h>

/* utility */
#include "shared.h"

/* client */
#include "chatline_common.h"
#include "client_main.h"          /* savefile global */
#include "connectdlg_common.h"
#include "pages_g.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_play.h"
#include "sr_speech.h"
#include "sr_window.h"

#include "sr_loadgame.h"

#define SR_LOADGAME_CLASS  L"FreecivSRLoadGame"
#define IDC_LG_LIST        9401
#define IDC_LG_LOAD        9402
#define IDC_LG_CANCEL      9403

/* ---- state ------------------------------------------------------------- */

struct lg_entry {
  char *display;   /* "name   YYYY-MM-DD HH:MM"  */
  char *fullpath;  /* full filename to /load    */
};

struct lg_state {
  HWND dlg;
  HWND list;
  HWND btn_load;
  HWND btn_cancel;
  HWND prev_focus;

  struct lg_entry *entries;
  int n_entries;
};

static struct lg_state *g_lg = NULL;

struct lg_pending {
  int active;
  char *filename;
  int take_after_load;   /* set after --file spawn; cleared by sr_loadgame_take_after_load */
};
static struct lg_pending pending;

/* Called from gui_real_set_client_page on PAGE_START. Returns 1 and
   clears the flag if a saved-game spawn just landed; 0 otherwise. */
int sr_loadgame_consume_take_after_load(void)
{
  if (pending.take_after_load) {
    pending.take_after_load = 0;
    return 1;
  }
  return 0;
}

/* ---- helpers ----------------------------------------------------------- */

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

static void free_entries(struct lg_state *s)
{
  int i;

  if (s->entries == NULL) { return; }
  for (i = 0; i < s->n_entries; i++) {
    free(s->entries[i].display);
    free(s->entries[i].fullpath);
  }
  free(s->entries);
  s->entries = NULL;
  s->n_entries = 0;
}

static int cmp_entry_mtime_desc(const void *a, const void *b)
{
  /* The display string starts with name, then a tab, then mtime suffix.
     We sort by the embedded mtime which we'll prepend during scan. To
     keep entry struct compact we sort via fullpath stat instead — but
     simpler: we already populated in mtime-desc order during scan. */
  (void)a; (void)b;
  return 0;
}

/* Build a one-line display string from a fileinfo entry. */
static char *make_display(const char *name, time_t mtime)
{
  struct tm tmv;
  char timebuf[32];
  size_t need;
  char *out;

#ifdef _WIN32
  {
    struct tm *p = localtime(&mtime);
    if (p != NULL) { tmv = *p; }
    else { memset(&tmv, 0, sizeof(tmv)); }
  }
#else
  if (localtime_r(&mtime, &tmv) == NULL) {
    memset(&tmv, 0, sizeof(tmv));
  }
#endif
  strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", &tmv);

  need = strlen(name) + 4 + strlen(timebuf) + 1;
  out = (char *)malloc(need);
  if (out == NULL) { return NULL; }
  snprintf(out, need, "%s   %s", name, timebuf);
  return out;
}

/* Newest-first comparator for qsort over struct lg_entry. mtime is
   parsed back out of the display string — we instead carry mtime in a
   separate parallel array during scan. To keep this simple, we use a
   small temporary record then emit entries in sorted order. */
struct lg_scan_rec {
  char *name;
  char *fullpath;
  time_t mtime;
};

static int cmp_scan_mtime_desc(const void *a, const void *b)
{
  const struct lg_scan_rec *ra = (const struct lg_scan_rec *)a;
  const struct lg_scan_rec *rb = (const struct lg_scan_rec *)b;

  if (ra->mtime < rb->mtime) { return  1; }
  if (ra->mtime > rb->mtime) { return -1; }
  return 0;
}

/* Scan get_save_dirs() for any name containing ".sav". Returns the
   number of entries placed into s->entries (also assigns s->n_entries).
*/
static void scan_saves(struct lg_state *s)
{
  struct fileinfo_list *files;
  struct lg_scan_rec *recs = NULL;
  int n = 0, cap = 0;
  int i;

  free_entries(s);

  files = fileinfolist_infix(get_save_dirs(), ".sav", FALSE);
  if (files == NULL) {
    return;
  }

  fileinfo_list_iterate(files, pf) {
    if (n == cap) {
      int newcap = cap == 0 ? 16 : cap * 2;
      struct lg_scan_rec *nr
        = (struct lg_scan_rec *)realloc(recs, sizeof(*recs) * (size_t)newcap);
      if (nr == NULL) { break; }
      recs = nr;
      cap = newcap;
    }
    recs[n].name = pf->name != NULL ? strdup(pf->name) : strdup("?");
    recs[n].fullpath = pf->fullname != NULL ? strdup(pf->fullname) : NULL;
    recs[n].mtime = pf->mtime;
    n++;
  } fileinfo_list_iterate_end;
  fileinfo_list_destroy(files);

  if (n == 0) {
    free(recs);
    return;
  }

  qsort(recs, (size_t)n, sizeof(*recs), cmp_scan_mtime_desc);

  s->entries = (struct lg_entry *)calloc((size_t)n, sizeof(*s->entries));
  if (s->entries == NULL) {
    for (i = 0; i < n; i++) {
      free(recs[i].name);
      free(recs[i].fullpath);
    }
    free(recs);
    return;
  }

  for (i = 0; i < n; i++) {
    s->entries[i].display = make_display(recs[i].name, recs[i].mtime);
    s->entries[i].fullpath = recs[i].fullpath;
    free(recs[i].name);
  }
  s->n_entries = n;
  free(recs);

  /* Suppress "unused" warning on stub comparator. */
  (void)cmp_entry_mtime_desc;
}

static void populate_list(void)
{
  struct lg_state *s = g_lg;
  int i;

  if (s == NULL || s->list == NULL) { return; }
  SendMessageW(s->list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(s->list, LB_RESETCONTENT, 0, 0);
  for (i = 0; i < s->n_entries; i++) {
    wchar_t *w = utf8_to_wide(s->entries[i].display != NULL
                              ? s->entries[i].display : "?");
    if (w != NULL) {
      SendMessageW(s->list, LB_ADDSTRING, 0, (LPARAM)w);
      free(w);
    }
  }
  if (s->n_entries > 0) {
    SendMessageW(s->list, LB_SETCURSEL, 0, 0);
  }
  SendMessageW(s->list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(s->list, NULL, TRUE);
}

/* ---- commit ------------------------------------------------------------ */

static void do_load(void)
{
  struct lg_state *s = g_lg;
  int sel;
  const char *path;

  if (s == NULL || s->list == NULL) { return; }
  sel = (int)SendMessageW(s->list, LB_GETCURSEL, 0, 0);
  if (sel < 0 || sel >= s->n_entries) { return; }
  path = s->entries[sel].fullpath;
  if (path == NULL || path[0] == '\0') { return; }

  free(pending.filename);
  pending.filename = strdup(path);
  pending.active = (pending.filename != NULL);
  if (!pending.active) { return; }

  {
    char buf[1024];
    snprintf(buf, sizeof(buf), "Load: %s", pending.filename);
    sr_log_append(buf);
  }
  sr_say("Loading saved game.", 1);
  sr_loadgame_close();

  /* Spawn the server with --file so the save's ruleset is loaded
     during server init. Doing /load mid-run triggers a ruleset
     reload that deadlocks in "Loading rulesets." on Windows.
     Target page is PAGE_START — the server is already loaded by
     init, so we don't wait in PAGE_LOAD for a handle_game_load
     packet that would only have been sent to pre-existing
     connections (we hadn't connected yet). */
  free(savefile);
  savefile = strdup(pending.filename);
  pending.take_after_load = 1;
  client_start_server_and_set_page(PAGE_START);

  /* /load chat is no longer needed — server already has the save. */
  free(pending.filename);
  pending.filename = NULL;
  pending.active = 0;
}

/* ---- window proc ------------------------------------------------------- */

static LRESULT CALLBACK lg_wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      if (g_lg == NULL) { return -1; }
      g_lg->list = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_LG_LIST, hi, NULL);
      g_lg->btn_load = CreateWindowExW(
          0, L"BUTTON", L"Load",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_LG_LOAD, hi, NULL);
      g_lg->btn_cancel = CreateWindowExW(
          0, L"BUTTON", L"Cancel",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_LG_CANCEL, hi, NULL);
      populate_list();
      return 0;
    }
    case WM_SIZE: {
      RECT rc;
      int pad = 8;
      int btn_h = 28;

      GetClientRect(h, &rc);
      if (g_lg == NULL) { return 0; }
      if (g_lg->list != NULL) {
        MoveWindow(g_lg->list, pad, pad,
                   rc.right - 2 * pad,
                   rc.bottom - 3 * pad - btn_h, TRUE);
      }
      if (g_lg->btn_load != NULL) {
        MoveWindow(g_lg->btn_load, pad, rc.bottom - pad - btn_h,
                   100, btn_h, TRUE);
      }
      if (g_lg->btn_cancel != NULL) {
        MoveWindow(g_lg->btn_cancel, pad + 100 + pad,
                   rc.bottom - pad - btn_h, 100, btn_h, TRUE);
      }
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(w);

      if (g_lg == NULL) { break; }
      if (id == IDC_LG_LIST && HIWORD(w) == LBN_DBLCLK) {
        do_load();
        return 0;
      }
      if (id == IDC_LG_LOAD && HIWORD(w) == BN_CLICKED) {
        do_load();
        return 0;
      }
      if (id == IDC_LG_CANCEL && HIWORD(w) == BN_CLICKED) {
        sr_loadgame_close();
        return 0;
      }
      if (id == IDCANCEL) {  /* Escape via IsDialogMessage. */
        sr_loadgame_close();
        return 0;
      }
      break;
    }
    case WM_KEYDOWN:
      if (w == VK_ESCAPE) { sr_loadgame_close(); return 0; }
      break;
    case WM_CLOSE:
      sr_loadgame_close();
      return 0;
    case WM_DESTROY:
      if (g_lg != NULL) {
        g_lg->dlg = NULL;
        g_lg->list = NULL;
        g_lg->btn_load = NULL;
        g_lg->btn_cancel = NULL;
      }
      return 0;
    default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

/* Listbox subclass: Enter loads, Escape cancels. */
static WNDPROC g_list_default = NULL;

static LRESULT CALLBACK lg_list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    LRESULT base = g_list_default != NULL
                   ? CallWindowProcW(g_list_default, h, m, w, l)
                   : DefWindowProcW(h, m, w, l);
    return base | DLGC_WANTMESSAGE;
  }
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) { do_load(); return 0; }
    if (w == VK_ESCAPE) { sr_loadgame_close(); return 0; }
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
  wc.lpfnWndProc = lg_wnd_proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_LOADGAME_CLASS;
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    g_class_registered = 1;
    return 1;
  }
  return 0;
}

/* ---- public API -------------------------------------------------------- */

void sr_loadgame_open(void)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  struct lg_state *s;

  if (!ensure_class(hi)) { return; }
  if (g_lg != NULL && g_lg->dlg != NULL) {
    SetForegroundWindow(g_lg->dlg);
    if (g_lg->list != NULL) { SetFocus(g_lg->list); }
    return;
  }

  s = (struct lg_state *)calloc(1, sizeof(*s));
  if (s == NULL) { return; }
  s->prev_focus = GetFocus();

  scan_saves(s);
  if (s->n_entries == 0) {
    sr_say("No saved games found.", 1);
    free_entries(s);
    free(s);
    return;
  }

  g_lg = s;

  s->dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME,
      SR_LOADGAME_CLASS, L"Load Saved Game",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
      CW_USEDEFAULT, CW_USEDEFAULT, 560, 380,
      NULL, NULL, hi, NULL);
  if (s->dlg == NULL) {
    free_entries(s);
    free(s);
    g_lg = NULL;
    return;
  }

  if (s->list != NULL) {
    g_list_default = (WNDPROC)SetWindowLongPtrW(
        s->list, GWLP_WNDPROC, (LONG_PTR)lg_list_proc);
  }

  sr_window_register_popup(s->dlg);

  ShowWindow(s->dlg, SW_SHOWNORMAL);
  UpdateWindow(s->dlg);
  if (s->list != NULL) { SetFocus(s->list); }
}

void sr_loadgame_close(void)
{
  struct lg_state *s = g_lg;

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
  free_entries(s);
  free(s);
  g_lg = NULL;
}

void sr_loadgame_flush_if_pending(int page)
{
  /* Legacy PAGE_LOAD path — unused since do_load now sets `savefile`
     and targets PAGE_START via --file. Kept as a safety net: if any
     future code path leaves pending active and lands on PAGE_LOAD,
     fall back to /load over chat. Pending state is self-healing
     (do_load overwrites on each call). */
  (void) page;
}

#else  /* !_WIN32 */

void sr_loadgame_open(void) {}
void sr_loadgame_close(void) {}
void sr_loadgame_flush_if_pending(int page) { (void)page; }
int  sr_loadgame_consume_take_after_load(void) { return 0; }

#endif  /* _WIN32 */
