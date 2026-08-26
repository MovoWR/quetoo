# Race override ledger

This ledger is the maintenance boundary between Race and the pinned current
Quetoo source. The immutable stock source baseline is commit
`3fd1e9253284ad7f5431de17fe503475a9af704b`.

The override audit covers the ledgered overrides; `Stock-tree integrity` below
covers everything else in the tree. Run both guards after changing Race, a
stock-derived override, or any project file:

```sh
python3 src/tools/verify_race_overrides.py
python3 src/tools/verify_projects.py --verbose
```

`verify_race_overrides.py --markdown` emits the complete canonical-LF SHA-256
and line-delta table for all 73 files. The audit reads each baseline directly
from the named Git commit, proves that the adjacent stock working-tree source
still matches it, rejects unledgered basename collisions, checks the API and
protocol constants, runs the three-build-system parity guard, and scans the
production Race tree for known non-portable layout/compiler constructs.

## Source-selection contract

- Autotools uses module-local `vpath` precedence and names every shared Race
  translation unit explicitly.
- Visual Studio removes each replaced common item and includes the Race-owned
  item exactly once in `game-race.vcxproj` or `cgame-race.vcxproj`.
- Xcode's `game-race` and `cgame-race` targets select the same resolved paths.
- `verify_projects.py` compares resolved repository paths, not only basenames.
- Stock GAME and CGAME targets continue selecting the untouched common/default
  files.

No override may be copied back into `src/game/common`, `src/cgame/common`, the
engine, or a stock module to make Race compile.

## GAME common overrides

| Race file | Bounded Race delta | Primary obligations |
|---|---|---|
| `bg_pmove.c` | Q2 and Race movement implementation shared by GAME and CGAME | Keep command streams, snapping, hulls, water, ladder, ramp, landing, and no-autohop behavior identical across GAME, CGAME, and direct AI |
| `g_ai_main.c` | Uses the spawned Race movement mask for living AI and prevents speculative AI lookahead from mutating authoritative one-way-wall state | Preserve the exact pinned stock blob apart from the Race include, movement traces, dynamic standing-hull substitutions, and bounded latch snapshot enforced by the override verifier |
| `g_ai_node.c` | Uses the active standing hull for path tests and rejects malformed or unbounded navigation input | Preserve the exact pinned stock blob apart from the Race include, dynamic standing-hull substitutions, and bounded fail-closed loader checks enforced by the override verifier |
| `g_ballistics.c` | Emits Race-local QRPL projectile observations through the module-owned observer | Preserve stock weapon signatures, damage order, immediate impacts, unobserved Quake rockets, and idempotent observer restoration |
| `g_client.c` | Installs Race lifecycle, spawn, input, mode, and movement hooks | Preserve stock connection, respawn, user-info, and entity-link order |
| `g_client_stats.c` | Adds Race HUD, score, roster, and inventory hook points | Preserve stock stats and scoreboard wire fields |
| `g_client_view.c` | Finalizes replay-backed client views once per frame | Preserve stock view, damage, animation, and end-frame order |
| `g_cmd.c` | Routes bounded Race client/intermission/activity commands and rejects private admin command shapes before chat fallback | Preserve stock command authorization and ordinary fallthrough; legacy shared-password admin is disabled in Race |
| `g_combat.c` | Applies the Race damage filter after `take_damage` and before protection/armor | Preserve stock knockback, armor, death, and non-client damage behavior |
| `g_entity.c` | Resolves the incoming Race settings layer before worldspawn, dispatches Race entities after stock item/class handling, and owns per-level score-vector cleanup | Preserve stock entity order and spawn precedence, ensure worldspawn never observes the previous map's override, and preserve entity parsing and teardown |
| `g_hook.c` | Applies the Race hook-allowed policy | Preserve stock hook state, pull, damage, and reset behavior |
| `g_main.c` | Advances Race frame/finalization services and releases module-owned score vectors | Preserve stock frame, intermission, map, and shutdown order |

`bg_pmove.c` is deliberately compiled into both Race modules. Every other GAME
override is GAME-only.

## CGAME common overrides

| Race file | Bounded Race delta | Primary obligations |
|---|---|---|
| `cg_discord.c` | Reports Race through the current Quetoo Discord rich-presence path | Preserve stock Discord initialization, callbacks, connection secrets, and shutdown behavior |
| `cg_entity.c` | Uses Race standing bounds and hides/filters conditional Race entities | Keep entity interpolation, effects, and scene population stock-compatible |
| `cg_main.c` | Routes Race server messages, invalidates changed-slot score enrichment, and refreshes the open Home roster when client configstrings change | Reject malformed Race messages without consuming stock commands |
| `cg_media.c` | Registers Race replay/HUD media | Preserve stock media initialization and shutdown ownership |
| `cg_predict.c` | Gates prediction on authoritative physics, uses Race masks, and runs paired prediction hooks | Remain command-for-command equivalent to GAME `bg_pmove.c` |
| `cg_score.c` | Owns Race HUD, configstring-backed roster with validated score enrichment, timer, replay, input/jump, vote, and raceline presentation while delegating score snapshot assembly to `cg_score_model.c` | Publish only complete, validated score snapshots and use only current CGAME drawing/message facilities and stock-compatible beams |
| `cg_score.h` | Declares the Race presentation surface owned by `cg_score.c` | Do not expose a new stock host ABI |

## Per-module manifests

| Stock module baseline | Race owner | Reason |
|---|---|---|
| `src/game/default/bg_item.c` | `src/game/race/bg_item.c` | Race's bounded Quake-bolt pickup quantity |
| `src/game/default/bg_item.h` | `src/game/race/bg_item.h` | Intentional exact module-manifest copy; Race must not include another module's private header |
| `src/game/default/g_module.c` | `src/game/race/g_module.c` | Installs and coordinates Race-owned services and common override hooks |
| `src/game/default/g_types.h` | `src/game/race/g_types.h` | Race protocol minor, packet/config/stat/entity fields, score payload, and per-client state |
| `src/cgame/default/cg_module.c` | `src/cgame/race/cg_module.c` | Paired CGAME lifecycle, message, prediction, UI, replay, and presentation implementation |

The Race `g_types.h` is the shared GAME/CGAME wire manifest. Changes require
paired modules, deterministic codec tests, and interoperability proof. GAME API
33, CGAME API 35, and protocol major 2029 are current module and engine
contracts and must never be edited to hide a mismatch.

## Native UI replacements

Race owns 49 stock-relative UI replacement files under these routes:

- `DialogViewController.css`
- `conback.png`: the Race-branded console background. It retains the stock
  `ui/conback.png` resource role and 16:9 presentation contract; only the
  artwork and source resolution differ.
- `backgrounds/0.png`..`5.png`: the six menu backgrounds. Stock ships
  character renders in bright daylight; Race replaces them with the
  branding pack's dark, low-detail track plates, which are what the menu
  copy is legible over. Same names, same 640x360 RGB contract, same
  `ui/backgrounds/%u.png` load in `MainView` - only the artwork differs.
- `controls/**`: Controls, Crosshair, Movement/Combat, and Response Service
- `credits/**`: Credits
- `home/**`: Home
- `main/**`: Loading, Main view/controller, and Update styling
  - Loading is a whole-file replacement of the controller, its view tree and
    its stylesheet, carrying the "Menu v2 - Loading" design. `cg_ui.c` stays
    stock and is compiled against the stock header, so the Race header keeps
    the stock struct prefix and the stock interface shape exactly; Race state
    is appended, never inserted, and no interface method is added. The lockup,
    `main/loading_lockup.png`, has no stock counterpart and is a Race-owned
    addition rather than an override.
- `play/**`: Create Server, Join Server, and Player Setup
- `settings/**`: Settings

The exact relative-path list is `UI_OVERRIDES` in
`src/tools/verify_race_overrides.py`; the verifier rejects additions or
omissions. These files are a cohesive Race-native presentation replacement,
not a host UI patch. Race-only Admin, Maps, Voting, quick-settings, active-vote,
and helper classes have no stock-relative counterpart and therefore are normal
Race-owned additions rather than whole-file overrides.

`MainView::render` is the Race live-refresh seam. ObjectivelyMVC continues to
own input and view-controller lifecycle. `JoinServer` uses the current aggregate
server metadata and `PointerArray` contract directly. Its Race-local C/H files
extend the presentation while compiling against the same public CGAME API 35
header as the other modules. Default, CTF, and Lithium continue selecting their
current common UI counterparts.

## Race-private integration additions

These files do not replace stock files or public module headers:

| File | Ownership |
|---|---|
| `src/cgame/race/cg_module_compat.h` | Private declarations implemented by selected Race CGAME overrides |
| `src/cgame/race/cg_race_barriers.c/.h` | CGAME barrier catalog, entity lookup, trace, latch, reset, and draw ownership using the shared Race collision policy |
| `src/cgame/race/cg_team_mode.c/.h` | Stock-API-compatible local team-mode catalog needed by the native server-creation UI |
| `src/game/race/race_clip.c/.h` | Shared deterministic conditional-gate and one-way-wall collision kernel |
| `src/game/race/race_kick_broker.c/.h` | Identity-safe `{slot, connection_id}` broker that commits a numeric stock kick in the same command-buffer drain |
| `src/game/race/race_module_compat.h` | Private declarations implemented by the selected Race GAME overrides; never a host import/export |
| `src/game/race/race_projectile_compat.h` | Private, idempotent projectile-observer lifecycle bridge between Race ballistics and replay recording |
| `src/game/race/race_settings.c/.h`, `race_settings_store.c/.h`, `race_settings_service.c/.h` | Shared 15-entry cvar registry, atomic `race/gset.cfg` persistence, activation ordering, and GAME-side command authority |
| `src/game/race/race_map_properties.c/.h` | Sparse active-`sv_map_list` property parser/editor shared by map activation and `mset`/`mclear` |
| `src/game/race/race_admin_allowlist.c/.h` | Independently persisted, candidate-verified canonical Operator cvar policy |

The verifier records exact SHA-256 values for the 11 files in its
`COMPATIBILITY_FILES` manifest. Compatibility headers may contain only
module-private declarations backed by implementations in the same Race image;
they must not redefine public GAME, CGAME, or engine imports. The additional
Race services in this table are covered by project-graph and focused-test
verification.

Current canonical-LF compatibility hashes:

| File | SHA-256 |
|---|---|
| `src/cgame/race/cg_module_compat.h` | `5e47b576f295117f9b7b6d5eae8fe318dfe1bd12e2579ba9a5bd55ac068b6d91` |
| `src/cgame/race/cg_race_barriers.c` | `181b5714893c7e3ebc014794d47eaec03218540716e16796f04cba8b001dbc35` |
| `src/cgame/race/cg_race_barriers.h` | `f8c3a8485d9cc908fef185a23a62f06a5ac9ed57ff0931f8ac2e1fe4d8178aa7` |
| `src/cgame/race/cg_team_mode.c` | `2dfb75fc0ad5dddb94e531a66a00ef9d111f00b34d54efc7122edf854637bc8b` |
| `src/cgame/race/cg_team_mode.h` | `194f6b5aa70299fd4e02e4188e2de91f4a82bae6267df20c7812ea64a69fcb2c` |
| `src/game/race/race_clip.c` | `896b4f66d724ceacd5821e06249b80e5c206ed3faa4296312a139977829af3b3` |
| `src/game/race/race_clip.h` | `cb523e091d3ac13afc93d927a97dc7a0b9e44d414911dc25a305ffb9b1961080` |
| `src/game/race/race_kick_broker.c` | `fea357d9cd30168e5c3b93563769a30f192a93c91d9258245e7bdaa35633ce43` |
| `src/game/race/race_kick_broker.h` | `8ffd87b59901b88b334dfc50e3096babbef039c96355b6fcccb5813c11a932ff` |
| `src/game/race/race_module_compat.h` | `afc0a517df83bcae1235a4e3e012743de5f2b8360be2ca925f7648de0e3a78af` |
| `src/game/race/race_projectile_compat.h` | `0d227c2e203470b9ad282bcbb3aac23dfcb61fe879e6d41dd1242ddc4fe180cf` |

### Race-owned live weapon-tuning additions

These additions are Race-private implementation, presentation, or test files;
they do not replace stock files and do not extend the public module ABI:

| File | Ownership |
|---|---|
| `src/game/race/race_weapon_tuning_types.h`, `race_weapon_tuning.c/.h` | Shared ordered catalog-v3 contract: 22 values for the four Quake II weapon types plus Global, including the complete Hyperblaster climb vector, typed snapshots, numeric validation, safety envelope, and deterministic identity |
| `src/game/race/race_weapon_tuning_wire.c/.h` | Explicit bounded little-endian GAME/CGAME transaction codec |
| `src/game/race/race_weapon_tuning_service.c/.h` | GAME-only runtime-baseline/current authority, direct Apply/Reset All handling, generation compare-and-swap, atomic mutation, rankability derivation, cleanup, audit, stamped-projectile resolution, command handling, and unconditional compact-status publication after each level configuration |
| `src/cgame/race/cg_race_weapon_tuning.c/.h` | CGAME-only staged baseline/current cache, local integrity verification, request correlation, direct Apply/Reset All command emission, compact status, per-frame bounded resynchronization, and presentation-only Hyper range |
| `src/cgame/race/ui/admin/WeaponLabViewController.c/.h/.json/.css` | Native Admin > Weapons panel; authoritative catalog rendering, local drafts, atomic Apply, and direct Reset All |
| `src/tests/check_race_weapon_tuning.c` | Catalog, parser, invariant, wire-codec, cache-integrity, correlation, and fail-closed regression fixture |

GAME is the sole gameplay authority. CGAME must not apply tuned ballistics,
refire, damage, knockback, inheritance, or movement prediction. While tuning is
custom, rank/replay/publication guards and the persistent UNRANKED presentation
are independent obligations. The temporary tool has no enable/private/password
gate, explicit session lifecycle, preset selector, undo, A/B slots, named
persistence, or Export/Load surface. Returning exactly to the runtime-captured
baseline restores inactive/rankable behavior automatically.

## Remediation-owned additions and gates

- `cg_race_barriers.c/.h` owns the client barrier lifecycle; `cg_module.c`
  remains a thin, order-preserving coordinator.
- `cg_score_model.c/.h` owns bounded, atomic score snapshot assembly and
  presentation-state decisions independently of drawing.
- `race_projectile_observer.c` owns projectile observer state and replay
  emission; installation and restoration are idempotent.
- `check_race_native` compiles the real module coordinator, native persistence
  and ObjectivelyMVC event fixtures, the quick-settings responder, the
  dashboard-layout gate, and the collision, score, team-mode, kick, hook, and
  projectile components in Autotools, Visual Studio, and Xcode.
  `verify_race_overrides.py` rejects source-graph drift among those three
  registrations.
- `.github/workflows/build.yml` and `.github/workflows/release.yml` run the
  source verifiers and the Race tests registered for each platform. The
  versioned Linux and macOS release jobs run all seven canonical Race test
  targets plus portability write/readback. Windows runs the native integration
  and weapon-tuning executables directly, requires
  `RACE_NATIVE_TEST PASS assertions=100167 differential=100000` and
  `RACE_WEAPON_TUNING_TEST PASS assertions=862 values=22`, and verifies the
  portability corpus.
- `verify_race_ui.py` parses every Race JSON/CSS resource and checks outlet,
  route, lifecycle, focus, Escape, resize, Home-state, Admin-capability, and
  selected-font contracts without launching a renderer.
- Workflow-level permissions are `contents: read`. Only the versioned GitHub
  release job receives `contents: write` in the protected `release`
  environment. Its S3 marker is written only after release creation succeeds,
  and the itch.io job has `needs: release`.

## Stock-tree integrity

The override audit above proves each *ledgered* Race file against the pinned
current stock baseline. Every adjacent stock counterpart must match that
reference exactly. The override comparison alone says nothing about the rest of
the tree, so an edit to the engine, a vendored dependency, or another module's
build file could otherwise pass unnoticed.

`verify_race_overrides.py` also runs `validate_stock_tree_integrity`, which
compares the whole working tree against the selected current Quetoo reference
`3fd1e9253284ad7f5431de17fe503475a9af704b`.

Every path is classified:

- **Race-owned** (`src/game/race/`, `src/cgame/race/`, `share/race/`, `src/tests/check_race*`,
  `src/tests/race_*`) — not compared.
- **Shared registration** (`configure.ac`, the solutions and Race projects, the Xcode
  project, `src/tests/Makefile.am`, `src/tools/`, `.github/workflows/`, `apple/`, `linux/`,
  `doc/`) — Race must edit these to exist; their content is checked by `verify_projects.py`,
  the workflow contract, and `verify_race_ui.py`.
- **Vendored additions** — declared third-party trees Race brings in.
- **Stock** — everything else. Must be byte-identical to the reference unless declared below.

The check fails on: an undeclared stock edit; a re-edit of a declared deviation; an upstream
change underneath a declared deviation; a declared deviation that no longer differs; a
deleted stock file; an untracked file dropped into stock source; and a vendored file that no
`EXTRA_DIST` entry distributes.

### Declared stock deviations

These are the only permitted differences from the reference. Each is pinned by both its
reference and its current canonical-LF SHA-256, so neither side can move silently. The
authoritative table is `STOCK_DEVIATIONS` in `src/tools/verify_race_overrides.py`.

| Stock file | Why it is accepted |
|---|---|
| `Quetoo.vs15/build_settings.props` | Parameterizes the dependency root so release and isolated builds can target an exact prerequisite stack without rebuilding the workspace siblings. |
| `Quetoo.vs15/COPY_DEPENDENCIES.bat` | Copies `ObjectivelyGPU.dll` into `bin/` and copies the SDL3 `3.4.12` and SDL3_image `3.4.4` runtimes selected by the sibling `sdl3.targets` files. The stock script omits ObjectivelyGPU itself and names stale SDL runtime versions, so a clean client can either fail to start or load DLLs that do not match the linked modules. Unrelated to Race - an upstream gap, and a candidate to send upstream rather than carry. |
| `deps/minizip/miniz.h` | Disables `MINIZ_USE_UNALIGNED_LOADS_AND_STORES`. Unaligned typed loads are undefined C; the bytewise path is required for arm64 targets. |
| `deps/Makefile.am` | Adds `EXTRA_DIST` for the vendored argon2 tree so it ships in the distribution tarball. |
| `src/cgame/ctf/Makefile.am` | Adds `-I$(top_srcdir)` so the normalized `client/ui` include paths resolve. |
| `src/cgame/default/Makefile.am` | Same. |
| `src/cgame/lithium/Makefile.am` | Same. |
| `src/client/ui/ui_types.h` | Normalizes two engine includes from `src/client/...` to `client/...` so the header resolves under each module include root. |
| `src/tests/check_cm_manifest.c` | Selects `DEFAULT_GAME` after `Fs_Init` so the fixture establishes the write directory required by the current filesystem lifecycle. Unrelated to Race — a candidate to send upstream. |
| `src/tests/check_cvar.c` | Same filesystem-lifecycle repair for the archived-cvar fixture. |
| `src/tests/check_filesystem.c` | Same filesystem-lifecycle repair for the search/read/write integration fixture. |
| `src/tests/check_master.c` | Selects `DEFAULT_GAME` after `Fs_Init` and updates the shutdown fixture for the authenticated two-argument `Ms_RemoveServer` contract already present in the integrated stock master. Unrelated to Race — a candidate to send upstream. |
| `src/tests/check_r_media.c` | Initializes the CPU-side occlusion list and installs a fixture-only `RenderDevice::waitForIdle` implementation required by the current `R_EndLoading` contract. Unrelated to Race — a candidate to send upstream. |
| `src/quemap/brush.c` | Guards BSP-node volume calculation when a split has no source brush. Unrelated to Race — a defensive compiler fix and candidate to send upstream rather than carry. |

The three `src/cgame/*/Makefile.am` rows are the only entries that change how a **stock
module** builds. They are the standing argument for sending the `ui_types.h` include
normalization upstream: if it lands there, four of these fourteen rows disappear.

The five `src/tests` rows repair fixtures for current stock filesystem,
master, and renderer contracts. They have nothing to do with Race and should
be sent upstream rather than carried. `src/quemap/brush.c` and
`COPY_DEPENDENCIES.bat` are similarly unrelated stock repairs. When the sibling
SDL targets advance, the copy-script deviation and its pinned canonical hash
must advance together so the deployed DLLs keep matching the modules built
against them.

### Declared vendored additions

| Tree | Distributed by | Purpose |
|---|---|---|
| `deps/argon2/` | `deps/Makefile.am` `EXTRA_DIST` | Pinned P-H-C Argon2 `20190702` reference subset, license, and recorded upstream hashes backing Race admin password hashing. `.gitattributes` fixes this tree to LF so the recorded hashes remain reproducible on Windows. |

Every file beneath a declared tree must appear in the named variable, so a vendored source
cannot silently fail to ship.

### Adding to these tables

A new row is an admission that Race changed something it does not own. Prefer, in order:
revert the change; shadow the file as a ledgered override; send the fix upstream. Declare it
only when none of those is possible, and record the reason in the constant itself — the
verifier's message points the next reader at it.

## Portability and drift obligations

- Production OS conditionals are limited to the Win32/POSIX filesystem
  facilities in `race_persistence.c`, cryptographic entropy facilities in
  `race_admin_password.c`, and the exact stock Win32 Discord initialization
  guard retained by `cg_discord.c`. Persistence behavior, schema, password
  hashing, and serialization remain common code; the Discord override changes
  only the reported mode label.
- Production Race code rejects GNU statement expressions, `typeof`, and
  omitted-middle conditional expressions; shared behavior remains standard C.
- Race GAME, CGAME, and focused movement-test targets compile with
  `-ffp-contract=off` in Autotools and Xcode and the exact clang-cl forwarding
  form in Visual Studio. Authoritative GAME, predicted CGAME, direct-AI, and
  replay movement all share serialized or compared states, so target-dependent
  FMA contraction is outside the Race numerical contract. The focused fixture
  translation units use the same policy because their synthetic collision
  arithmetic participates in exact movement-state assertions.
- Persistence walks POSIX paths through anchored directory descriptors with
  `O_NOFOLLOW`, rejects multiply linked final files, flushes candidates with
  `fsync` (or macOS `F_FULLFSYNC`), promotes with `renameat`, and treats the
  parent-directory `fsync` after promotion or removal as part of success.
  Win32 opens parent and file handles with `FILE_FLAG_OPEN_REPARSE_POINT`,
  rejects reparse, canonical-path, regular-file, and hard-link violations
  before mutation, flushes data with `FlushFileBuffers`, and promotes or
  removes by validated handle. Owner-only data uses mode `0600` on POSIX and a
  protected current-user-plus-LocalSystem DACL on Windows. Windows makes no
  POSIX-style parent-directory flush guarantee.
- Every persisted `gi.RealPath` result is copied through
  `Race_Persistence_CopyRealPath`. It verifies the complete virtual-path suffix
  and separator boundary and rejects absolute, empty, dot, dot-dot, backslash,
  and drive-qualified virtual components before accepting the stock fixed-size
  buffer, so a silently truncated or redirected host result cannot name
  another persistence object.
- Race administrator accounts read V1 through V4 and write the V4 account-only
  `race/admins.db` document. A V3 embedded Operator policy is candidate-written
  to `race/operator_cvars.txt` before the account document is rewritten. The
  external allowlist is sorted, independently reloadable, and fails closed for
  Operators if it is corrupt or semantically invalid. Credentials remain
  Argon2id v19 with `m=19456`, `t=2`, `p=1`, 16-byte random salts, and 32-byte
  tags. Login starts with local `radmin_password` plus `radmin <account>`.
  GAME issues a one-use, 15-second account-and-nonce challenge; CGAME performs
  Argon2id locally, clears the password cvar, and returns a keyed BLAKE2b proof
  bound to the canonical account and nonce. The raw password and derived tag do
  not enter client-command transport, and online GAME verification does not run
  Argon2id. Fixed canonical-account, normalized-address, and global failure
  budgets survive reconnects, while a global challenge budget sheds load.
  Account revisions invalidate active sessions after password, role,
  enablement, or removal changes. This changes no stock engine or module API
  boundary; locally entered password commands can still remain in console
  history.
- Q2Jump-style Race configuration owns real server cvars through a 15-entry
  registry. `race/gset.cfg` is the atomic global-assignment store, active
  `sv_map_list` catalog rows hold sparse map properties, and `mset`/`mclear`
  never restart or schedule the map they edit. The old `race/settings/*` and
  `settings/*` paths are detection-only migration notices; no release fixture or
  runtime path writes them.
- QRPL and Race wire formats use explicit-width integers, explicit
  little-endian encoding, bounded lengths, finite floating-point validation,
  and CRCs. Native structure layout is never serialized.
- `%llu`/`%lld` uses pass explicitly matching `unsigned long long`/`long long`
  values, avoiding LP64 versus LLP64 format ambiguity.
- The fixed QRPL v1 golden fixture must remain 550 bytes with replay ID
  `0xdba8d53d1dc24cab` and little-endian ID bytes
  `ab 4c c2 1d 3d d5 a8 db` at offset 52.
- `src/tests/race_portability_fixture.c` uses the production profile,
  leaderboard/map-state, settings, QRPL, finish-report, replay-transport, and
  physics codecs as a native corpus writer and reader. Its test-only `wire.bin`
  envelope decodes and canonically re-encodes finish, replay-state, training
  telemetry, projectile, raceline, and physics-identity records. Visual Studio
  and Autotools compile the same production source set; the Xcode release jobs
  run the Autotools test graph beside the native Xcode module build. Every
  official architecture must emit byte-identical fixture files before release.
- The following archived corpus SHA-256 values are historical pre-Q2Jump
  configuration evidence. The current Q2Jump fixture is recorded below.
  The historical profile value is
  `25fc83f868f9e8314c0846fdc8b6698fb39a8b1570a30335f7909b2a16b52297`,
  map state
  `74b677c047448b0ff4c73e4b05f64e9988cdbc617317161b7aed330904cd29c9`,
  settings
  `2601e96b6572c64503a6b363b06add8704bbf9f35897d32421a263b5ec099032`,
  QRPL
  `984dad083bb8bf48e3475c7a44fab35e14d949bbbcc688b952cbc0d1d5c9e237`,
  and wire corpus
  `6851888927b3a044b08aa7b1e03cd2a5fff23bf464f5238bdaf4a539502115c9`.
  QRPL read validation is semantic through `Race_Replay_Equals`: QRPL v1
  preserves an input signed-zero bit while `Vec3` normalizes signed zero during
  parse, so parse-and-reserialize identity is not a format contract. Original
  generated bytes and replay identity remain immutable and compared across hosts.
- Same-basename files must be rebased deliberately when the pinned stock
  reference changes. Refresh the named stock commit only as part of an explicit
  Quetoo source migration, review the complete old/new/override diff, then run
  project parity, focused tests, sanitizers, native loads, and installed-payload
  verification on every target.

## Historical evidence

Dated build, package, and runtime hashes from earlier source baselines remain in
their original reports. They are retained as test-method and defect history
only; they do not validate the current Quetoo reference, CGAME ABI, modules, or
release payload. Current acceptance must identify the exact Race commit,
`3fd1e9253284ad7f5431de17fe503475a9af704b` stock reference, dependency
revisions, artifact hashes, and newly executed validation results.
