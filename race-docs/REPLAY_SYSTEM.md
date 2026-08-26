# Replay System

## Purpose

Every ranked Race run is backed by a replay. The replay system records the run
as it happens, seals it into a verifiable file, ties it to the leaderboard
record it proves, and lets players watch stored runs — either as full playback
or as a "raceline" ghost trail to race against.

It helps to keep six distinct jobs apart, because the source does:

| Job | Where |
|---|---|
| Recording | `race_replay_service.c`, `race_replay_record.c`, `race_projectile_observer.c` |
| File format (QRPL) | `race_replay_format.c` |
| Persistence | `race_replay_store.c` |
| Transport to clients | `race_replay_transport.c` (+ wire commands in `g_types.h`) |
| Playback | `race_replay_playback_service.c`, `race_replay_playback.c` |
| Presentation | `src/cgame/race/cg_race_replay.c` |

## Ownership

GAME owns recording, storage, and playback — the server never sends a replay
file to a client. CGAME owns only presentation: it receives small derived
messages (pose, telemetry, projectile events, raceline points) and draws them.
The format, transport framing, and raceline math live in shared sources
compiled into both modules so encoder and decoder cannot drift.

## Recording

Recording starts when a run starts, but only for a run that could actually be
ranked: Race mode (not Practice), a ready authenticated profile, and a
supported physics family. Every server tick (40 Hz, 25 ms) the service captures
a sample: the full movement state (`pm_state_t` plus parameters), the player's
stats and inventory arrays, the input flags, and a strafe-helper sample.
Buffers grow up to 24 000 frames (10 minutes); a server-wide 128 MiB recording
budget bounds total memory. If capture fails or the cap is hit, the run is
marked invalid (`RACE_INVALID_REPLAY_CAPACITY`) rather than silently losing
data.

**Projectile events.** Race's ballistics override (`g_ballistics.c`) reports
rocket and hyperblaster spawn/impact/despawn through a module-private observer
seam (`race_projectile_compat.h` — deliberately not a public engine API). The
replay service records these as paired events with origin, velocity, and impact
normal. They are presentation data: on playback the client re-derives each
projectile's flight by simple extrapolation from its spawn, so replays show
rockets and bolts without re-simulating physics.

```text
run starts (Race mode, ready profile, rankable physics)
   ↓
40 Hz samples + projectile events recorded
   ↓
finish accepted
   ↓
replay finalized and validated (times must reconcile exactly)
   ↓
two-phase publication: replay file committed, then the record
committed pointing at it — replay removed again if the record fails
```

## The QRPL format

A replay file is a 64-byte header (`QRPL` magic, version 1), the map and player
name strings, and a deflate-compressed payload holding a deduplicated
movement-parameters table, the fixed-size frames, and the projectile events.
Reliability features worth knowing about:

- **Two CRC32s** — one over the raw payload, one over the header + metadata.
- **Two version knobs** — the container version and a separate frame schema,
  so parse failures distinguish "unknown file" from "unsupported frame layout".
- **Strict validation** — monotone frame times that must sum to the recorded
  elapsed time within one tick, canonical map/player identity, and fully
  paired projectile events. Files over 16 MiB are rejected.

## Replay identity

A replay's 64-bit ID is content-derived, not a counter: the high half is a
CRC32 over the identity tuple (player, map, mode, physics, elapsed time, frame
count, name) and the low half is the payload CRC32. Identical runs produce
identical IDs, which makes re-committing the same replay idempotent.
Leaderboard records store this ID (`replay_id`), and every consumer
cross-checks record ↔ replay by ID, elapsed time, *and* player identity before
trusting either.

## Storage

Files live at `replays/<ruleset>/<encoded-map>/replay-<16-hex-id>.ghost`,
written through the shared candidate-then-promote persistence layer with a full
reload-and-compare verification at every step (see
[RECORDS_AND_PERSISTENCE.md](RECORDS_AND_PERSISTENCE.md)). A byte-different
file already at the same ID is a hard collision error. There is no automatic
pruning; removal exists only as rollback when publication fails. After a map's
state loads, the map-state service re-verifies one referenced replay per frame
in the background and disables the leaderboard if any fails.

## Transport

Four unicast server-to-client messages (`SV_CMD_RACE_REPLAY_STATE`,
`..._TELEMETRY`, `..._PROJECTILES`, `SV_CMD_RACE_RACELINE`), each a small
length-prefixed payload capped at 255 bytes. The state message (sent every
25 ms during playback) carries flags, speed, playhead, identity, and a short
forward window of poses for smooth client interpolation; the telemetry message
piggybacks the same sequence number and carries movement state, input flags,
and strafe data — the client only applies telemetry whose generation, sequence,
and playhead match the current state, so HUD readouts can never desync from the
pose. Projectile messages stream events as the playhead crosses them, and any
seek triggers a reset-plus-snapshot rebuild (snapshots restore state without
re-firing explosion effects). Racelines arrive as a begin/chunk/end sequence,
one chunk per server frame, with strict continuity checks; any violation drops
the partial line.

## Playback

There is no ghost entity and no bot. Playback rewrites the *viewing player's
own* player state each frame from the recorded samples — a first-person,
server-authoritative re-drive — while normal gameplay is disabled for that
client. Sessions are per-client (at most 8 loaded server-wide) and end on
completion, `+attack`, starting a run, map change, disconnect, or a 5-minute
pause timeout.

```text
Replay selection:   pb (default) | wr | rank 1–15
Playback controls:  pause resume restart back forward
                    step step_forward step_back slower faster
                    speed 0.25|0.5|1|2|4
```

Seeks jump ±5 s and pause; steps move exactly one recorded sample. Controls
are rate-limited, and every discontinuity flags the client to rebuild its
projectile view.

## Raceline

A raceline is a stored replay reduced to a decimated position-vs-time path (up
to 512 points) and drawn in the world as a glowing trail. The server decimates
and streams it; the client renders a 3-second trailing window whose head
follows either the active replay's playhead or — the interesting case — the
viewer's *own live run timer*, which turns any stored run into a pace ghost.
`race <selector>` starts a raceline and a live run together.

## Command surface

```text
replay [pb|wr|<rank>|stop|off|<control>...]   load / control playback
replay_control <pause|restart|back|forward|step_back|step_forward|slower|faster>
replay_cancel                                 exit playback
raceline [pb|wr|<rank>|off|stop]              show/hide a raceline (default wr)
race <selector> / race off                    raceline + live run together
```

## CGAME presentation

`cg_race_replay.c` keeps exactly one client cache (the shared transport cache)
plus a projectile table. It draws the replay HUD (identity tag, playhead,
speed badge, scrub timeline, live key-hint row resolved from actual binds),
extrapolated projectiles with models/sounds/trails, and the raceline beams.
Replay telemetry also feeds the input viewer, jump viewer, and training
overlays (see [UI_AND_HUD.md](UI_AND_HUD.md)).

## Interactions

- **Records** — publication is two-phase with map state; a record without a
  verifiable replay is never published (see
  [RECORDS_AND_PERSISTENCE.md](RECORDS_AND_PERSISTENCE.md)).
- **Physics** — the ruleset names the storage directory; unrankable physics
  demotes recording to memory-only validation.
- **Weapon tuning** — non-stock live tuning marks the server unrankable, which
  blocks replay-backed publication and playback starts.
- **Training tools** — input viewer, jump viewer, and training overlays consume
  replay telemetry during playback.

The replay subsystem registers no cvars; all limits are compile-time constants.

## Main source

```text
src/game/race/race_replay_service.c        src/game/race/race_replay_store.c
src/game/race/race_replay_record.c         src/game/race/race_replay_transport.c
src/game/race/race_replay_format.c         src/game/race/race_replay_playback.c
src/game/race/race_projectile_observer.c   src/game/race/race_replay_playback_service.c
src/game/race/race_projectile_compat.h     src/cgame/race/cg_race_replay.c
```
