# Movement and Physics

## Purpose

Race's defining feature is precise, selectable movement physics. The mod
supports several movement rulesets ("presets"), keeps server and client
movement bit-for-bit identical so prediction and replays are trustworthy, and
ties every leaderboard to the exact physics it was set under.

## Ownership

Movement is **shared code with server authority**. The server runs the real
movement and owns the physics identity; the client runs the *same compiled
source* for prediction and refuses to predict at all until it has proven it is
synchronized with the server.

## Main components

```text
bg_pmove.c              The single shared mover, compiled into BOTH modules.
race_physics.c          Physics identity catalog: families, presets, wire codec.
race_pmove_policy.c     Per-preset locomotion policy records (hulls, jumps, steps).
race_physics_service.c  GAME runtime: selection, parameter enforcement, publishing.
race_clip.c             Shared conditional-brush collision kernel (GAME + CGAME).
race_weapon_movement.c  GAME fire-policy boundary for movement-relevant weapons.
g_ballistics.c          Projectile construction + tuning application (override).
cg_predict.c            CGAME prediction loop (Race override of common's).
cg_race_physics.c       CGAME identity mirror; gates prediction on sync.
cg_race_double_jump.c   Client-side double-jump input macro.
```

## Families and presets

Two families and four presets, cataloged in `race_physics.c`:

| Preset key | Family | Name | Movement parameters |
|---|---|---|---|
| `quetoo-common-v1` | `quetoo` | Current common Quetoo | live movement cvars (no fixed vector) |
| `q2-v1` | `q2` | Quake II | fixed immutable vector |
| `quetoo-fix-v1` | `q2` | Legacy Quetoo Fix hybrid | fixed immutable vector |
| `dp2-v1` | `q2` | Digital Paint: Paintball 2 | fixed vector (shares Q2's) |

The server selects a preset with the `g_race_physics` cvar (default `q2`;
latched, so changes apply at the next map load). Accepted selectors are the
four preset keys plus the aliases `q2`, `quake2`, and `dp2`. Anything else
**fails closed**: the server refuses to start rather than silently defaulting.
(The voting system separately accepts a few legacy vote spellings such as
`q2pro`, which it maps to `q2-v1` before reaching the catalog.)

Q2-family presets also carry a snap mode (`g_q2_snap_mode`: 0 off, 1 nearest
1/8 unit, 2 truncate toward zero — the default), which becomes part of the
ruleset identity: leaderboards for `q2-v1` with snapping off live under
`q2-v1-snap-off`, and so on.

A preset bundles: a complete fixed movement-parameter vector (gravity,
per-medium acceleration/friction/speed caps, jump speeds), a **locomotion
policy** (hull heights, ramp behavior, jump/ground/step variants — this is how
DP2's fixed 270-impulse jump or Quetoo Fix's trick-jump probe are expressed),
a weapon-movement profile, and rankability. The policy records live in
`race_pmove_policy.c` and describe *only* differences already implemented in
`bg_pmove.c` — they are data, not behavior.

## Physics identity and synchronization

The active physics is published to every client as a versioned, human-readable
config string (`CS_RACE_PHYSICS_CONFIG`), e.g. `v2\q2\dp2-v1\truncate` —
semantic keys only, never enum ordinals. On the client, `cg_race_physics.c`
decodes it and requires three things before prediction may run: a valid
decoded identity, a valid server snapshot, and byte-equality between the
preset's fixed parameter vector and the parameters in the snapshot.
**Prediction fails closed** — until all three hold, the client renders pure
server state instead of mispredicting.

On the server side, the physics service chains into `G_PrepareMove` and
overwrites the movement parameters with the fixed vector on *every* move, so
live cvar tinkering cannot drift a named preset. "Rankable" additionally
requires that the live vector still equals the preset's descriptor — if it
does not, records and replays are disabled.

## Why `bg_pmove.c` is shared, and what keeps it in sync

`src/game/race/bg_pmove.c` is one source file compiled into both the GAME and
CGAME libraries (autotools `vpath`, and the Visual Studio/Xcode projects
include the same path — the CGAME project explicitly removes common's copy).
It is not an interface or a port: encoder and decoder of movement are the same
translation unit.

Determinism is protected at three levels:

- **Build** — both modules compile with `-ffp-contract=off` so fused
  multiply-add cannot make GAME and CGAME round differently.
- **Runtime** — the prediction gate above; plus the mover asserts its bound
  config/preset/policy are consistent, and the server hard-errors if a bot's
  first command completes with drifted parameters.
- **Tests** — dedicated pmove tests run the shared source under both module
  configurations (`src/tests/check_race_pmove*.c`) and assert the GAME and
  CGAME clip masks are identical.

## Player bounds and collision policy

Player hull tops are policy-controlled: Q2 and DP2 players are 32 units tall
(4 ducked), Quetoo Fix 32/6, Quetoo common the stock 36/6. AI navigation reads
the same policy-aware bounds.

Race players **never collide with each other**: the movement clip mask drops
other players while keeping world and clip brushes
(`Race_MovementClipMask`), mirrored exactly in CGAME prediction. Projectile
and hitscan masks are deliberately unaffected.

`race_clip.c` is a shared collision kernel — a careful transcription of the
engine's box-to-brush trace, kept free of GAME/CGAME entity types so the
identical source compiles into both modules. It implements conditional
barriers (checkpoint gates and one-way walls; see
[MAPS_AND_ENTITIES.md](MAPS_AND_ENTITIES.md)) with one shared blocking rule,
so a barrier can never be solid on the server but passable in prediction.

## AI

Bots are ordinary clients: they run through the same `G_ClientThink` →
`Pm_Move` path with the same forced parameter vector, and their speculative
lookahead moves save and restore one-way-wall latch state so probing a barrier
never mutates authoritative state.

## Weapon–movement integration

Weapons matter to Race movement (rocket jumps, hyperblaster climbs), so Race
wraps the movement-relevant fire paths without modifying common weapon code:

- `race_weapon_movement.c` swaps each relevant item's fire callback for a thin
  scope that delegates to the *unmodified* common callback, then stamps the
  spawned projectile with its physics preset, weapon profile, fire kind, and
  any live tuning values (see the weapon tuning section of
  [SETTINGS_SYSTEM.md](SETTINGS_SYSTEM.md)).
- The damage policy makes self-damage free but keeps its knockback, so rocket
  jumping works without health cost; a per-projectile self-knockback multiplier
  can further scale it.
- The hyperblaster "climb" is generalized from stock's Z-only bump into a full
  3D vector (wall push, up component, velocity boost), computed by finite-
  checked math in `Race_WeaponMovement_HyperClimbDelta` and parameterized by
  live tuning.

Covered fire paths: hand grenade, grenade, Quake grenade, rocket, Quake
rocket, hyperblaster. Everything else keeps stock behavior.

## Double jump

`cg_race_double_jump.c` is a pure client-side input macro (`+double_jump`)
that turns one held key into two jump presses with a deliberate one-command
gap, so the server sees a genuine second jump edge. It commits phases exactly
once per outgoing command and only *previews* on re-predicted commands, so
prediction can never consume an input phase. There is no server component.

## Interactions

- **Records/replays** — the ruleset string names the storage namespace; every
  leaderboard is per-physics (see
  [RECORDS_AND_PERSISTENCE.md](RECORDS_AND_PERSISTENCE.md)).
- **Voting** — a passed physics vote sets `g_race_physics` and schedules a map
  reload (see [VOTING_AND_ADMIN.md](VOTING_AND_ADMIN.md)).
- **Barriers** — GAME and CGAME both consult `race_clip.c`
  (see [MAPS_AND_ENTITIES.md](MAPS_AND_ENTITIES.md)).
- **Weapon tuning** — tuned values apply per stamped projectile; non-stock
  tuning marks the server unrankable.

## Key cvars

| Cvar | Owner | Default | Purpose |
|---|---|---|---|
| `g_race_physics` | GAME | `q2` | Selects the physics preset (latched; visible in the server browser). |
| `g_q2_snap_mode` | GAME | `2` | Q2-family velocity snapping: 0 off, 1 nearest, 2 truncate. Latched. |
| `cg_predict` | CGAME (common) | `1` | Enables client-side movement prediction. |

Diagnostic commands: `race_physics [status|families]` (server) and
`cg_race_physics` (client) print the full authoritative and synchronized
identities.

## Main source

```text
src/game/race/bg_pmove.c               src/game/race/race_weapon_movement.c
src/game/race/race_physics.c           src/game/race/g_ballistics.c
src/game/race/race_pmove_policy.c      src/cgame/race/cg_predict.c
src/game/race/race_physics_service.c   src/cgame/race/cg_race_physics.c
src/game/race/race_clip.c              src/cgame/race/cg_race_double_jump.c
```
