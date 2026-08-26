# Maps and Entities

## Purpose

This document covers what a Race map *is* to the mod: the entities a mapper
places to define a course, the conditional barriers that shape routes, the
per-map catalog and configuration, and the map browser that exposes it all to
players.

## Ownership

GAME owns entity spawning, course validation, and all authoritative barrier
collision. CGAME independently reads the same BSP data to predict barrier
collision and to draw markers and gates — using the *same shared collision
kernel*, so client and server can never disagree about whether a wall is
solid.

## Course entities

`race_trigger.c` claims exactly seven classnames (dispatched after all stock
entity classes, from Race's `g_entity.c` override):

### Brush triggers

| Classname | Keys | Role |
|---|---|---|
| `trigger_race_start` | `start_mode` (`touch`/`exit`/`jump`), `wait`, `target`, `message` | Starts a run — on contact, on leaving the zone, or on jumping inside it. |
| `trigger_race_cp` | `cp` (1–64) | Numbered checkpoint; must be taken strictly in order. |
| `trigger_race_finish` | `wait`, `target`, `message` | Finish line; only accepts a run with every checkpoint taken. |
| `trigger_race_split` | `split` (1–64), `label` | Analytical timing point — enriches records, never gates a finish. |
| `trigger_race_stage` | `stage` (2–64), `restart_target`, `label` | Stage boundary; `restart_target` names an `info_notnull` respawn anchor for Practice `restart_stage`. |

All triggers share per-client touch debouncing (`wait`, default 0.5 s) and can
fire ordinary `target`/`message` behavior. Checkpoints, splits, and stages
each build a bitmask that must be exactly contiguous (1..N; stages 2..N, since
stage 1 is implicit) — a gap, duplicate, or out-of-range number marks that
layer malformed. A course is **valid** when its checkpoints are contiguous and
it has at least one finish; splits and stages validate independently and only
disable their own features. Maps with *no* start trigger auto-start runs on
first movement input instead.

Each stage's restart anchor is verified at level load: it must resolve to
exactly one `info_notnull`, and a player-hull trace at its origin must not
start inside solid. Split geometry and labels are hashed (FNV-1a) into a
`split_layout` identity stored with records, so split comparisons only happen
between runs of the same layout.

### Conditional barriers

| Classname | Keys | Role |
|---|---|---|
| `func_race_checkpoint_gate` | `cp`, `mode` (`atleast`/`exact`), `invert` | A clip brush that blocks until the player has reached (or exactly has, or inversely) checkpoint N. |
| `func_race_oneway_wall` | `angles` (yaw only) | A clip brush passable only in one horizontal direction. |

Barriers must be inline models whose every brush is pure `CONTENTS_PLAYER_CLIP`,
each with a unique model index; violations disable the barrier and make the
course non-rankable. Checkpoint gates must also sit at least 1024 units from
their matching checkpoint trigger, so a gate can never overlap the trigger
that opens it. At level load barriers are sorted by model index and given
stable IDs — the ID doubles as the bit index in each client's one-way latch
mask.

**How blocking works.** The shared kernel in `race_clip.c` implements one rule
used verbatim on both sides:

- A checkpoint gate blocks unless the player's reached-checkpoint count
  satisfies the gate's condition.
- A one-way wall lets you pass when moving in its allowed direction, then
  *latches* per-client so you remain non-colliding while your hull still
  overlaps it (you can't get stuck inside); leaving it clears the latch.
  Approaching from the wrong side blocks.

```text
GAME:  G_Module_TraceMovement enumerates entities, asks
       Race_ClipBarrierBlocks, clips with the engine trace.
CGAME: cg_race_barriers.c scans the BSP for the same brushes, runs the
       identical Race_ClipBoxToBrushes / Race_ClipBarrierBlocks code
       against a client-local latch state that resets at the start of
       every re-prediction batch.
```

AI lookahead saves and restores latch state around speculative moves so
probing never mutates real state. Barriers carry the `EF_RACE_GATE` effect so
the client can identify them; barriers authored with `visual 1` are drawn as
colored boxes *only while they currently block*.

## Map catalog and per-map configuration

The server's map list file (cvar `sv_map_list`, default `maps.lst`) is the
single source for both the catalog and per-map overrides:

- **Catalog** (`race_map_catalog.c`) — up to 256 brace-delimited rows with
  `name`, `message` (title), `author`, `description`, `tags`, `difficulty`
  (1–10), `time_limit`. Re-parsed on demand.
- **Per-map properties** (`race_map_properties.c`) — any additional key in a
  row that matches a *map-overridable Race setting* (gravity, weapons, voting
  limits, finish cue, and so on) overrides the global value for that map. The
  settings service applies these at map load; admin `mset`/`mclear` commands
  edit the file through a strict parse → edit → revalidate cycle (see
  [SETTINGS_SYSTEM.md](SETTINGS_SYSTEM.md)).

Map names are canonicalized (lowercased, `maps/` prefix and `.bsp` suffix
stripped, restricted character set) everywhere — catalog, state files, replay
paths — and hex-encoded when used as filesystem components.

## Map state

Each *(map, physics ruleset)* pair has one state file holding that map's
leaderboard (see [RECORDS_AND_PERSISTENCE.md](RECORDS_AND_PERSISTENCE.md) for
the format and lifecycle). There are no other per-map server statistics.

## Map browser

The map browser is a request/response flow over unicast wire messages:

```text
client command  maps_ui / mapinfo_ui / maptimes
        ↓
GAME    race_map_browser_service.c filters the catalog, loads map-state
        summaries (world record, personal best, rank, total runs)
        ↓
wire    SV_CMD_RACE_MAP_BROWSER (paged rows, 10/page)
        SV_CMD_RACE_MAP_BROWSER_DETAIL (one map: record holder, top times,
        your rank, run count)
        ↓
CGAME   cg_race_map_browser.c decodes and refreshes the Maps UI screen
```

Filtering is substring match on name/title/author, with an optional
"personal best" scope that only lists maps you have a ranked time on. All
outgoing strings are sanitized against the wire delimiters.

## Course presentation (CGAME)

- `cg_race_markers.c` reads the BSP directly (no server data) and draws 3D
  markers over start (green arrow), checkpoints (cyan numbers), and finish
  (orange glyph), toggled by `cg_race_markers`.
- `cg_race_practice_markers.c` is a purely client-local annotation system: up
  to 128 player-placed markers per map (point/takeoff/landing/aim), persisted
  in per-map client files with verified writes. The server never sees them.

## Interactions

- **Runs** — triggers drive the run state machine
  (see [GAMEPLAY_AND_RUNS.md](GAMEPLAY_AND_RUNS.md)).
- **Prediction** — barrier behavior is part of movement prediction
  (see [MOVEMENT_AND_PHYSICS.md](MOVEMENT_AND_PHYSICS.md)).
- **Settings** — per-map overrides ride in the map list
  (see [SETTINGS_SYSTEM.md](SETTINGS_SYSTEM.md)).
- **Voting** — map votes and nominations draw on the same catalog
  (see [VOTING_AND_ADMIN.md](VOTING_AND_ADMIN.md)).

## Main source

```text
src/game/race/race_trigger.c            src/game/race/race_map_browser_service.c
src/game/race/race_clip.c               src/game/race/race_map_browser_wire.c
src/game/race/race_map_catalog.c        src/cgame/race/cg_race_barriers.c
src/game/race/race_map_properties.c     src/cgame/race/cg_race_markers.c
src/game/race/g_entity.c                src/cgame/race/cg_race_practice_markers.c
share/race/maps.lst                     src/cgame/race/cg_race_map_browser.c
```
