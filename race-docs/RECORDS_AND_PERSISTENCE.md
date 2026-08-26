# Records and Persistence

## Purpose

Race keeps three kinds of durable data: **who a player is** (profiles), **what
has been achieved on each map** (map state and its leaderboard), and **the
evidence behind those achievements** (replay files). All of it is file-backed
on the server, written through one shared, deliberately paranoid persistence
layer. There is no database and no network service — everything lives under the
engine's write directory.

## Ownership

GAME owns all of it. CGAME never reads or writes these files; it only receives
leaderboard and map-browser data over the wire and presents it.

## The big picture

```text
Player profile  (profiles/<uuid>.profile)
    └── identity: UUID + display name + credential hash

Map state       (state/<ruleset>/<hex-map>.state)
    └── leaderboard records, one per profile UID
            └── replay_id ─────────────┐
                                       ▼
Replay store    (replays/<ruleset>/<hex-map>/replay-<id16>.ghost)
    └── QRPL replay file

Settings        (gset.cfg — see SETTINGS_SYSTEM.md)
Admin data      (see VOTING_AND_ADMIN.md)
```

A record is only trusted if it is *replay-backed*: the map-state format refuses
to publish records that do not reference a stored replay, and the map-state
service re-verifies referenced replays in the background after loading.

## Player profiles

Main components:

```text
race_profile.c        One profile: format, canonicalization, validation.
race_profiles.c       The collection: enrollment, connect/disconnect lifecycle.
race_profile_auth.c   Challenge/response proof of identity.
```

A profile is intentionally small — `uid`, `display_name`, `credential` — and
holds no statistics. Records live in map state and point back at the profile by
UID.

**Identity model.** The server mints an RFC-4122 v4 UUID at enrollment and
hands the client a one-time secret. The server stores only an Argon2id hash of
that secret (same parameters as the admin password module). On every later
connection the flow is:

```text
client connects
   ↓
server sends SV_CMD_RACE_PROFILE_REQUEST
   ↓
client asks for a challenge for its UID
   ↓
server sends salt + 16-byte nonce  (single use, 15 s lifetime)
   ↓
client derives the credential from its secret and returns a keyed
BLAKE2b-256 proof over a fixed domain string + UID + nonce
   ↓
server verifies in constant time → connection is "ready" for ranking
```

Only an authenticated UID (`Race_Profiles_AuthenticatedUid`) can publish ranked
results. Enrollment is rate-limited per address and globally, and two connected
clients may not claim the same UID. If a player's netname changes, the stored
display name is updated; display names in records are snapshots, never
identity.

## Map state and leaderboards

Main components:

```text
race_map_state.c          Map-state model, canonical map names, text format + CRC.
race_map_state_store.c    Pure load/commit file I/O with read-back verification.
race_map_state_service.c  The live singleton: lifecycle, status machine, queries.
race_leaderboard.c        Record evaluation, ranking, insertion.
race_leaderboard_wire.c   Encodes the top list into a configstring for clients.
```

**Scope of a state file.** One file per *(map, ruleset)* pair, where the
ruleset is the physics preset identity (for example `q2-v1` or
`quetoo-fix-v1` — see MOVEMENT_AND_PHYSICS.md). Times set under different
physics never share a leaderboard. Mode is not part of the key; instead the
service simply refuses to rank a finish unless the client is in Race mode
(Practice runs are never ranked).

**Record identity.** One record per profile UID per state file. A new time only
replaces the same player's previous record if it is faster. Ranking is
elapsed-time ascending (UID as tiebreaker), with the top 15 exposed to clients.
Each record carries the elapsed milliseconds, checkpoint times, analytical
split times, a date, a display-name snapshot, and the `replay_id` of the ghost
replay proving the run.

**Format and integrity.** The state file is a versioned text format (V1–V4,
current writes upgrade to V4) ending in a CRC-32 line. V2 introduced mandatory
replay backing, V3 added dates, V4 added split layouts. The service loads a
file into one of a few statuses: `READY`, `VALIDATING` (replay-backed records
are re-verified one replay per frame after load), `PENDING_V1` (legacy records,
read-only), `CORRUPT`, or `UNAVAILABLE`. Any replay that fails re-verification
disables the leaderboard rather than serving unproven times.

**Publication to clients.** Only a `READY`, replay-backed state publishes. The
top 15 records are encoded as a compact backslash-delimited string into the
`CS_RACE_LEADERBOARD` configstring, so every client sees the current top list
without asking.

## Map catalog and per-map properties

Main components:

```text
race_map_catalog.c      Parses the server's map list into an in-memory catalog.
race_map_properties.c   Per-map setting overrides stored in the same file.
race_map_browser_service.c / race_map_browser_wire.c
                        Serves paged catalog + leaderboard data to the client.
```

The single source is the file named by the `sv_map_list` cvar (default
`maps.lst`, shipped at `share/race/maps.lst`). Each brace-delimited row names a
map and may carry metadata (`message`, `author`, `description`, `tags`,
`difficulty` 1–10, `time_limit`) plus any *map-overridable Race setting*
(gravity, weapons, voting limits, and so on — see SETTINGS_SYSTEM.md). The
catalog is bounded (256 entries) and re-parsed on demand; map names must
survive canonicalization (lowercase, `maps/`-prefix and `.bsp`-suffix
stripped, restricted character set) or the row is dropped.

The map browser service answers client commands (`maps_ui`, `mapinfo_ui`,
`maptimes`) with paged rows combining catalog metadata with each map's world
record and the requesting player's personal best, read from the map-state
files. See CGAME_AND_PRESENTATION.md for the client half.

## The persistence layer

`race_persistence.c` is the one shared file layer under everything above (and
under replays, settings, and admin data). Its job is to make server-side writes
boring and safe:

- **Path safety.** Virtual paths are validated (no absolute paths, no `..`, no
  backslashes), and the engine-resolved real path is verified to still end in
  the expected virtual path. On POSIX, directories are walked component by
  component with `openat(O_NOFOLLOW)` so a symlinked directory cannot redirect
  a write; Windows gets an equivalent reparse-point and link-count check.
- **Candidate-then-promote writes.** Every save writes a `.candidate` file
  first (fsynced, created exclusively, never through a symlink). Callers then
  *read the candidate back and re-parse it* — the map-state store requires
  structural equality with what it meant to write — before `Promote` renames it
  over the committed file and fsyncs the directory. A crash at any point leaves
  either the old file or a verifiable candidate, never a torn state.
- **Owner-only mode** for secrets (profile credentials, admin data): candidate
  files are created with `0600` permissions (a restricted DACL on Windows).

Integrity beyond that — magic version headers, CRC trailers, monotonic
`generation` counters — is layered on by each format, not by the persistence
layer itself.

## Interactions

- **Replay system** — replays are committed first, then the record referencing
  them; if the record commit fails, the new replay is removed
  (see REPLAY_SYSTEM.md).
- **Physics** — the active preset decides the ruleset directory and whether the
  map is rankable at all; unrankable physics disables map state.
- **Settings** — per-map overrides ride in the map list file and are applied by
  the settings service at map load (see SETTINGS_SYSTEM.md).
- **Voting / map browser** — both read map-state summaries to show records.

## Main source

```text
src/game/race/race_profile.c            src/game/race/race_map_state.c
src/game/race/race_profiles.c           src/game/race/race_map_state_store.c
src/game/race/race_profile_auth.c       src/game/race/race_map_state_service.c
src/game/race/race_leaderboard.c        src/game/race/race_map_catalog.c
src/game/race/race_leaderboard_wire.c   src/game/race/race_map_properties.c
src/game/race/race_persistence.c        src/game/race/race_connection_address.c
src/game/race/race_map_browser_service.c
src/game/race/race_map_browser_wire.c
share/race/maps.lst
```
