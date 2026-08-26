# Settings System

## Purpose

Race has its own configuration system layered *on top of* engine cvars. It adds
what raw cvars lack: typed validation, a stable alias namespace, per-map
overrides, durable persistence with atomic commits, defined activation timing,
capability-gated authorization, an audit trail, and a client mirror so menus
always show the truth.

Three things that look similar are distinct:

- **Race settings** — the 15 cataloged entries below, managed by this system.
- **Engine/server cvars** — everything else on the server (`g_balance_*`,
  `g_hook_*`, `sv_*`...). `gset` can persist these too, but without typed
  validation.
- **CGAME local cvars** — client-side preferences (`cg_*`), plain archived
  cvars outside this system entirely (see
  [CVAR_REFERENCE.md](CVAR_REFERENCE.md)).

## Ownership

GAME owns the catalog, validation, authorization, persistence, and activation.
CGAME holds a read-only decoded mirror for menu display — the server
revalidates every write, so a stale mirror can only draw a wrong label, never
write a wrong value.

## Main components

```text
race_settings.c          The catalog: 15 typed descriptors + parse/format rules.
race_settings_service.c  Runtime: resolution, commands, authorization, activation.
race_settings_store.c    The gset.cfg document store (atomic candidate/promote).
race_settings_wire.c     Encodes assigned state into CS_RACE_SETTINGS_STATUS.
race_map_properties.c    Per-map overrides stored in the map list file.
cg_race_settings.c       CGAME read-only status mirror.
```

## The catalog

Each setting is a descriptor: id, alias (the short name used in commands and
menus), backing cvar, map key, type (bool/int/float/enum/string), bounds or
enum values, default, activation timing, and description. The catalog is
validated at startup — every default must round-trip through parse/format
byte-for-byte, names must be canonical and unique — and a rejected catalog
disables the whole service.

| Alias | Backing cvar | Type / range | Default | Applies |
|---|---|---|---|---|
| `finish_cue_enabled` | `g_race_finish_cue_enabled` | bool | 1 | immediately |
| `finish_cue_gain` | `g_race_finish_cue_gain` | int 1–100 | 100 | immediately |
| `checkpoint_feedback` | `g_race_checkpoint_feedback` | `time` / `silent` | time | immediately |
| `voting_time` | `g_race_voting_time` | int 0–300 s | 30 | immediately |
| `max_votes` | `g_race_max_votes` | int 0–100 | 3 | immediately |
| `vote_menu_duration` | `g_race_vote_menu_duration` | int 0–300 s | 20 | immediately |
| `vote_menu_choices` | `g_race_vote_menu_choices` | int 0–8 | 3 | immediately |
| `vote_allow_spectators` | `g_race_vote_allow_spectators` | bool | 0 | immediately |
| `weapons` | `g_race_weapons` | bool | 1 | on restart |
| `gravity` | `g_gravity` | int 1–32767 | 800 | immediately |
| `gameplay` | `g_gameplay` | enum (7 modes) | default | on restart |
| `min_clients` | `sv_min_clients` | int | 0 | immediately |
| `frag_limit` | `g_frag_limit` | int | 30 | immediately |
| `time_limit` | `g_time_limit` | float, minutes | 30 | immediately |
| `music` | `g_music` | string | "" | on restart |

Every setting is map-overridable.

## Scopes and effective values

```text
map override      (row in the sv_map_list file, edited by mset/mclear)
    overrides
global assignment (race/gset.cfg, edited by gset/gclear)
    overrides
catalog default   (compiled in — or the map's own compiled value for
                   engine-level settings like gravity that a map may set)
```

At each level load the service records the global values, resets every
descriptor cvar to them, parses the map list for this map's overrides, applies
them, and caches the effective values. A console-level `set g_gravity 1` is
therefore temporary — the next map load restores the recorded configuration.
Engine-side values (gravity, gameplay, limits, music) are pushed into the
level only when actually assigned; otherwise the map's own compiled fallback
wins.

## Persistence

The global document is `race/gset.cfg` — one `set <name> "<value>"` line per
assignment, up to 128 assignments (non-catalog cvars allowed). Commits go
through the shared candidate/verify/promote path with owner-only permissions
(see [RECORDS_AND_PERSISTENCE.md](RECORDS_AND_PERSISTENCE.md)), and loads are
fail-closed: a single invalid assignment discards the whole document with a
warning. Map overrides are persisted by rewriting the map list file through a
strict parse → edit → revalidate-candidate cycle; `mset`/`mclear` deliberately
affect the *next* load of that map, never the running level.

## The wire mirror

CGAME compiles the catalog itself, so the config string
(`CS_RACE_SETTINGS_STATUS`) carries only *state*: which settings are assigned
globally, which are overridden by the current map, and their values, in a
strictly canonical escaped encoding that the client validates by re-encoding
and comparing. If the payload would overflow, values are dropped
longest-first and a `truncated` flag is set — the menu then shows "assigned,
value unknown" instead of a wrong default.

## Authorization

Every settings mutation runs through the admin service (see
[VOTING_AND_ADMIN.md](VOTING_AND_ADMIN.md)): the caller needs the
`server-cvar` capability, the target cvar must be on the operator allowlist
(owners bypass the list), and it must not be write-protected. Sensitive names
(`rcon_password`, `g_password`, `g_admin_password`) are redacted from reads.
Every read and write is audited with scope, cvar, and outcome.

## Command surface

```text
gset / gget / gclear      global scope (persisted to gset.cfg)
mset / mget / mclear      map scope (persisted to the map list; next map load)
allowcvar list|add|remove|reload    manage the operator cvar allowlist
```

## Weapon live tuning (a sibling system)

Weapon tuning shares the settings system's philosophy — server-authoritative,
validated, admin-gated — but is deliberately separate and **non-persistent**:

- A catalog of exactly 22 tunable values (hyperblaster speed/knockback/refire
  and the nine climb parameters, rocket and grenade speed/knockback/fuse/
  refire, hand-grenade knockback/refire, global self-knockback, grenade
  velocity inheritance), each with type, range, and step
  (`race_weapon_tuning.c`).
- The runtime baseline is captured from the live balance cvars; applying a
  change bumps a generation, cleans the boundary (covered projectiles freed,
  everyone reset to Practice and respawned), and stamps subsequent projectiles
  with the tuned values so in-flight shots keep what they were fired under.
- **Any deviation from baseline makes the server unrankable** — Race mode,
  record publication, and replay playback are blocked until reset, and every
  client shows an "UNRANKED" warning.
- Sync to clients is a versioned binary stream over
  `SV_CMD_RACE_WEAPON_TUNING` plus a status config string; the client caches
  all-or-nothing with hash verification and never predicts tuned values.
- Mutations require the `settings-mutate` admin capability and go through
  `race tune apply` / `race tune reset` with compare-and-swap generations.
  Nothing is ever written to disk or back into the cvars.

The Admin UI's "Weapons" tab (WeaponLab) is the front end — a sparse local
draft over the synchronized cache, applied as one batch (see
[UI_AND_HUD.md](UI_AND_HUD.md)).

## Interactions

- **Admin** — sole authorization and audit path.
- **Maps** — per-map overrides live in the map list file
  (see [MAPS_AND_ENTITIES.md](MAPS_AND_ENTITIES.md)).
- **Physics** — gravity changes refresh the level's movement parameters;
  weapon tuning gates rankability.
- **Voting** — five settings are voting policy knobs consumed by the vote
  services.

## Main source

```text
src/game/race/race_settings.c            src/game/race/race_weapon_tuning.c
src/game/race/race_settings_service.c    src/game/race/race_weapon_tuning_service.c
src/game/race/race_settings_store.c      src/game/race/race_weapon_tuning_wire.c
src/game/race/race_settings_wire.c       src/cgame/race/cg_race_weapon_tuning.c
src/game/race/race_map_properties.c      src/cgame/race/cg_race_settings.c
```
