/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
***********************************************************************/

/*
  Random map generator sub-mode for freeciv-sr-tools.

  Walks the user through a chain of pickers (ruleset, mapsize, generator,
  landmass, temperature, huts, opponents, seed, filename), writes a .serv
  script to TEMP, spawns the bundled freeciv-server.exe headlessly with
  --read <script>, and reports success or failure. The save lands in
  %APPDATA%\.freeciv\saves\<name>.sav.bz2 — the same dir the main game's
  Load dialog scans, so the round-trip is "generate here, load in
  freeciv-sr.exe" with zero extra wiring.

  Save shape: pregame, BEFORE `start`. The server only saves player
  records when game_was_started() is true (savegame3.c:2348-2354), so
  this keeps `game.save_players=false` in the file, which makes
  `game.info.is_new_game=true` on load (savegame3.c:2228-2229). The
  gui-sr load handler already auto-fires `/take -` in that case
  (connectdlg.c:97-99), so the user lands in the pregame lobby attached
  to one of the aifill-created AI slots. From there Ctrl+Shift+N opens
  the nation picker (sr_window.c:ID_SR_GAME_NATION) and `begin` runs
  `start` which generates the map using the saved settings. The seed is
  always baked in (random or user-picked) so reloading reproduces the
  same map every time.

  No mid-game regeneration: each map roll is its own one-shot server
  invocation. That is a deliberate match to how the server works
  (map_generate runs only on the pregame -> running transition).
*/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <shlobj.h>

#include "sr_pick.h"
#include "sr_speech.h"

#include "mode_mapgen.h"

/* ---- picker value tables -------------------------------------------- */

static const char *const RULESETS[] = {
  "civ2civ3", "classic", "multiplayer", "civ1", "civ2",
  "experimental", "sandbox", "alien"
};
#define N_RULESETS ((int)(sizeof(RULESETS) / sizeof(RULESETS[0])))

/* Server `set mapsize fullsize` is the simplest: pick total tile count
   bracket via these labels. The numeric column maps to the value
   we send via `set size N` which matches freeciv's "size" dimension. */
static const char *const MAPSIZE_LABELS[] = {
  "Tiny (size 1)",
  "Small (size 2)",
  "Normal (size 4)",
  "Large (size 6)",
  "Huge (size 10)"
};
static const int MAPSIZE_VALUES[] = { 1, 2, 4, 6, 10 };
#define N_MAPSIZES 5

static const char *const GENERATORS[] = {
  "random", "fractal", "island", "fair", "fracture", "scenario",
  "pseudofractal"
};
#define N_GENERATORS ((int)(sizeof(GENERATORS) / sizeof(GENERATORS[0])))

/* ---- chain state ---------------------------------------------------- */

struct mapgen_state {
  int   ruleset_idx;
  int   mapsize_idx;
  int   generator_idx;
  int   landmass;
  int   temperature;
  int   huts;
  int   opponents;
  int   mapseed;
  char  filename[128];
};

static struct mapgen_state g_state;

static void on_pick_ruleset(int idx, const char *v, void *ud);
static void on_pick_mapsize(int idx, const char *v, void *ud);
static void on_pick_generator(int idx, const char *v, void *ud);
static void on_pick_landmass(int value, void *ud);
static void on_pick_temperature(int value, void *ud);
static void on_pick_huts(int value, void *ud);
static void on_pick_opponents(int value, void *ud);
static void on_pick_seed(int value, void *ud);
static void on_pick_filename(const char *value, void *ud);
static void run_generation(void);

void mode_mapgen_open(void)
{
  /* Sensible defaults; user picks each in turn. */
  g_state.ruleset_idx   = 0;   /* civ2civ3 */
  g_state.mapsize_idx   = 2;   /* normal */
  g_state.generator_idx = 1;   /* fractal */
  g_state.landmass      = 30;
  g_state.temperature   = 50;
  g_state.huts          = 15;
  g_state.opponents     = 4;   /* 4 AI players (user takes one in pregame) */
  g_state.mapseed       = 0;   /* 0 = random; tool picks a real seed */
  g_state.filename[0]   = '\0';

  sr_say("Pick a ruleset.", 1);
  sr_pick_enum("Ruleset", RULESETS, N_RULESETS, g_state.ruleset_idx,
               on_pick_ruleset, NULL);
}

static void on_pick_ruleset(int idx, const char *v, void *ud)
{
  (void) v; (void) ud;
  if (idx < 0) { tools_open_mode_picker(); return; }
  g_state.ruleset_idx = idx;
  sr_say("Pick a map size.", 1);
  sr_pick_enum("Map size", MAPSIZE_LABELS, N_MAPSIZES,
               g_state.mapsize_idx, on_pick_mapsize, NULL);
}

static void on_pick_mapsize(int idx, const char *v, void *ud)
{
  (void) v; (void) ud;
  if (idx < 0) { tools_open_mode_picker(); return; }
  g_state.mapsize_idx = idx;
  sr_say("Pick a map generator.", 1);
  sr_pick_enum("Map generator", GENERATORS, N_GENERATORS,
               g_state.generator_idx, on_pick_generator, NULL);
}

static void on_pick_generator(int idx, const char *v, void *ud)
{
  (void) v; (void) ud;
  if (idx < 0) { tools_open_mode_picker(); return; }
  g_state.generator_idx = idx;
  sr_say("Landmass percent, 1 to 100.", 1);
  sr_pick_int("Landmass percent (1-100)", g_state.landmass, 1, 100,
              on_pick_landmass, NULL);
}

static void on_pick_landmass(int value, void *ud)
{
  (void) ud;
  g_state.landmass = value;
  sr_say("Temperature, 0 to 100.", 1);
  sr_pick_int("Temperature (0-100)", g_state.temperature, 0, 100,
              on_pick_temperature, NULL);
}

static void on_pick_temperature(int value, void *ud)
{
  (void) ud;
  g_state.temperature = value;
  sr_say("Goody huts, 0 to 500.", 1);
  sr_pick_int("Goody huts (0-500)", g_state.huts, 0, 500,
              on_pick_huts, NULL);
}

static void on_pick_huts(int value, void *ud)
{
  (void) ud;
  g_state.huts = value;
  sr_say("Number of AI opponents.", 1);
  sr_pick_int("AI opponents (1-30)", g_state.opponents, 1, 30,
              on_pick_opponents, NULL);
}

static void on_pick_opponents(int value, void *ud)
{
  (void) ud;
  g_state.opponents = value;
  sr_say("Map seed. Zero means random.", 1);
  sr_pick_int("Map seed (0 = random)", g_state.mapseed, 0, 0x7fffffff,
              on_pick_seed, NULL);
}

static void on_pick_seed(int value, void *ud)
{
  (void) ud;
  g_state.mapseed = value;
  /* Always bake a real seed into the save so reloading reproduces the same
     map. If the user picked 0 ("random"), pick one for them now. */
  if (g_state.mapseed == 0) {
    g_state.mapseed = (int)((GetTickCount() ^ GetCurrentProcessId()) & 0x7fffffff);
    if (g_state.mapseed == 0) { g_state.mapseed = 1; }
  }
  sr_say("Save filename, no extension.", 1);
  sr_pick_string("Save filename (no extension)",
                 g_state.filename[0] != '\0' ? g_state.filename : "newmap",
                 on_pick_filename, NULL);
}

static void on_pick_filename(const char *value, void *ud)
{
  (void) ud;
  if (value == NULL || value[0] == '\0') {
    sr_say("Empty filename. Cancelled.", 1);
    tools_open_mode_picker();
    return;
  }
  /* Reject path separators — saves must land in the saves dir. */
  if (strchr(value, '/') != NULL || strchr(value, '\\') != NULL
      || strchr(value, ':') != NULL) {
    sr_say("Filename must not contain path separators. Cancelled.", 1);
    tools_open_mode_picker();
    return;
  }
  strncpy(g_state.filename, value, sizeof(g_state.filename) - 1);
  g_state.filename[sizeof(g_state.filename) - 1] = '\0';
  run_generation();
}

/* ---- script writer + server spawn --------------------------------- */

static int write_script(const wchar_t *script_path)
{
  FILE *f = _wfopen(script_path, L"w");

  if (f == NULL) { return 0; }
  /* Save in pregame, BEFORE `start`. That keeps game.save_players=FALSE
     in the file, which makes is_new_game=TRUE on load — so the gui-sr
     load handler auto-fires `/take -` and the user lands in the lobby
     attached to one of the aifill players. From there the existing
     popup_races_dialog (dialogs.c:820) gives them the standard nation
     picker, and `begin` runs `start` which generates the map using
     these saved settings. The seed is always baked in (random or user-
     picked) so reload reproduces the same map. */
  fprintf(f, "rulesetdir %s\n", RULESETS[g_state.ruleset_idx]);
  fprintf(f, "set size %d\n",  MAPSIZE_VALUES[g_state.mapsize_idx]);
  fprintf(f, "set generator %s\n", GENERATORS[g_state.generator_idx]);
  fprintf(f, "set landmass %d\n", g_state.landmass);
  fprintf(f, "set temperature %d\n", g_state.temperature);
  fprintf(f, "set huts %d\n", g_state.huts);
  fprintf(f, "set mapseed %d\n", g_state.mapseed);
  /* aifill in pregame creates AI players immediately so the loader's
     /take - has someone to attach to. */
  fprintf(f, "set aifill %d\n", g_state.opponents);
  fprintf(f, "save %s\n", g_state.filename);
  fprintf(f, "quit\n");
  fclose(f);
  return 1;
}

/* Locate freeciv-server.exe alongside this exe. */
static int find_server_exe(wchar_t *out, size_t outlen)
{
  wchar_t self[MAX_PATH];
  DWORD n = GetModuleFileNameW(NULL, self, MAX_PATH);
  wchar_t *slash;

  if (n == 0 || n >= MAX_PATH) { return 0; }
  slash = wcsrchr(self, L'\\');
  if (slash == NULL) { return 0; }
  *slash = L'\0';
  if (_snwprintf(out, outlen, L"%ls\\freeciv-server.exe", self) <= 0) {
    return 0;
  }
  out[outlen - 1] = L'\0';
  return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

static void run_generation(void)
{
  wchar_t script_path[MAX_PATH];
  wchar_t server_exe[MAX_PATH];
  wchar_t cmdline[MAX_PATH * 3];
  wchar_t temp_dir[MAX_PATH];
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  DWORD exit_code = 1;

  if (GetTempPathW(MAX_PATH, temp_dir) == 0) {
    sr_say("Failed to locate TEMP directory.", 1);
    tools_open_mode_picker();
    return;
  }
  if (_snwprintf(script_path, MAX_PATH, L"%lsfcsr-tools-%lu.serv",
                 temp_dir, (unsigned long) GetCurrentProcessId()) <= 0) {
    sr_say("Failed to build script path.", 1);
    tools_open_mode_picker();
    return;
  }
  script_path[MAX_PATH - 1] = L'\0';

  if (!write_script(script_path)) {
    sr_say("Failed to write generator script.", 1);
    tools_open_mode_picker();
    return;
  }

  if (!find_server_exe(server_exe, MAX_PATH)) {
    sr_say("freeciv-server.exe not found beside this tool.", 1);
    DeleteFileW(script_path);
    tools_open_mode_picker();
    return;
  }

  /* --port 0 lets the OS pick a free port so a running freeciv-server
     elsewhere on the box doesn't make us fail to bind. The script's
     `quit` will tear the server down before the port matters anyway. */
  if (_snwprintf(cmdline, MAX_PATH * 3,
                 L"\"%ls\" --port 0 --read \"%ls\"",
                 server_exe, script_path) <= 0) {
    sr_say("Failed to build server command line.", 1);
    DeleteFileW(script_path);
    tools_open_mode_picker();
    return;
  }
  cmdline[MAX_PATH * 3 - 1] = L'\0';

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  sr_say("Generating map. Please wait.", 1);

  if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                      CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
    sr_say("Failed to launch freeciv-server.", 1);
    DeleteFileW(script_path);
    tools_open_mode_picker();
    return;
  }

  /* Cap the wait at 5 minutes — even a huge map finishes much sooner. */
  if (WaitForSingleObject(pi.hProcess, 5 * 60 * 1000) == WAIT_OBJECT_0) {
    GetExitCodeProcess(pi.hProcess, &exit_code);
  } else {
    TerminateProcess(pi.hProcess, 1);
    sr_say("Map generation timed out. Cancelled.", 1);
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  DeleteFileW(script_path);

  if (exit_code == 0) {
    char msg[320];

    snprintf(msg, sizeof(msg),
             "Saved as %s. Open it in freeciv-sr with control O. In the"
             " pregame lobby press control shift N to pick your nation,"
             " then alt G then B to begin.", g_state.filename);
    sr_say(msg, 1);
  } else {
    sr_say("Map setup failed. Try different settings.", 1);
  }
  tools_open_mode_picker();
}
