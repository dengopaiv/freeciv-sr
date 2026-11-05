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

#include "sr_pick.h"
#include "sr_window.h"

/* ====================================================================== */
/* Shared helpers                                                         */
/* ====================================================================== */

/* True if any picker has an active window. Defined at the bottom of
   the file after all three g_*_ctx globals are in scope. Used by the
   *_close paths after the commit callback: if the callback opened a
   new picker (cascade — e.g. nation -> leader), that new picker
   already owns keyboard focus, and restoring prev_focus here would
   rip it away, leaving the visible-but-dead dialog the user saw on
   the leader picker. */
static int sr_pick_any_active(void);

static wchar_t *to_wide(const char *utf8)
{
  int wlen;
  wchar_t *w;

  if (utf8 == NULL) { return NULL; }
  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (wlen <= 0) { return NULL; }
  w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
  if (w == NULL) { return NULL; }
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
  return w;
}

/* ====================================================================== */
/* Int picker                                                             */
/* ====================================================================== */

#define SR_PICK_INT_CLASS L"FreecivSRPickInt"
#define IDC_PICKINT_EDIT  9101

struct int_ctx {
  HWND dlg;
  HWND edit;
  HWND prev_focus;
  int min;
  int max;
  sr_pick_int_cb cb;
  void *userdata;
  WNDPROC edit_default;
};

static struct int_ctx *g_int_ctx = NULL;

static void int_close(BOOL commit)
{
  struct int_ctx *c = g_int_ctx;
  wchar_t wbuf[32];
  int n = 0;

  if (c == NULL) { return; }

  /* Read the EDIT contents BEFORE detaching g_int_ctx. The subclass
     int_edit_proc only forwards WM_GETTEXT to the original EDIT proc
     while g_int_ctx is set — after detach it falls to DefWindowProc,
     which has no EDIT state and returns 0. */
  if (commit && c->edit != NULL) {
    n = GetWindowTextW(c->edit, wbuf, 31);
    wbuf[n > 0 ? (n < 31 ? n : 31) : 0] = L'\0';
  }

  g_int_ctx = NULL;  /* Detach before callback — re-entrancy guard */

  if (commit && c->cb != NULL && n > 0) {
    int val = _wtoi(wbuf);

    if (c->min != c->max) {
      if (val < c->min) { val = c->min; }
      if (val > c->max) { val = c->max; }
    }
    c->cb(val, c->userdata);
  }
  if (c->dlg != NULL) { DestroyWindow(c->dlg); }
  /* Skip focus restore if the callback opened a new picker — it has
     already taken focus and we must not steal it back. */
  if (!sr_pick_any_active()
      && c->prev_focus != NULL && IsWindow(c->prev_focus)) {
    SetFocus(c->prev_focus);
  }
  free(c);
}

static LRESULT CALLBACK int_edit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  /* Claim Enter/Escape so IsDialogMessageW in the main message loop
     does not route them as default-button presses. */
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_HASSETSEL
           | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) { int_close(TRUE);  return 0; }
    if (w == VK_ESCAPE) { int_close(FALSE); return 0; }
  }
  if (g_int_ctx != NULL) {
    return CallWindowProcW(g_int_ctx->edit_default, h, m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK int_wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      if (g_int_ctx == NULL) { return -1; }
      g_int_ctx->edit = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_PICKINT_EDIT, hi, NULL);
      if (g_int_ctx->edit != NULL) {
        g_int_ctx->edit_default = (WNDPROC)SetWindowLongPtrW(
            g_int_ctx->edit, GWLP_WNDPROC, (LONG_PTR)int_edit_proc);
      }
      return 0;
    }
    case WM_SIZE: {
      RECT rc; int pad = 8;
      GetClientRect(h, &rc);
      if (g_int_ctx != NULL && g_int_ctx->edit != NULL) {
        MoveWindow(g_int_ctx->edit, pad, pad,
                   rc.right - 2 * pad, 24, TRUE);
      }
      return 0;
    }
    case WM_CLOSE:   int_close(FALSE); return 0;
    case WM_DESTROY:
      /* Only clear the context if THIS window is the one that owns it.
         When a commit-callback cascades to another picker, the fresh
         picker has already taken g_int_ctx before our old window's
         WM_DESTROY arrives; blindly clearing then would corrupt the
         new picker (dlg/edit go NULL) and the next commit would read
         nothing. */
      sr_window_unregister_popup(h);
      if (g_int_ctx != NULL && g_int_ctx->dlg == h) {
        g_int_ctx->dlg = NULL;
        g_int_ctx->edit = NULL;
      }
      return 0;
    default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

static int int_class_registered = 0;
static int ensure_int_class(HINSTANCE hi)
{
  WNDCLASSEXW wc;
  if (int_class_registered) { return 1; }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = int_wnd_proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_PICK_INT_CLASS;
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    int_class_registered = 1;
    return 1;
  }
  return 0;
}

void sr_pick_int(const char *title, int current, int minv, int maxv,
                 sr_pick_int_cb on_commit, void *userdata)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  wchar_t *wtitle;
  struct int_ctx *c;

  if (!ensure_int_class(hi)) { return; }
  if (g_int_ctx != NULL) { int_close(FALSE); }

  c = (struct int_ctx *)calloc(1, sizeof(*c));
  if (c == NULL) { return; }
  c->prev_focus = GetFocus();
  c->min = minv;
  c->max = maxv;
  c->cb = on_commit;
  c->userdata = userdata;
  g_int_ctx = c;

  wtitle = to_wide(title != NULL ? title : "Value");
  c->dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
      SR_PICK_INT_CLASS, wtitle != NULL ? wtitle : L"Value",
      WS_POPUP | WS_CAPTION | WS_SYSMENU,
      CW_USEDEFAULT, CW_USEDEFAULT, 280, 92,
      NULL, NULL, hi, NULL);
  if (wtitle != NULL) { free(wtitle); }
  if (c->dlg == NULL) {
    free(c); return;
  }
  sr_window_register_popup(c->dlg);
  if (c->edit != NULL) {
    wchar_t cur[32];
    _snwprintf(cur, 31, L"%d", current);
    cur[31] = 0;
    SetWindowTextW(c->edit, cur);
    SendMessageW(c->edit, EM_SETSEL, 0, -1);
  }
  ShowWindow(c->dlg, SW_SHOWNORMAL);
  UpdateWindow(c->dlg);
  if (c->edit != NULL) { SetFocus(c->edit); }
}

/* ====================================================================== */
/* Enum picker                                                            */
/* ====================================================================== */

#define SR_PICK_ENUM_CLASS L"FreecivSRPickEnum"
#define IDC_PICKENUM_LIST  9201

struct enum_ctx {
  HWND dlg;
  HWND list;
  HWND prev_focus;
  const char * const *values;
  int n;
  sr_pick_enum_cb cb;
  void *userdata;
  WNDPROC list_default;
};

static struct enum_ctx *g_enum_ctx = NULL;

static void enum_close(BOOL commit)
{
  struct enum_ctx *c = g_enum_ctx;
  int sel = -1;

  if (c == NULL) { return; }

  /* Read selection BEFORE detaching g_enum_ctx. enum_list_proc only
     forwards LB_GETCURSEL to the original LISTBOX proc while
     g_enum_ctx is set; after detach DefWindowProc returns 0, which
     would silently pin every commit to index 0. */
  if (commit && c->list != NULL) {
    sel = (int)SendMessageW(c->list, LB_GETCURSEL, 0, 0);
  }

  g_enum_ctx = NULL;  /* Detach before callback — prevents re-entrant free
                         when a cascading picker (e.g. nation → leader)
                         opens a new sr_pick_enum from inside the callback. */

  if (commit && c->cb != NULL && sel >= 0 && sel < c->n) {
    c->cb(sel, c->values[sel], c->userdata);
  }
  if (c->dlg != NULL) { DestroyWindow(c->dlg); }
  /* Skip focus restore if the callback opened a new picker (cascade). */
  if (!sr_pick_any_active()
      && c->prev_focus != NULL && IsWindow(c->prev_focus)) {
    SetFocus(c->prev_focus);
  }
  free(c);
}

static LRESULT CALLBACK enum_list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  /* Claim Enter/Escape so IsDialogMessageW in the main message loop
     does not route them as default-button presses (no IDOK button
     exists on this picker). */
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) { enum_close(TRUE);  return 0; }
    if (w == VK_ESCAPE) { enum_close(FALSE); return 0; }
  }
  if (g_enum_ctx != NULL) {
    return CallWindowProcW(g_enum_ctx->list_default, h, m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK enum_wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);
      int i;

      if (g_enum_ctx == NULL) { return -1; }
      g_enum_ctx->list = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_PICKENUM_LIST, hi, NULL);
      if (g_enum_ctx->list != NULL) {
        g_enum_ctx->list_default = (WNDPROC)SetWindowLongPtrW(
            g_enum_ctx->list, GWLP_WNDPROC, (LONG_PTR)enum_list_proc);
        for (i = 0; i < g_enum_ctx->n; i++) {
          wchar_t *ws = to_wide(g_enum_ctx->values[i]);
          if (ws != NULL) {
            SendMessageW(g_enum_ctx->list, LB_ADDSTRING, 0, (LPARAM)ws);
            free(ws);
          }
        }
      }
      return 0;
    }
    case WM_SIZE: {
      RECT rc; int pad = 6;
      GetClientRect(h, &rc);
      if (g_enum_ctx != NULL && g_enum_ctx->list != NULL) {
        MoveWindow(g_enum_ctx->list, pad, pad,
                   rc.right - 2 * pad, rc.bottom - 2 * pad, TRUE);
      }
      return 0;
    }
    case WM_COMMAND:
      if (g_enum_ctx != NULL && HIWORD(w) == LBN_DBLCLK
          && (HWND)l == g_enum_ctx->list) {
        enum_close(TRUE);
        return 0;
      }
      break;
    case WM_CLOSE:   enum_close(FALSE); return 0;
    case WM_DESTROY:
      /* Cascade-safe: clear fields only if g_enum_ctx still points at
         THIS window. A commit-callback may have opened a new picker
         and repointed g_enum_ctx before DestroyWindow on the old
         one fired this WM_DESTROY. */
      sr_window_unregister_popup(h);
      if (g_enum_ctx != NULL && g_enum_ctx->dlg == h) {
        g_enum_ctx->dlg = NULL;
        g_enum_ctx->list = NULL;
      }
      return 0;
    default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

static int enum_class_registered = 0;
static int ensure_enum_class(HINSTANCE hi)
{
  WNDCLASSEXW wc;
  if (enum_class_registered) { return 1; }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = enum_wnd_proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_PICK_ENUM_CLASS;
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    enum_class_registered = 1;
    return 1;
  }
  return 0;
}

void sr_pick_enum(const char *title,
                  const char * const *values, int n_values, int current,
                  sr_pick_enum_cb on_commit, void *userdata)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  wchar_t *wtitle;
  struct enum_ctx *c;

  if (!ensure_enum_class(hi)) { return; }
  if (g_enum_ctx != NULL) { enum_close(FALSE); }
  if (values == NULL || n_values <= 0) { return; }

  c = (struct enum_ctx *)calloc(1, sizeof(*c));
  if (c == NULL) { return; }
  c->prev_focus = GetFocus();
  c->values = values;
  c->n = n_values;
  c->cb = on_commit;
  c->userdata = userdata;
  g_enum_ctx = c;

  wtitle = to_wide(title != NULL ? title : "Choose");
  c->dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
      SR_PICK_ENUM_CLASS, wtitle != NULL ? wtitle : L"Choose",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
      CW_USEDEFAULT, CW_USEDEFAULT, 340, 360,
      NULL, NULL, hi, NULL);
  if (wtitle != NULL) { free(wtitle); }
  if (c->dlg == NULL) {
    free(c); return;
  }
  sr_window_register_popup(c->dlg);
  if (c->list != NULL && current >= 0 && current < c->n) {
    SendMessageW(c->list, LB_SETCURSEL, (WPARAM)current, 0);
  }
  ShowWindow(c->dlg, SW_SHOWNORMAL);
  UpdateWindow(c->dlg);
  if (c->list != NULL) { SetFocus(c->list); }
}

/* ====================================================================== */
/* String picker                                                          */
/* ====================================================================== */

#define SR_PICK_STRING_CLASS L"FreecivSRPickString"
#define IDC_PICKSTRING_EDIT  9301

struct string_ctx {
  HWND dlg;
  HWND edit;
  HWND prev_focus;
  sr_pick_string_cb cb;
  void *userdata;
  WNDPROC edit_default;
};

static struct string_ctx *g_string_ctx = NULL;

static void string_close(BOOL commit)
{
  struct string_ctx *c = g_string_ctx;
  wchar_t wbuf[512];
  int n = 0;

  if (c == NULL) { return; }

  /* Read text BEFORE detach (same reason as int_close). */
  if (commit && c->edit != NULL) {
    n = GetWindowTextW(c->edit, wbuf, 511);
    wbuf[n > 0 ? (n < 511 ? n : 511) : 0] = L'\0';
  }

  g_string_ctx = NULL;  /* Detach before callback — re-entrancy guard */

  if (commit && c->cb != NULL) {
    char utf8[1024];

    if (n <= 0
        || WideCharToMultiByte(CP_UTF8, 0, wbuf, -1,
                               utf8, sizeof(utf8), NULL, NULL) <= 0) {
      utf8[0] = '\0';
    }
    c->cb(utf8, c->userdata);
  }
  if (c->dlg != NULL) { DestroyWindow(c->dlg); }
  /* Skip focus restore if the callback opened a new picker (cascade). */
  if (!sr_pick_any_active()
      && c->prev_focus != NULL && IsWindow(c->prev_focus)) {
    SetFocus(c->prev_focus);
  }
  free(c);
}

static LRESULT CALLBACK string_edit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_HASSETSEL
           | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) { string_close(TRUE);  return 0; }
    if (w == VK_ESCAPE) { string_close(FALSE); return 0; }
  }
  if (g_string_ctx != NULL) {
    return CallWindowProcW(g_string_ctx->edit_default, h, m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK string_wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      if (g_string_ctx == NULL) { return -1; }
      g_string_ctx->edit = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_PICKSTRING_EDIT, hi, NULL);
      if (g_string_ctx->edit != NULL) {
        g_string_ctx->edit_default = (WNDPROC)SetWindowLongPtrW(
            g_string_ctx->edit, GWLP_WNDPROC, (LONG_PTR)string_edit_proc);
      }
      return 0;
    }
    case WM_SIZE: {
      RECT rc; int pad = 8;
      GetClientRect(h, &rc);
      if (g_string_ctx != NULL && g_string_ctx->edit != NULL) {
        MoveWindow(g_string_ctx->edit, pad, pad,
                   rc.right - 2 * pad, 24, TRUE);
      }
      return 0;
    }
    case WM_CLOSE:   string_close(FALSE); return 0;
    case WM_DESTROY:
      /* Cascade-safe: see the matching comment in int_wnd_proc /
         enum_wnd_proc. Clear fields only when THIS window still owns
         the global context. */
      sr_window_unregister_popup(h);
      if (g_string_ctx != NULL && g_string_ctx->dlg == h) {
        g_string_ctx->dlg = NULL;
        g_string_ctx->edit = NULL;
      }
      return 0;
    default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

static int string_class_registered = 0;
static int ensure_string_class(HINSTANCE hi)
{
  WNDCLASSEXW wc;
  if (string_class_registered) { return 1; }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = string_wnd_proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_PICK_STRING_CLASS;
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    string_class_registered = 1;
    return 1;
  }
  return 0;
}

void sr_pick_string(const char *title, const char *initial,
                    sr_pick_string_cb on_commit, void *userdata)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  wchar_t *wtitle;
  struct string_ctx *c;

  if (!ensure_string_class(hi)) { return; }
  if (g_string_ctx != NULL) { string_close(FALSE); }

  c = (struct string_ctx *)calloc(1, sizeof(*c));
  if (c == NULL) { return; }
  c->prev_focus = GetFocus();
  c->cb = on_commit;
  c->userdata = userdata;
  g_string_ctx = c;

  wtitle = to_wide(title != NULL ? title : "Value");
  c->dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
      SR_PICK_STRING_CLASS, wtitle != NULL ? wtitle : L"Value",
      WS_POPUP | WS_CAPTION | WS_SYSMENU,
      CW_USEDEFAULT, CW_USEDEFAULT, 420, 92,
      NULL, NULL, hi, NULL);
  if (wtitle != NULL) { free(wtitle); }
  if (c->dlg == NULL) {
    free(c); return;
  }
  sr_window_register_popup(c->dlg);
  if (c->edit != NULL) {
    wchar_t *winit = to_wide(initial != NULL ? initial : "");
    if (winit != NULL) {
      SetWindowTextW(c->edit, winit);
      free(winit);
      SendMessageW(c->edit, EM_SETSEL, 0, -1);
    }
  }
  ShowWindow(c->dlg, SW_SHOWNORMAL);
  UpdateWindow(c->dlg);
  if (c->edit != NULL) { SetFocus(c->edit); }
}

/* Defined here, at the bottom, so all three g_*_ctx globals are in
   scope. Forward-declared at the top so the close functions can call
   it without circular dependencies. */
static int sr_pick_any_active(void)
{
  return (g_int_ctx != NULL)
      || (g_enum_ctx != NULL)
      || (g_string_ctx != NULL);
}

#endif  /* _WIN32 */
