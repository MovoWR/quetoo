# Quetoo Race — Mod Overview

**Quetoo Race** is a movement/racing game mode for the
[Quetoo](https://github.com/jdolan/quetoo) engine, delivered as a paired
GAME/CGAME module set (`+set game race`). Players run courses of ordered
checkpoints against the clock under selectable movement physics; valid
finishes become replay-backed leaderboard records.

This directory is a **developer overview**: enough to understand what the mod
consists of, how it is structured, which side owns what, and where the
implementation lives — without reading the whole source tree. It is not user
documentation or an install guide.

## Scope of the mod

- Timed runs with strict checkpoint ordering, optional splits and stages,
  and Race / Practice / Spectator modes.
- Four selectable movement physics presets across two families, with
  bit-identical server movement and client prediction.
- Replay-backed records: every ranked time references a verifiable replay
  file, watchable as full playback or as a "raceline" ghost trail.
- Cryptographic player identity (per-server profiles), file-backed
  persistence with atomic commits.
- A typed settings system with server and per-map scopes, live weapon
  tuning, majority voting, and credentialed role-based administration.
- A complete native menu (ObjectivelyMVC), a purpose-built HUD, and
  movement-training tools (strafe helper, input/jump viewers, markers).

## Structure at a glance

```text
Quetoo Race
│
├── GAME  (src/game/race/ — authoritative)
│   ├── runs, timing, modes            race.c, race_logic.c, race_modes.c
│   ├── course entities & barriers     race_trigger.c, race_clip.c
│   ├── physics identity & runtime     race_physics*.c, race_pmove_policy.c
│   ├── movement (shared with CGAME)   bg_pmove.c
│   ├── weapon movement & tuning       race_weapon_*.c, g_ballistics.c
│   ├── profiles, records, map state   race_profile*.c, race_leaderboard*.c,
│   │                                  race_map_state*.c, race_persistence.c
│   ├── replay record/store/playback   race_replay_*.c
│   ├── settings                       race_settings*.c
│   ├── voting                         race_vote*.c
│   └── administration & kick broker   race_admin*.c, race_kick_broker.c
│
└── CGAME  (src/cgame/race/ — prediction + presentation)
    ├── prediction & physics sync      cg_predict.c, cg_race_physics.c
    ├── barrier prediction             cg_race_barriers.c
    ├── HUD & finish presentation      cg_race_hud.c, cg_race_finish_report.c
    ├── scoreboard / roster            cg_score.c, cg_score_model.c
    ├── replay & raceline display      cg_race_replay.c
    ├── training tools                 cg_strafe_helper.c, cg_input_viewer.c,
    │                                  cg_jump_viewer.c, cg_race_*markers.c
    └── native Race UI                 ui/ (main, home, play, controls,
                                       settings, maps, voting, credits, admin)
```

## Who owns what

**GAME decides, CGAME shows.** The server owns the run timer, finish
validity, records, physics identity, votes, settings, and admin authority.
CGAME mirrors that state and predicts movement locally — using the *same
compiled movement source* as the server — but never produces an authoritative
result. The full ownership table is in
[ARCHITECTURE.md](ARCHITECTURE.md).

## The documents

| Document | Covers |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | How Race is put together: modules, hooks, overrides, wire manifest, ownership. |
| [SYSTEM_INDEX.md](SYSTEM_INDEX.md) | One-line map from every system to its source files. |
| [GAMEPLAY_AND_RUNS.md](GAMEPLAY_AND_RUNS.md) | Runs, modes, timing, finish → validation → record → publication. |
| [MOVEMENT_AND_PHYSICS.md](MOVEMENT_AND_PHYSICS.md) | Physics presets, the shared mover, prediction, weapon–movement. |
| [MAPS_AND_ENTITIES.md](MAPS_AND_ENTITIES.md) | Course entities, conditional barriers, map catalog, map browser. |
| [RECORDS_AND_PERSISTENCE.md](RECORDS_AND_PERSISTENCE.md) | Profiles, map state, leaderboards, the safe-write storage layer. |
| [REPLAY_SYSTEM.md](REPLAY_SYSTEM.md) | Recording, the QRPL format, storage, transport, playback, racelines. |
| [SETTINGS_SYSTEM.md](SETTINGS_SYSTEM.md) | The typed settings catalog, scopes, persistence, and weapon tuning. |
| [VOTING_AND_ADMIN.md](VOTING_AND_ADMIN.md) | Vote lifecycle and types; admin accounts, auth, and the kick broker. |
| [CGAME_AND_PRESENTATION.md](CGAME_AND_PRESENTATION.md) | The client half: message parsing, mirrors, prediction, scoreboard. |
| [UI_AND_HUD.md](UI_AND_HUD.md) | The native menu shell and screens, HUD elements, training tools. |
| [CVAR_REFERENCE.md](CVAR_REFERENCE.md) | Every Race cvar, grouped, with defaults and plain-language purpose. |

**Where to start:** [ARCHITECTURE.md](ARCHITECTURE.md), then
[GAMEPLAY_AND_RUNS.md](GAMEPLAY_AND_RUNS.md). Use
[SYSTEM_INDEX.md](SYSTEM_INDEX.md) whenever you need to find a feature's
implementation.

## Deeper reference

This overview intentionally stays at the systems level. The repository
carries exhaustive references alongside the source — notably
`doc/race-override-ledger.md` (the boundary between Race and stock code),
`doc/race-physics.md` (the physics contract in prose),
`doc/race-voting-system.md`, `doc/race-configuration.md`, and
`doc/RACE_CONFIGURATION_TECHNICAL_REFERENCE.md` — for implementation-level
detail beyond what these pages describe.
