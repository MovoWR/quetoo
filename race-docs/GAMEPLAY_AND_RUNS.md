# Gameplay and Runs

## Purpose

This is the server-side heart of the mod: what a "run" is, how it starts,
progresses, and finishes, and how a finish becomes (or does not become) a
published record.

## Ownership

Entirely GAME. The server owns the timer, the run state machine, run validity,
and the record decision. CGAME only receives run state through player stats and
the finish report and presents it (see
[CGAME_AND_PRESENTATION.md](CGAME_AND_PRESENTATION.md)).

## Main components

```text
race.c              Lifecycle glue: hooks, start/reset/finish, stats/score publication.
race_logic.c        Pure run + course state machine (no engine calls; unit-testable).
race_modes.c        Race / Practice / Spectator mode policy, spawn preparation, noclip.
race_trigger.c      Race map entities: start/checkpoint/split/stage/finish, barriers.
race_finish_report.c  The versioned finish-report wire record sent to the client.
race_publication.c  The replay + record two-phase commit transaction.
race_actions.c      Server-side action registration (the private kick-commit stage).
race_cmds.c         The single client command dispatcher.
g_module.c          The GAME module contract implementation (init, frame, per-client hooks).
```

## Lifecycle

`G_Module_Init` initializes every Race service (physics, training, profiles,
map browser, map state, replay, playback, settings, admin, weapon tuning,
voting, vote menu) and installs Race's chainable hooks over common GAME:
`G_ConfigureLevel`, `G_InitMedia`, `G_InhibitItem`, `G_InitItem`,
`G_TossInventory`, plus a full replacement of `G_ClientKill` and the
hook-eligibility predicate. Each level load runs `Race_ConfigureLevel`, which
resolves settings for the map, lets common spawn entities, validates the
course, configures barriers and stage anchors, and hands the map to the
physics, tuning, map-state, replay, and voting services in a fixed order.
`G_Module_Frame` then advances the map-state, replay, playback, and vote
services every server tick.

## Modes

Three modes (`race_mode_t`): **Race**, **Practice**, **Spectator**. Mode is a
Race-owned concept deliberately unrelated to team IDs.

| | Race | Practice |
|---|---|---|
| Records | Valid finishes may publish | Never submitted |
| Invalidation | Applies | Not used — Practice is excluded outright |
| Grapple hook | Denied | Allowed |
| Noclip | Only alone or with cheats | Always allowed |
| Stored spawn (`store` / `kill`) | Unavailable | Available |
| `restart_stage` | Refused | Available |

Players switch with the `mode` command (`race`, `practice`, `spectator`).
Every mode change resets the run, clears armed triggers and one-way latches,
and respawns the client. If live weapon tuning has made the server unrankable,
requests for Race mode are forced to Practice.

## The run state machine

A run is `IDLE → ACTIVE → FINISHED` (`race_run_state_t`), owned per client in
`cl->race_run`. All timing is in milliseconds from `g_level.time` — the server
tick clock — stored as deltas from the run's start time. The elapsed time
reaches the HUD as two 16-bit stat halves.

**Starting.** Maps with a `trigger_race_start` zone start runs through it, in
one of three modes: `touch` (start on contact), `exit` (arm on contact, start
when leaving the zone), or `jump` (arm on contact, start on a jump input
inside it). Maps with no start zone auto-start on the first movement input.
Starting snapshots the player's speed and, in Race mode with a ready profile,
begins replay recording.

**Progress.** Checkpoints must be taken strictly in order (checkpoint N+1 only
after N). Two optional overlays exist:

- **Splits** — analytical timing points, also strictly sequential. They never
  gate a finish; they enrich records with comparison data.
- **Stages** — numbered sections (2..64; stage 1 is implicit) with designated
  restart anchors. In Practice, `restart_stage` respawns you at the last
  stage boundary you crossed.

**Finishing vs validating vs publishing.** These are three separate decisions:

```text
finish detected      trigger_race_finish touched while ACTIVE
        ↓
finish accepted      Race_Run_Finish: every checkpoint taken, exactly
        ↓
run validity         invalid_flags == 0  (noclip use or replay-capacity
        ↓            failure invalidates; the run keeps going, ranking doesn't)
record decision      map-state service evaluates the candidate: Race mode at
        ↓            start and finish, authenticated profile, rankable physics
        ↓            and weapon tuning, no noclip — would it beat the PB?
replay-backed        two-phase commit: replay file first, then the record
publication          pointing at it; the replay is rolled back if the record
                     commit fails (race_publication.c)
```

An accepted finish always prints times and speeds to the player; a Practice
finish or an invalid run says explicitly that nothing was submitted.
Successful publication broadcasts "Personal best" / "First completion" /
"World record" with the appropriate sound cue, and a generic finish cue
(settings-controlled) plays otherwise.

**The finish report.** Every accepted finish also unicasts
`SV_CMD_RACE_FINISH_REPORT`: a small versioned, CRC-checked-by-construction
wire record carrying mode, validity flags, elapsed time, previous PB, world
record, per-checkpoint times, and start/end/top/average speeds. The same
validation routine runs on encode (GAME) and decode (CGAME), so a report that
claims publication must be internally consistent — e.g. a committed record
must actually beat the previous PB.

## Player-state policy

Race removes player-versus-player interaction entirely:

- **Damage** — Race's damage filter drops all damage *and* knockback between
  different clients. Self-damage keeps its knockback (rocket jumps work) but
  costs no health.
- **Collision** — players do not collide with each other (the movement clip
  mask drops other players); world and clip brushes still apply.
- **Spawning** — no telefrag kill-box; spawn position can be overridden by a
  pending stage restart or, in Practice, a stored spawn (`store` saves your
  position, `kill` returns to it).
- **Suicide** — `kill` is fully replaced: no death or gibs, just a rate-limited
  reset-and-respawn (and it doubles as the replay-playback exit key).
- **Weapons** — the Blaster is removed at the item level; whether weapons exist
  at all is a Race setting; ammo is unlimited in deathmatch gameplay.
- **Noclip** — always allowed in Practice; in Race mode it requires playing
  alone or cheats, and using it invalidates the current run.

## Command surface

One dispatcher (`race_cmds.c`) routes all client commands, giving the profile,
weapon-tuning, settings, vote, and map-browser services first claim. The
player-facing groups:

```text
Mode & run:     mode, store, restart_stage, race, race status
Replay:         replay, replay_control, replay_cancel, raceline, race off
Spectating:     chase_toggle
Voting:         nominate, race vote
Admin:          radmin, race admin, race admin_logout
Information:    race help, race mapstate
```

## Interactions

- **Replay** — recording starts/stops with the run; publication is joint (see
  [REPLAY_SYSTEM.md](REPLAY_SYSTEM.md)).
- **Records** — the map-state service makes the accept/reject decision (see
  [RECORDS_AND_PERSISTENCE.md](RECORDS_AND_PERSISTENCE.md)).
- **Physics / weapon tuning** — both can mark the server unrankable, which
  forces Practice and blocks publication.
- **Entities** — the course itself is built from Race trigger entities (see
  [MAPS_AND_ENTITIES.md](MAPS_AND_ENTITIES.md)).

## Main source

```text
src/game/race/race.c              src/game/race/race_finish_report.c
src/game/race/race_logic.c        src/game/race/race_publication.c
src/game/race/race_modes.c        src/game/race/race_cmds.c
src/game/race/race_trigger.c      src/game/race/g_module.c
src/game/race/race_actions.c      src/game/race/g_types.h
```
