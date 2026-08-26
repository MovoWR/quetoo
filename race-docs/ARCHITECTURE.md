# Architecture

## The shape of the mod

Race is a paired **GAME / CGAME module set** for Quetoo, selected with
`+set game race`. The engine loads the two shared libraries per its normal
module contract; everything Race does lives inside them.

```text
Engine (server)                    Engine (client)
   │  loads GAME "race"               │  loads CGAME "race"
   ▼                                  ▼
GAME — authority                   CGAME — prediction + presentation
   run timing, physics, records,      movement prediction, HUD,
   replays, settings, voting,         scoreboard, replay display,
   admin, persistence                 training tools, native UI
   │                                  ▲
   └── stats, config strings, ────────┘
       unicast Race messages
```

**GAME is authoritative** for everything that matters: the run timer, finish
validity, records, physics identity, votes, settings, admin authority.
**CGAME predicts and presents** — it mirrors server state, runs the same
movement code locally for responsiveness, and refuses to predict when it
cannot prove it is synchronized. No client-side value ever becomes a result.

## Module contracts and hooks

Quetoo's common GAME/CGAME source pools call `G_Module_*` / `Cg_Module_*`
functions that each module must define (a missing one is a link error), and
expose chainable function-pointer hooks a module installs over. Race
implements the full contract in `src/game/race/g_module.c` and
`src/cgame/race/cg_module.c`, and installs chained hooks for level
configuration, media, items, and movement preparation. Where Race needs a
seam that common does not expose, it declares it in a **module-private
compatibility header** (`race_module_compat.h`, `cg_module_compat.h`) whose
implementations are selected only by Race's own build files — these are
internal shims, never new host APIs.

## Race-owned code vs bounded overrides

Race source splits into two kinds:

- **Module-owned additions** — the `race_*` / `cg_race_*` files (services,
  wire codecs, stores, UI). These are new code with no stock counterpart.
- **Ledgered whole-file overrides** — Race's copies of stock-derived files
  (`g_client.c`, `g_combat.c`, `g_entity.c`, `cg_predict.c`, `cg_entity.c`,
  the loading screen, and so on) that shadow the common versions via build
  source selection. Each carries a *bounded* Race delta — typically a hook
  installation or a small dispatch — and an obligation to preserve stock
  behavior otherwise.

Every override is listed in the repository's override ledger
(`doc/race-override-ledger.md`), and `src/tools/verify_race_overrides.py`
audits all 73 files against a pinned stock baseline commit: the stock tree
must remain untouched, no unledgered basename collisions may appear, and all
three build systems (autotools, Visual Studio, Xcode) must select the same
sources. The standalone principle: **Race never copies its policy into
`src/game/common`, `src/cgame/common`, the engine, or another module to make
itself compile.**

## Shared movement

`src/game/race/bg_pmove.c` — the movement mover — is one source file compiled
into **both** modules, so server movement and client prediction are literally
the same code. It is the only GAME override shared with CGAME; determinism is
protected by build flags (`-ffp-contract=off`), runtime synchronization
gates, and dedicated tests. See
[MOVEMENT_AND_PHYSICS.md](MOVEMENT_AND_PHYSICS.md).

## The wire manifest

Race's `src/game/race/g_types.h` is the GAME/CGAME wire contract in one
header: the module's `PROTOCOL_MINOR` (1066 at time of writing — read the
header, not this document), thirteen Race server-to-client message commands
(`SV_CMD_RACE_*`), eight Race config strings (seven `CS_RACE_*` slots — physics identity,
leaderboard, vote state, settings and tuning status — plus
`CS_HOOK_PULL_SPEED`), thirteen Race player
stats (`STAT_RACE_*`), and the Race extensions to client, entity, and score
structures. Both modules compile against it, so a wire change is a paired
change by construction. Current host contracts: GAME API 33, CGAME API 35,
protocol major 2029.

## GAME ↔ CGAME communication

Three channels, chosen by data shape:

| Channel | Used for |
|---|---|
| Player stats | Per-frame per-client scalars: run state, elapsed time, checkpoint count, PB/WR, capabilities, vote flags. |
| Config strings | Shared slow-changing state: physics identity, leaderboard top-15, vote info/menu, settings and tuning status. |
| Unicast messages | Structured payloads: finish reports, splits, replay state/telemetry/projectiles, racelines, map browser pages, tuning sync, admin and profile challenges. |

Wire payloads are versioned, bounded, and validated on both ends; several
codecs (finish report, settings status) run the same validation on encode and
decode so a non-canonical payload cannot exist.

## Persistence boundary

All durable data is server-side, file-backed, and written through one
hardened persistence layer (candidate-then-promote with read-back
verification, symlink-safe path handling). CGAME persists only local
conveniences (practice markers, archived preference cvars) through its own
verified-write primitive. See
[RECORDS_AND_PERSISTENCE.md](RECORDS_AND_PERSISTENCE.md).

## Ownership table

| Area | Owner | Purpose |
|---|---|---|
| Run timing & validity | GAME | Owns the millisecond timer and decides whether a finish counts. |
| Movement authority | GAME | Runs the real `Pm_Move` with enforced physics parameters. |
| Movement prediction | CGAME | Replays pending commands through the same shared mover. |
| Physics identity | GAME | Selects the preset, publishes it, gates rankability. |
| Course & barriers | GAME | Spawns/validates entities; authoritative barrier collision. |
| Barrier prediction | CGAME | Identical shared collision kernel against client-local latches. |
| Records & leaderboards | GAME | Evaluates, stores, and publishes replay-backed records. |
| Profiles & identity | GAME | UUID identity with challenge/response auth; client holds the secret. |
| Replay record/store/playback | GAME | Records runs, stores QRPL files, re-drives playback viewers. |
| Replay & raceline display | CGAME | Renders streamed poses, telemetry, projectiles, and trails. |
| Settings | GAME | Catalog, validation, scopes, persistence, activation. |
| Weapon tuning | GAME | Live tunable weapon values; deviation makes the server unranked. |
| Voting | GAME | Vote lifecycle, eligibility, resolution, execution. |
| Administration | GAME | Accounts, roles, capabilities, sessions, audit, allowlist. |
| HUD & scoreboard | CGAME | Draws run state, records, votes, and roster from mirrored data. |
| Native UI | CGAME | ObjectivelyMVC menu shell and screens; issues commands only. |
| Training tools | CGAME | Input/jump viewers, strafe helper, markers — local presentation. |

## Reading order

Start with [GAMEPLAY_AND_RUNS.md](GAMEPLAY_AND_RUNS.md) for the core loop,
then [MOVEMENT_AND_PHYSICS.md](MOVEMENT_AND_PHYSICS.md) for why movement is
special. The remaining documents each stand alone;
[SYSTEM_INDEX.md](SYSTEM_INDEX.md) answers "where does this feature live".
