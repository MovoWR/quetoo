# CGAME and Presentation

## Purpose

The client half of Race. CGAME predicts movement so play feels immediate,
parses the server's Race messages and config strings, and presents
everything — HUD, scoreboard, finish reports, replays, votes, the native UI.
**It never decides authoritative Race results**: every timer value, record,
vote tally, and setting it shows comes from GAME.

## Ownership

CGAME owns presentation and prediction only. Its state is a set of read-only
mirrors of server data, plus purely local conveniences (practice markers,
HUD preferences) that the server never sees.

## Main components

```text
cg_module.c              The CGAME module contract: init, parse, scene, UI hooks.
cg_module_compat.h       Module-private hook declarations (not a host API).
cg_race_message.c        Shared wire-parsing helpers (string bounds, drain).
cg_race_presentation.c   Pure formatters shared by HUD/score/finish/replay.
cg_predict.c             The prediction loop (Race override of common's).
cg_entity.c              Entity interpolation with Race hiding/filtering.
cg_score_model.c         Score snapshot assembly (pure, engine-free).
cg_score.c               Scoreboard + roster + leaderboard rendering.
cg_race_hud.c            The Race HUD (see UI_AND_HUD.md).
cg_race_finish_report.c  Finish report banner.
cg_race_map_browser.c    Map browser message decoding.
cg_race_profiles.c       Client half of profile enrollment/authentication.
cg_media.c / cg_main.c / cg_discord.c   Stock-derived overrides with Race tails.
```

## Module lifecycle

`cg_module.c` implements the CGAME module contract: `Cg_Module_Init` brings up
each Race subsystem (admin auth, profiles, double jump, weapon tuning,
settings, physics mirror, finish report, HUD, markers, replay, training);
`Cg_Module_ClearState` resets them all per connection. Race hooks into the
frame at defined points: message parsing is a short-circuit chain over the
subsystem parsers, `Cg_Module_PopulateScene` adds barriers, markers, and
replay visuals after common's entities and effects, and `Cg_Module_UpdateUi`
refreshes the native menu shell each frame while it is open.

The HUD hook is a full replacement (`Cg_DrawHudElements` is pointed at Race's
implementation rather than chained), and the input path is wrapped:
`cge.Move` goes through the double-jump macro before common's `Cg_Move`.

## Message and state flow

Everything authoritative arrives on three channels:

1. **Unicast Race messages** (`SV_CMD_RACE_*` from `g_types.h`): replay
   state/telemetry/projectiles, raceline chunks, map browser pages and
   details, the finish report, split events, weapon-tuning sync, and the
   admin/profile challenge flow. Each subsystem parses its own messages;
   malformed payloads are warned-and-cleared (or hard-error on stream
   desync), never half-applied.
2. **Config strings**: physics identity, leaderboard top-15, checkpoint
   total, vote info and vote menu, weapon-tuning status, settings status,
   hook pull speed.
3. **Player stats** (`STAT_RACE_*`): mode, run state, elapsed time (two
   16-bit halves), checkpoint count, invalid flags, input flags, PB and WR
   times, admin capabilities, vote flags. These are read fresh from the
   frame every draw, never cached.

A spectator chasing a player receives the chased player's run stats, so
watching someone shows their live timer.

## Prediction

Race replaces common's `cg_predict.c`. The loop seeds from the authoritative
snapshot (parameters are *never* taken from local cvars), replays pending
commands through the shared `bg_pmove.c`, and routes every trace through the
module so conditional barriers clip identically to the server (see
[MOVEMENT_AND_PHYSICS.md](MOVEMENT_AND_PHYSICS.md) and
[MAPS_AND_ENTITIES.md](MAPS_AND_ENTITIES.md)). Prediction fails closed: it is
disabled during replay playback, when the hook pull speed config string is
invalid, or whenever the physics identity is not yet proven synchronized.

## Score, roster, leaderboard

`cg_score_model.c` reassembles the chunked score stream into validated
snapshots (grouped Race / Practice / Spectator) — only complete snapshots are
ever published to the renderer. `cg_score.c` renders the scoreboard: map
header with physics preset, your standing line with gaps to the record and
the player above you, the persistent 15-slot leaderboard (decoded from the
leaderboard config string), and roster bands with per-player ping-quality
chips that shed detail gracefully as player counts grow. The same roster
snapshot feeds the Home menu screen.

## Finish presentation

The finish report banner decodes the server's versioned finish report and
classifies it with documented precedence — invalid run > practice > completed
but unpublished > world record > personal best — then shows the hero time, PB
and WR delta chips, and live keybind hints (restart, watch replay) for 10
seconds. The classification math lives in a pure header
(`cg_race_finish_report_math.h`) so it is testable without the engine.

## Entity handling

Race's `cg_entity.c` override filters entities two ways: conditional barriers
are prediction-filtered (drawn only while blocking), and — when
`cg_show_jumpers` is off — other players plus their attributed projectiles,
trails, sounds, and events are hidden entirely, turning a busy server into a
solo experience.

## Profiles (client half)

`cg_race_profiles.c` answers the server's profile request: it stores the
enrollment secret in a per-server archived cvar (the name is suffixed with a
hash of the server address, so credentials never leak across servers),
answers challenges with the derived proof, and offers `race_profile_reset`.
See [RECORDS_AND_PERSISTENCE.md](RECORDS_AND_PERSISTENCE.md) for the server
half.

## Miscellany

- `cg_media.c` is stock media loading plus a Race tail (barrier, marker, and
  replay media).
- `cg_discord.c` reports "Race — <map title>" through Discord Rich Presence,
  with the join address sanitized against command injection.
- `cg_race_client_file.c` is the client-side verified-write primitive
  (write, close, read back, compare) used by practice-marker persistence —
  the CGAME import has no rename/flush, so this is the honest alternative.
- `cg_module_compat.h`, like its GAME twin, declares module-private hooks
  that common does not expose; only the Race project files select the
  implementations, so nothing leaks into other modules.

## Interactions

Every other Race system has its presentation end here; see
[UI_AND_HUD.md](UI_AND_HUD.md) for the HUD elements, training tools, and
native menu, [REPLAY_SYSTEM.md](REPLAY_SYSTEM.md) for replay presentation,
and [VOTING_AND_ADMIN.md](VOTING_AND_ADMIN.md) for vote/admin surfaces.

## Main source

```text
src/cgame/race/cg_module.c            src/cgame/race/cg_score.c
src/cgame/race/cg_main.c              src/cgame/race/cg_score_model.c
src/cgame/race/cg_predict.c           src/cgame/race/cg_race_finish_report.c
src/cgame/race/cg_entity.c            src/cgame/race/cg_race_map_browser.c
src/cgame/race/cg_race_message.c      src/cgame/race/cg_race_profiles.c
src/cgame/race/cg_race_presentation.c src/cgame/race/cg_race_client_file.c
```
