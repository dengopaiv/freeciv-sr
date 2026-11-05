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

/*
  Screen-reader output backend.

  On Windows, dynamically loads nvdaControllerClient(64|32).dll and forwards
  speech / braille to NVDA. On any other platform, or when no reader is
  present, falls back to stderr so development without a reader is possible.

  Kept free of freeciv headers so the file can later be lifted into an NVDA
  addon package. The sole gui-sr dependency is sr_log, which is a simple
  Win32 EDIT wrapper — callers extracting this file for an addon should
  stub the sr_log_append() / sr_log_is_up() pair.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "sr_speech.h"
#include "sr_log.h"

#ifdef _WIN32

typedef unsigned long sr_err_t;  /* NVDA's error_status_t is ULONG. */
typedef sr_err_t (__stdcall *fn_test_t)(void);
typedef sr_err_t (__stdcall *fn_speak_t)(const wchar_t *);
typedef sr_err_t (__stdcall *fn_cancel_t)(void);
typedef sr_err_t (__stdcall *fn_braille_t)(const wchar_t *);

static HMODULE nvda_dll = NULL;
static fn_test_t    fn_test    = NULL;
static fn_speak_t   fn_speak   = NULL;
static fn_cancel_t  fn_cancel  = NULL;
static fn_braille_t fn_braille = NULL;
static int reader_live = 0;

static wchar_t *utf8_to_wide(const char *utf8)
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
  if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen) <= 0) {
    free(w);
    return NULL;
  }
  return w;
}

void sr_speech_init(void)
{
  if (nvda_dll != NULL) {
    return;
  }
#ifdef _WIN64
  nvda_dll = LoadLibraryA("nvdaControllerClient64.dll");
#else
  nvda_dll = LoadLibraryA("nvdaControllerClient32.dll");
#endif
  if (nvda_dll == NULL) {
    /* Try the generic name as a fallback. */
    nvda_dll = LoadLibraryA("nvdaControllerClient.dll");
  }
  if (nvda_dll == NULL) {
    fprintf(stderr,
            "[sr] nvdaControllerClient DLL not found; using stderr fallback.\n");
    return;
  }
  fn_test    = (fn_test_t)   (void *)GetProcAddress(nvda_dll, "nvdaController_testIfRunning");
  fn_speak   = (fn_speak_t)  (void *)GetProcAddress(nvda_dll, "nvdaController_speakText");
  fn_cancel  = (fn_cancel_t) (void *)GetProcAddress(nvda_dll, "nvdaController_cancelSpeech");
  fn_braille = (fn_braille_t)(void *)GetProcAddress(nvda_dll, "nvdaController_brailleMessage");

  if (fn_test != NULL && fn_test() == 0) {
    reader_live = 1;
  } else {
    fprintf(stderr, "[sr] NVDA not running; using stderr fallback.\n");
  }
}

void sr_speech_exit(void)
{
  if (nvda_dll != NULL) {
    FreeLibrary(nvda_dll);
    nvda_dll = NULL;
  }
  fn_test = NULL;
  fn_speak = NULL;
  fn_cancel = NULL;
  fn_braille = NULL;
  reader_live = 0;
}

void sr_say(const char *utf8_text, int interrupt)
{
  if (utf8_text == NULL || utf8_text[0] == '\0') {
    return;
  }
  /* Always mirror to the event-log EDIT (no-op until the window is up).
     NVDA reads the log natively when the user is focused there; the
     direct speech path below covers the case when they're not. */
  sr_log_append(utf8_text);

  if (reader_live && fn_speak != NULL) {
    wchar_t *w;
    if (interrupt && fn_cancel != NULL) {
      fn_cancel();
    }
    w = utf8_to_wide(utf8_text);
    if (w != NULL) {
      fn_speak(w);
      free(w);
      return;
    }
  }
  /* Only echo to stderr when no window is up yet (early startup) and no
     NVDA is attached — otherwise the log / NVDA cover it. */
  if (!sr_log_is_up()) {
    fprintf(stderr, "[sr] %s\n", utf8_text);
    fflush(stderr);
  }
}

void sr_interrupt(void)
{
  if (reader_live && fn_cancel != NULL) {
    fn_cancel();
  }
}

void sr_braille(const char *utf8_text)
{
  if (utf8_text == NULL || utf8_text[0] == '\0') {
    return;
  }
  if (reader_live && fn_braille != NULL) {
    wchar_t *w = utf8_to_wide(utf8_text);
    if (w != NULL) {
      fn_braille(w);
      free(w);
    }
  }
}

int sr_has_reader(void)
{
  return reader_live;
}

#else  /* !_WIN32 */

void sr_speech_init(void)
{
  fprintf(stderr, "[sr] non-Windows build; using stderr fallback.\n");
}

void sr_speech_exit(void)
{
}

void sr_say(const char *utf8_text, int interrupt)
{
  (void)interrupt;
  if (utf8_text == NULL || utf8_text[0] == '\0') {
    return;
  }
  fprintf(stderr, "[sr] %s\n", utf8_text);
  fflush(stderr);
}

void sr_interrupt(void)
{
}

void sr_braille(const char *utf8_text)
{
  (void)utf8_text;
}

int sr_has_reader(void)
{
  return 0;
}

#endif
