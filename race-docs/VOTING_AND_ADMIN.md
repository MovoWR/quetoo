# Voting and Administration

## Purpose

Two GAME-owned governance systems. **Voting** lets players change the map,
physics, or remove a disruptive player by majority. **Administration** gives
named, credentialed operators direct authority over the same actions plus
settings and accounts. They share the action layer (map scheduling, the kick
broker) and the admin service is the authorization source for everything else
in the mod that needs authority.

## Ownership

GAME owns all vote state, resolution, admin accounts, and authorization.
CGAME parses vote config strings for the HUD and menus, and computes the
admin login proof locally so the password never crosses the wire.

## Voting

Main components:

```text
race_vote.c               Pure vote state machine (ballots, quorum, outcomes).
race_vote_service.c       Server lifecycle: start, cast, resolve, execute.
race_vote_admission.c     Per-map rate limiting by address and profile.
race_vote_menu.c          Pure end-of-map ballot logic.
race_vote_menu_service.c  Intermission map-choice menu lifecycle.
cg_race_vote.c            CGAME parsing and HUD presentation.
```

There are two independent votes:

### The regular vote

Three types: **map**, **kick**, **physics**. One vote at a time.

```text
proposal      race vote map|kick|physics <target>
   ↓          initiator checks: not intermission, not spectator/bot/dead/idle,
   ↓          no active vote, admission (rate limit per address AND profile)
   ↓          target validated (map in catalog; kick target resolved;
   ↓          physics selector canonical and different from current)
voting        eligible roster snapshotted at start; initiator pre-votes YES;
   ↓          ballots may change; roster shrinks as voters disconnect
resolution    strict majority of eligible, with a quorum of half; resolves
   ↓          early once everyone has decided, else at the deadline
execution     2.5 s delay (1.5 s if sole voter), target re-validated,
              then: map → schedule; kick → identity-safe broker;
              physics → set g_race_physics + reload the current map
```

**Vote identity** is `{slot, connection_id}` — a monotonic per-connection ID
snapshotted into the eligibility roster at vote start. A player who
disconnects and reconnects lands in the same slot with a new ID and is simply
ineligible, never mistaken for the original voter. Eligibility requires being
a connected, live, non-bot participant (spectators only if the
`vote_allow_spectators` setting allows).

Physics votes accept a few legacy spellings (`quetoo`, `quetoo_fix`, `q2pro`)
that are normalized to canonical preset keys (`q2pro` → `q2-v1`) before
storage; only canonical keys are ever advertised or persisted.

### The end-of-map menu vote

At intermission, a menu of up to 8 map choices — player **nominations** first,
then a random fill from the catalog — runs for a configured duration. Ballots
are simple per-slot choices; ties break randomly; no votes means the map
repeats. The level does not change until the menu resolves, and opening it
cancels any live regular vote.

### Presentation

Vote state reaches clients as two config strings (`CS_RACE_VOTE_INFO` — type,
initiator, target, tallies, time; `CS_RACE_VOTE_MENU` — choices and counts,
republished once per second) plus per-client capability bits in
`STAT_RACE_VOTE_FLAGS` (may cast, may cast in menu, may nominate). CGAME
draws the active vote under the HUD lockup with per-voter tally tabs, the
intermission ballot list, the Voting menu route, and the global active-vote
strip in the ESC menu.

Vote settings (`voting_time`, `max_votes`, `vote_menu_duration`,
`vote_menu_choices`, `vote_allow_spectators`) come from the settings system.

## Administration

Main components:

```text
race_admin.c            Pure account document: accounts, roles, invariants.
race_admin_store.c      Durable admins.db load/commit.
race_admin_service.c    Runtime: sessions, statuses, migration, audit.
race_admin_password.c   Argon2id hashing and credential encoding.
race_admin_auth.c       Challenge/response proof (BLAKE2b, constant-time).
race_admin_admission.c  Login throttling (per account, address, global).
race_admin_allowlist.c  Operator cvar allowlist (operator_cvars.txt).
race_admin_actions.c    The `race admin ...` client action dispatcher.
race_kick_broker.c      Identity-safe kick commit.
cg_race_admin_auth.c    Client-side proof computation.
```

### Accounts, roles, capabilities

Accounts live in `race/admins.db` — a versioned, CRC-checked document of up
to 64 accounts, each with a stable ID, display handle, role, enabled flag,
revision, and an Argon2id credential. Three roles map to capability sets:

| Role | Capabilities |
|---|---|
| `moderator` | `player-kick`, `player-ban`, `vote-admin` |
| `operator` | + `settings-mutate`, `map-change`, `server-cvar` |
| `owner` | + `account-manage`, `cvar-allowlist-manage` |

(`player-ban` is granted but no ban action exists yet.) The document refuses
any mutation that would leave it without an enabled, credentialed owner, and
the server console command `race_admin password` doubles as owner recovery.
The deprecated shared `g_admin_password` cvar is registered but explicitly
ignored; a legacy plaintext `admins.cfg` in the write directory fails the
whole service closed until removed.

### Login

The password never crosses the wire:

```text
radmin <account>
   ↓  server sends salt + one-use 32-byte nonce (15 s lifetime);
   ↓  unknown accounts get an indistinguishable dummy salt
client derives key = Argon2id(password, salt), then
proof = keyed BLAKE2b over a domain string + account + nonce
   ↓  radmin proof ... (assembled by the client, never typed)
server recomputes and compares in constant time
```

The client-side password lives momentarily in the `radmin_password` cvar and
is force-cleared the instant a challenge arrives. Login attempts are
throttled per account, per address, and globally. A successful login creates
a per-connection **session** that is re-validated against the live document on
every use — editing or disabling an account invalidates its sessions
immediately. The session's capability mask is published to the client in
`STAT_RACE_ADMIN_CAPABILITIES`, which is what reveals the Admin menu route.

### Actions and the allowlist

`race admin ...` covers map change (plus an ungated `map validate` probe),
kick, vote cancel, account management, and the operator cvar allowlist. The
allowlist (`race/operator_cvars.txt`, up to 32 names, `rcon_password`
hard-banned) bounds what a non-owner with `server-cvar` may touch through the
settings commands; owners bypass it. Every action is audited with actor,
action, detail, and result.

### The kick broker

Stock kick takes a bare slot number and executes later through the command
buffer — by then the slot may hold a different player. Race's broker makes
kicks identity-safe:

```text
kick decided (vote passed / admin command)
   ↓  capture {slot, connection_id} ticket
   ↓  queue private command: race_kick_commit <slot> <id>
buffer drains
   ↓  revalidate: same slot still held by the same connection?
   stale → silently dropped        match → stock kick <slot> issued
```

The validation happens in the same buffer drain as the kick itself, closing
the reuse window. This is a bounded compatibility seam around the stock
command, not a new engine API.

### Admin and the rest of the mod

The admin service is the single authorization source: settings commands
(`server-cvar` + allowlist), weapon tuning (`settings-mutate`), vote
cancellation (`vote-admin`), and map/kick actions all call into it, and all
of them audit through it.

## Main source

```text
src/game/race/race_vote.c                 src/game/race/race_admin.c
src/game/race/race_vote_service.c         src/game/race/race_admin_service.c
src/game/race/race_vote_admission.c       src/game/race/race_admin_store.c
src/game/race/race_vote_menu.c            src/game/race/race_admin_auth.c
src/game/race/race_vote_menu_service.c    src/game/race/race_admin_password.c
src/game/race/race_kick_broker.c          src/game/race/race_admin_admission.c
src/game/race/race_actions.c              src/game/race/race_admin_allowlist.c
src/cgame/race/cg_race_vote.c             src/game/race/race_admin_actions.c
src/cgame/race/cg_race_admin_auth.c       src/cgame/race/cg_race_admin_command.c
```
