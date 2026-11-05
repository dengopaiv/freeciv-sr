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
#include <string.h>
#include <stdlib.h>

#include "sr_log.h"

#ifdef _WIN32
#include <windows.h>
#include "sr_window.h"  /* WM_SR_LOG_APPEND, sr_window_ui_thread_id */
#endif

#ifdef _WIN32

#define SR_LOG_TRIM_SOFT  (200 * 1024)   /* trim when buffer > ~200 KB */
#define SR_LOG_TRIM_KEEP  (150 * 1024)   /* keep this much after trim */

static HWND g_edit = NULL;

/* ---------------------------------------------------------------------
   File persistence.

   We tee every sr_log_append call into `_srlog.txt` next to the exe so
   that post-mortem debugging has a breadcrumb trail — the in-memory
   EDIT buffer is lost when the process exits, and run-sr.ps1's
   _runtime.log only captures stderr (which SR barely uses past boot).

   The file is opened lazily on first write (append mode, line-flushed),
   prefixed with a session header and per-line HH:MM:SS timestamps. A
   critical section serialises writes across the UI thread and any
   worker threads that reach sr_log_append directly.
   ------------------------------------------------------------------ */

static FILE *g_log_file = NULL;
static CRITICAL_SECTION g_log_lock;
static volatile LONG g_log_init = 0;   /* 0=pristine, 1=init-in-progress,
                                          2=ready */
static BOOL g_log_file_tried = FALSE;

static void log_init_once(void)
{
  LONG prev = InterlockedCompareExchange(&g_log_init, 1, 0);

  if (prev == 0) {
    InitializeCriticalSection(&g_log_lock);
    InterlockedExchange(&g_log_init, 2);
    return;
  }
  if (prev == 2) {
    return;
  }
  /* prev == 1: another thread is mid-init. Yield until state reaches 2. */
  while (g_log_init != 2) {
    Sleep(0);
  }
}

/* Caller must hold g_log_lock. One-shot: on success opens g_log_file
   and writes a session header; on failure sets g_log_file_tried so we
   don't retry every line. */
static void ensure_log_file_locked(void)
{
  wchar_t path_w[MAX_PATH];
  DWORD n;
  wchar_t *slash;
  SYSTEMTIME st;
  static const wchar_t FNAME[] = L"_srlog.txt";

  if (g_log_file_tried) {
    return;
  }
  g_log_file_tried = TRUE;

  n = GetModuleFileNameW(NULL, path_w, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return;
  }
  slash = wcsrchr(path_w, L'\\');
  if (slash == NULL) {
    return;
  }
  slash[1] = L'\0';  /* keep trailing backslash */
  if (wcslen(path_w) + wcslen(FNAME) >= MAX_PATH) {
    return;
  }
  wcscat(path_w, FNAME);
  g_log_file = _wfopen(path_w, L"a");
  if (g_log_file == NULL) {
    return;
  }
  GetLocalTime(&st);
  fprintf(g_log_file,
          "=== freeciv-sr session started %04d-%02d-%02d %02d:%02d:%02d ===\n",
          st.wYear, st.wMonth, st.wDay,
          st.wHour, st.wMinute, st.wSecond);
  fflush(g_log_file);
}

/* Shared timestamped emit: "HH:MM:SS  line\n". Called with both stdout
   (live console view) and g_log_file (post-mortem) as sinks. */
static void emit_stamped(FILE *f, const SYSTEMTIME *st, const char *utf8_line)
{
  size_t len = strlen(utf8_line);

  fprintf(f, "%02d:%02d:%02d  %s",
          st->wHour, st->wMinute, st->wSecond, utf8_line);
  if (len == 0 || utf8_line[len - 1] != '\n') {
    fputc('\n', f);
  }
  fflush(f);
}

static void write_to_sinks(const char *utf8_line)
{
  SYSTEMTIME st;

  log_init_once();
  EnterCriticalSection(&g_log_lock);
  ensure_log_file_locked();
  GetLocalTime(&st);
  /* Live console first — NVDA reads Win32 consoles natively, so this
     is the user's real-time view of picker / dialog activity. */
  if (stdout != NULL) {
    emit_stamped(stdout, &st, utf8_line);
  }
  if (g_log_file != NULL) {
    emit_stamped(g_log_file, &st, utf8_line);
  }
  LeaveCriticalSection(&g_log_lock);
}

void sr_log_attach(HWND edit_hwnd)
{
  g_edit = edit_hwnd;
}

int sr_log_is_up(void)
{
  return g_edit != NULL;
}

/* Trim oldest text when the EDIT gets large. The EDIT stores lines joined
   by "\r\n"; we find the first newline past the "keep from" offset and
   delete everything before it so we cut on a line boundary. */
static void trim_if_large(void)
{
  int len = GetWindowTextLengthW(g_edit);

  if (len <= 0 || (size_t)len < SR_LOG_TRIM_SOFT / sizeof(wchar_t)) {
    return;
  }
  {
    int cut_wchars = len - (int)(SR_LOG_TRIM_KEEP / sizeof(wchar_t));

    if (cut_wchars <= 0) {
      return;
    }
    SendMessageW(g_edit, EM_SETSEL, (WPARAM)0, (LPARAM)cut_wchars);
    SendMessageW(g_edit, EM_REPLACESEL, FALSE, (LPARAM)L"");
  }
}

/* Append to the EDIT without moving user's caret if they've scrolled up.
   We set the selection to the very end, replace-sel with the new text,
   then restore the original selection. */
static void append_on_ui_thread(const char *utf8)
{
  int utf8_len;
  int wlen;
  wchar_t *wbuf;
  int end_pos;
  DWORD prev_sel_start = 0, prev_sel_end = 0;
  BOOL user_has_selection;

  if (g_edit == NULL || utf8 == NULL || utf8[0] == '\0') {
    return;
  }

  utf8_len = (int)strlen(utf8);
  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, utf8_len, NULL, 0);
  if (wlen <= 0) {
    return;
  }

  /* Allocate room for wide text + optional "\r\n" + NUL. */
  wbuf = (wchar_t *)malloc(sizeof(wchar_t) * (wlen + 3));
  if (wbuf == NULL) {
    return;
  }
  MultiByteToWideChar(CP_UTF8, 0, utf8, utf8_len, wbuf, wlen);

  /* Ensure the appended text ends in CRLF so the EDIT shows it as its
     own line. The incoming text may already carry a trailing newline;
     normalise. */
  while (wlen > 0 && (wbuf[wlen - 1] == L'\n' || wbuf[wlen - 1] == L'\r')) {
    wlen--;
  }
  wbuf[wlen++] = L'\r';
  wbuf[wlen++] = L'\n';
  wbuf[wlen] = L'\0';

  SendMessageW(g_edit, EM_GETSEL,
               (WPARAM)&prev_sel_start, (LPARAM)&prev_sel_end);
  user_has_selection = (prev_sel_start != prev_sel_end);

  end_pos = GetWindowTextLengthW(g_edit);
  SendMessageW(g_edit, EM_SETSEL, (WPARAM)end_pos, (LPARAM)end_pos);
  SendMessageW(g_edit, EM_REPLACESEL, FALSE, (LPARAM)wbuf);

  /* Restore the user's selection/caret if they were reading history.
     If they had the caret at the end already, leave it at the new end
     so they keep following live output. */
  if (user_has_selection || (int)prev_sel_end < end_pos) {
    SendMessageW(g_edit, EM_SETSEL,
                 (WPARAM)prev_sel_start, (LPARAM)prev_sel_end);
  } else {
    /* Scroll the new line into view. */
    SendMessageW(g_edit, EM_SCROLLCARET, 0, 0);
  }

  free(wbuf);
  trim_if_large();
}

void sr_log_append(const char *utf8_line)
{
  if (utf8_line == NULL || utf8_line[0] == '\0') {
    return;
  }

  /* Tee to stdout + file unconditionally. This fires even before
     g_edit is attached, so early boot / pregame lines are captured. */
  write_to_sinks(utf8_line);

  if (g_edit == NULL) {
    return;
  }

  if (GetCurrentThreadId() == sr_window_ui_thread_id()) {
    append_on_ui_thread(utf8_line);
  } else {
    /* Off-thread: make a copy the UI thread will free after appending. */
    size_t n = strlen(utf8_line);
    char *copy = (char *)malloc(n + 1);

    if (copy != NULL) {
      memcpy(copy, utf8_line, n + 1);
      if (!PostMessageW(g_edit, WM_SR_LOG_APPEND, 0, (LPARAM)copy)) {
        /* Posting failed (window is gone). Free to avoid leak. */
        free(copy);
      }
    }
  }
}

/* Called by sr_window's WndProc when a WM_SR_LOG_APPEND arrives. The
   lParam is a malloc'd UTF-8 string owned by the message; we take
   ownership here. */
void sr_log_handle_posted(LPARAM lparam)
{
  char *utf8 = (char *)lparam;

  if (utf8 != NULL) {
    append_on_ui_thread(utf8);
    free(utf8);
  }
}

#else  /* !_WIN32 */

void sr_log_attach(void *edit_hwnd) { (void)edit_hwnd; }
void sr_log_append(const char *utf8_line) { (void)utf8_line; }
int  sr_log_is_up(void) { return 0; }

#endif
