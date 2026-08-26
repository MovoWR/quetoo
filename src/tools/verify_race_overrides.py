#!/usr/bin/env python3
"""Audit every Race override against the pinned current Quetoo source.

Two independent boundaries are enforced. The override audit proves each of the
ledgered Race overrides still matches its stock counterpart at the pinned
upstream source. The stock-tree integrity check proves that nothing *else* in the
stock tree changed: the engine, vendored dependencies, and the other modules'
build files must be byte-identical to the upstream reference commit unless the
difference is declared in STOCK_DEVIATIONS.
"""

import argparse
import difflib
import hashlib
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


BASELINE_COMMIT = "3fd1e9253284ad7f5431de17fe503475a9af704b"

# The upstream-only tip merged into this branch. Every file that is not
# Race-owned or shared build registration must be byte-identical to this
# commit; see validate_stock_tree_integrity. Advance it only as part of an
# explicit upstream merge, after re-reviewing STOCK_DEVIATIONS.
STOCK_REFERENCE_COMMIT = BASELINE_COMMIT

# Paths Race owns outright. Not compared against the stock reference.
RACE_OWNED_PREFIXES = (
  "src/game/race/",
  "src/cgame/race/",
  "share/race/",
  "src/tests/check_race",
  "src/tests/race_",
)

# Files Race must edit to exist at all: build registration, packaging, CI, and
# its own verifiers. Their *content* is checked by verify_projects.py, the
# workflow contract, and the UI verifier -- this check only refrains from
# treating them as untouchable stock.
SHARED_REGISTRATION_PREFIXES = (
  ".github/workflows/",
  "apple/",
  "linux/",
  "doc/",
  "src/tools/",
  "Quetoo.xcodeproj/xcshareddata/xcschemes/",
)

SHARED_REGISTRATION_FILES = (
  ".gitattributes",
  ".gitignore",
  "Makefile.am",
  "README.md",
  "configure.ac",
  "share/Makefile.am",
  "Quetoo.vs15/Smoke-QuetooRace.ps1",
  "Quetoo.vs15/quetoo.sln",
  "Quetoo.vs15/quetoo_all.sln",
  "Quetoo.vs15/cgame-race.vcxproj",
  "Quetoo.vs15/cgame-race.vcxproj.filters",
  "Quetoo.vs15/game-race.vcxproj",
  "Quetoo.vs15/game-race.vcxproj.filters",
  "Quetoo.vs15/check-race-native.vcxproj",
  "Quetoo.vs15/check-race-weapon-movement.vcxproj",
  "Quetoo.vs15/check-race-weapon-movement.vcxproj.filters",
  "Quetoo.vs15/check-race-weapon-tuning.vcxproj",
  "Quetoo.vs15/check-race-weapon-tuning.vcxproj.filters",
  "Quetoo.vs15/race-portability-fixture.vcxproj",
  "Quetoo.xcodeproj/project.pbxproj",
  "src/tests/Makefile.am",
)

# Stock files Race changed anyway, each pinned by (reference, current)
# canonical-LF SHA-256. A new stock edit is rejected because it is absent
# here; editing a listed file again is rejected because its current hash
# moves; an upstream change to a listed file is rejected because its
# reference hash moves. Removing a deviation requires deleting its row.
STOCK_DEVIATIONS = (
  ("Quetoo.vs15/build_settings.props",
   "6e1b5a2c54f72d63004cadc20b9b47346e507edcfeef6bf6b2efae324510d7ff",
   "b7a820ecd40ad034b0036317d24cd3bea42aeb8ba0365802c26741914c6d8cb6",
   "Parameterizes the dependency root so release and isolated builds can "
   "target an exact prerequisite stack without rebuilding the siblings."),
  ("Quetoo.vs15/COPY_DEPENDENCIES.bat",
   "7b979bb1acbd5ed870f25c5c1f2596f2c80b021d0249554c467d5fc41eff0969",
   "f5d2979b989abe4776f2204a9aec172e55fe0fb950c3f11376090f341c0be769",
   "Copies ObjectivelyGPU.dll into bin/. The stock script already takes SDL3 "
   "out of the ObjectivelyGPU tree but never the library itself, so a client "
   "built from a clean tree cannot start - ObjectivelyMVC links against it. "
   "The SDL3 and SDL3_image copy paths match the versions selected by the "
   "sibling sdl3.targets files, preventing stale runtime DLLs from being "
   "paired with newly linked modules. Upstream gap rather than a Race need; "
   "also terminates the final line, which stock leaves without a newline."),
  ("deps/minizip/miniz.h",
   "e9bc0ad11e0d67283b7fb254d907e7011509560f50072840d3c10973c2855795",
   "56115332814e44aadfd486d1afb428b1b56c1c93e1cbfcb3ec416e0d451c49a4",
   "Disables MINIZ_USE_UNALIGNED_LOADS_AND_STORES. Unaligned typed loads "
   "are undefined C; the bytewise path is required for arm64 targets."),
  ("src/cgame/ctf/Makefile.am",
   "27e62e39de8fa520ad54abcd516aaab4eac0e5da75fdbc84ffabdb98d31a8974",
   "4cd911fbe259ea4183400f5dc67eb1b2cd6bb5ba26dcca199f0a3aa658e1b117",
   "Adds -I$(top_srcdir) so the normalized client/ui include paths resolve."),
  ("src/cgame/default/Makefile.am",
   "50c2d381034b7411765740cd12af14bbbec98f63f15f8858f6d1ceb89a9e895e",
   "1d7913ed37b1feea55e4af4745a0f0ecd10096bbc6636558074cc04273a4641f",
   "Adds -I$(top_srcdir) so the normalized client/ui include paths resolve."),
  ("src/cgame/lithium/Makefile.am",
   "c0e16c9d2861cd07fbfd0c90fbc4d7ae3a6c2b0d6f69440607349f4c2e176b0b",
   "446805f814cd8ad7fa1c232420a49884dddb4d0aa44acee2848ebf20915938f3",
   "Adds -I$(top_srcdir) so the normalized client/ui include paths resolve."),
  ("src/client/ui/ui_types.h",
   "6102bd433ddb46195e9b22683e9e2eb0cb6d72d73e790f1c98bf4189d9086f39",
   "1661213a082e946c409dd1cabdfc2befa8a35816b85c3103bb48a4341b865b12",
   "Normalizes two engine includes from src/client/... to client/... so the "
   "header resolves under both host and standalone include roots."),
  ("deps/Makefile.am",
   "8830aa0699545067712a87581f1114011f6f72170b2c6d486f5a69dec922f8e4",
   "35d6a224f9ae07d5c8f4bede06c40897546bd6483bfa5468b5ddfd136565242d",
   "Adds EXTRA_DIST for the vendored argon2 tree so it ships in the "
   "distribution tarball. See VENDORED_ADDITIONS."),
  ("src/tests/check_cm_manifest.c",
   "38d6c5b3108c77dd1e9a59d49b204da7c7b777b703732042dd83ceb8e02824f4",
   "003f0d38689f1ac462a807cee79519610ce17d770247c29013940f77b4829382",
   "Selects DEFAULT_GAME after Fs_Init so the fixture establishes the write "
   "directory required by the current filesystem lifecycle. Unrelated to "
   "Race; a candidate to send upstream."),
  ("src/tests/check_cvar.c",
   "e60d9769a631175a6a71c8f22e2ab78839cae824d7433f0c8526920f8ecce413",
   "b932e36836db0cbaae03ce262d895f4e4e74ed7ae23f9d6a8cb6b673d2fa8969",
   "Selects DEFAULT_GAME after Fs_Init so the fixture establishes the write "
   "directory required by the current filesystem lifecycle. Unrelated to "
   "Race; a candidate to send upstream."),
  ("src/tests/check_filesystem.c",
   "bb2a48cff7f8d2d851c3d713f314408299dde66b7e15be7a1f03e7e717ce3fd5",
   "cff967fe57e18280ab40ec6a00a66465b2ad64a62e4797982383e8dac58eb057",
   "Selects DEFAULT_GAME after Fs_Init so the fixture exercises the current "
   "search and write path lifecycle. Unrelated to Race; a candidate to send "
   "upstream."),
  ("src/tests/check_master.c",
   "df17bc2be0e76129560faa45408c7c4df7ef44b5f264f649ebebdb3560348f07",
   "0aa7b18fa7f43aeb3031d5eb7f8e15701f8d6a8cbab6c314d7928f6d870d8c0b",
   "Selects DEFAULT_GAME after Fs_Init and updates the shutdown fixture for "
   "the authenticated two-argument Ms_RemoveServer contract already present "
   "in the integrated stock master. Unrelated to Race; a candidate to send "
   "upstream."),
  ("src/tests/check_r_media.c",
   "68ce0cd2abfabe4dba508a899eeba29e81cdd9fc548e897b5600b9f0611a6dae",
   "253bbcdc171ca3089efbaf1450c6b8107592aac5efbbab4616f49e8f1045d78f",
   "Initializes the CPU-side occlusion list and installs a fixture-only "
   "RenderDevice waitForIdle implementation required by the current "
   "R_EndLoading contract. Unrelated to Race; a candidate to send upstream."),
  ("src/quemap/brush.c",
   "c3c1c9023b2eef95ce57c109a57df54b487686de6cf19d647545d9bd2176715a",
   "70ac3c771315fb72cc8dc551bea517fa17dcabeb4109735e85232472f46adefb",
   "Guards SplitBrush BSP-node volumes, which have no source brush, before "
   "owner diagnostics and map-world bounds checks. Unrelated Quemap safety "
   "fix retained from the working tree; a candidate to send upstream."),
)

# Third-party trees Race adds under deps/. These are additions, not stock
# modifications, but they land inside the stock tree and must be declared:
# each prefix is allowed, and every file beneath it must be distributed by
# the named Makefile.am variable so it cannot silently fail to ship.
VENDORED_ADDITIONS = (
  ("deps/argon2/", "deps/Makefile.am", "EXTRA_DIST",
   "Argon2id reference implementation backing Race admin password hashing."),
)

# Directories whose contents are stock game or engine code. An untracked file
# appearing here is a stock-tree addition, not a Race addition.
STOCK_SOURCE_PREFIXES = (
  "src/game/common/",
  "src/cgame/common/",
  "src/game/default/",
  "src/game/ctf/",
  "src/game/lithium/",
  "src/cgame/default/",
  "src/cgame/ctf/",
  "src/cgame/lithium/",
  "src/client/",
  "src/server/",
  "src/shared/",
  "src/common/",
  "src/collision/",
  "src/net/",
  "src/master/",
  "src/main/",
  "src/quemap/",
  "src/ai/",
  "deps/",
)

GAME_COMMON = (
  "bg_pmove.c",
  "g_ai_main.c",
  "g_ai_node.c",
  "g_ballistics.c",
  "g_client.c",
  "g_client_stats.c",
  "g_client_view.c",
  "g_cmd.c",
  "g_combat.c",
  "g_entity.c",
  "g_hook.c",
  "g_main.c",
)

CGAME_COMMON = (
  "cg_discord.c",
  "cg_entity.c",
  "cg_main.c",
  "cg_media.c",
  "cg_predict.c",
  "cg_score.c",
  "cg_score.h",
)

MODULE_MANIFEST = (
  ("src/game/default/bg_item.c", "src/game/race/bg_item.c"),
  ("src/game/default/bg_item.h", "src/game/race/bg_item.h"),
  ("src/game/default/g_module.c", "src/game/race/g_module.c"),
  ("src/game/default/g_types.h", "src/game/race/g_types.h"),
  ("src/cgame/default/cg_module.c", "src/cgame/race/cg_module.c"),
)

UI_OVERRIDES = (
  "DialogViewController.css",
  "conback.png",
  "backgrounds/0.png",
  "backgrounds/1.png",
  "backgrounds/2.png",
  "backgrounds/3.png",
  "backgrounds/4.png",
  "backgrounds/5.png",
  "controls/ControlsViewController.c",
  "controls/ControlsViewController.h",
  "controls/ControlsViewController.json",
  "controls/CrosshairView.c",
  "controls/CrosshairView.h",
  "controls/MovementCombatViewController.c",
  "controls/MovementCombatViewController.css",
  "controls/MovementCombatViewController.h",
  "controls/MovementCombatViewController.json",
  "credits/CreditsViewController.c",
  "credits/CreditsViewController.css",
  "credits/CreditsViewController.h",
  "credits/CreditsViewController.json",
  "home/HomeViewController.c",
  "home/HomeViewController.css",
  "home/HomeViewController.h",
  "home/HomeViewController.json",
  "main/LoadingViewController.c",
  "main/LoadingViewController.css",
  "main/LoadingViewController.h",
  "main/LoadingViewController.json",
  "main/MainView.c",
  "main/MainView.css",
  "main/MainView.h",
  "main/MainView.json",
  "main/MainViewController.c",
  "main/MainViewController.h",
  "main/UpdateViewController.css",
  "play/CreateServerViewController.c",
  "play/CreateServerViewController.css",
  "play/CreateServerViewController.h",
  "play/CreateServerViewController.json",
  "play/JoinServerViewController.c",
  "play/JoinServerViewController.css",
  "play/JoinServerViewController.h",
  "play/JoinServerViewController.json",
  "play/PlayerSetupViewController.css",
  "play/PlayerSetupViewController.json",
  "settings/SettingsViewController.c",
  "settings/SettingsViewController.h",
  "settings/SettingsViewController.json",
)

COMPATIBILITY_FILES = (
  "src/cgame/race/cg_module_compat.h",
  "src/cgame/race/cg_race_barriers.c",
  "src/cgame/race/cg_race_barriers.h",
  "src/cgame/race/cg_team_mode.c",
  "src/cgame/race/cg_team_mode.h",
  "src/game/race/race_clip.c",
  "src/game/race/race_clip.h",
  "src/game/race/race_kick_broker.c",
  "src/game/race/race_kick_broker.h",
  "src/game/race/race_module_compat.h",
  "src/game/race/race_projectile_compat.h",
)

LEDGERED_OVERRIDE_SOURCES = frozenset((
  *(f"src/game/common/{name}" for name in GAME_COMMON),
  *(f"src/cgame/common/{name}" for name in CGAME_COMMON),
  *(stock for stock, _ in MODULE_MANIFEST),
  *(f"src/cgame/common/ui/{name}" for name in UI_OVERRIDES),
))

PORTABILITY_FIXTURE_SOURCES = (
  "src/game/race/race_finish_report.c",
  "src/game/race/race_leaderboard.c",
  "src/game/race/race_map_state.c",
  "src/game/race/race_physics.c",
  "src/game/race/race_profile.c",
  "src/game/race/race_replay_format.c",
  "src/game/race/race_replay_transport.c",
  "src/game/race/race_settings.c",
  "src/tests/race_portability_fixture.c",
)

RACE_NATIVE_TEST_SOURCES = (
  "src/tests/check_race_native.c",
  "src/tests/check_race_cgame_module.c",
  "src/tests/check_race_persistence_native.c",
  "src/tests/check_race_ui_native.c",
  "src/cgame/race/cg_module.c",
  "src/cgame/race/cg_race_admin_command.c",
  "src/cgame/race/cg_race_client_file.c",
  "src/cgame/race/cg_race_dashboard_layout.c",
  "src/cgame/race/cg_race_double_jump.c",
  "src/cgame/race/cg_score_model.c",
  "src/cgame/race/cg_team_mode.c",
  "src/cgame/race/ui/main/QuickSettingsHostView.c",
  "src/game/race/race_clip.c",
  "src/game/race/race_hook.c",
  "src/game/race/race_kick_broker.c",
  "src/game/race/race_persistence.c",
  "src/game/race/race_projectile_observer.c",
)

RACE_NATIVE_AUTOTOOLS_TARGET = "check_race_native"
RACE_NATIVE_PROJECT_TARGET = "check-race-native"
RACE_NATIVE_MARKER = (
  "RACE_NATIVE_TEST PASS assertions=100167 differential=100000"
)
RACE_WEAPON_MOVEMENT_AUTOTOOLS_TARGET = "check_race_weapon_movement"
RACE_WEAPON_MOVEMENT_PROJECT_TARGET = "check-race-weapon-movement"
RACE_WEAPON_MOVEMENT_MARKER = "Checks: 7, Failures: 0, Errors: 0"
RACE_WEAPON_TUNING_AUTOTOOLS_TARGET = "check_race_weapon_tuning"
RACE_WEAPON_TUNING_PROJECT_TARGET = "check-race-weapon-tuning"
RACE_WEAPON_TUNING_MARKER = (
  "RACE_WEAPON_TUNING_TEST PASS assertions=862 values=22"
)


@dataclass(frozen=True)
class Override:
  group: str
  stock: str
  race: str


@dataclass(frozen=True)
class WorkflowRunStep:
  job: str
  name: str
  script: str
  condition: str | None


def sha256(data: bytes) -> str:
  return hashlib.sha256(data).hexdigest()


BINARY_SUFFIXES = (".png", ".ttf", ".wav", ".tga", ".jpg")


def is_binary(relative: str) -> bool:
  """Whether an override is compared byte for byte rather than as text."""

  return relative.lower().endswith(BINARY_SUFFIXES)


def canonical_bytes(relative: str, data: bytes) -> bytes:
  """Canonicalize an override for comparison against its baseline blob.

  Text is reduced to the repository's LF form so a CRLF checkout compares
  equal. A binary override must not be touched: `canonical_text` rewrites
  every `0d 0a` byte pair it finds, which inside a PNG stream is data, not a
  line ending, and made an untouched stock image read as drifted.
  """

  return data if is_binary(relative) else canonical_text(data)


def canonical_text(data: bytes) -> bytes:
  """Return the repository's LF form independent of checkout line endings."""

  return data.replace(b"\r\n", b"\n")


def git_blob(root: Path, path: str) -> bytes:
  result = subprocess.run(
    ["git", "show", f"{BASELINE_COMMIT}:{path}"],
    cwd=root,
    check=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
  )
  return result.stdout


def line_delta(stock: bytes, race: bytes) -> tuple[int, int]:
  before = stock.decode("utf-8", errors="surrogateescape").splitlines()
  after = race.decode("utf-8", errors="surrogateescape").splitlines()
  added = 0
  deleted = 0
  for line in difflib.ndiff(before, after):
    if line.startswith("+ "):
      added += 1
    elif line.startswith("- "):
      deleted += 1
  return added, deleted


def override_manifest() -> tuple[Override, ...]:
  entries = [
    *(Override("GAME common", f"src/game/common/{name}",
               f"src/game/race/{name}") for name in GAME_COMMON),
    *(Override("CGAME common", f"src/cgame/common/{name}",
               f"src/cgame/race/{name}") for name in CGAME_COMMON),
    *(Override("module manifest", stock, race)
      for stock, race in MODULE_MANIFEST),
    *(Override("native UI", f"src/cgame/common/ui/{name}",
               f"src/cgame/race/ui/{name}") for name in UI_OVERRIDES),
  ]
  return tuple(entries)


def discover_overrides(root: Path) -> set[tuple[str, str]]:
  discovered = set()

  for kind in ("game", "cgame"):
    stock = root / "src" / kind / "common"
    race = root / "src" / kind / "race"
    for candidate in race.iterdir():
      baseline = stock / candidate.name
      if candidate.is_file() and baseline.is_file():
        discovered.add((baseline.relative_to(root).as_posix(),
                        candidate.relative_to(root).as_posix()))

  for stock, race in MODULE_MANIFEST:
    if (root / race).is_file():
      discovered.add((stock, race))

  stock_ui = root / "src" / "cgame" / "common" / "ui"
  race_ui = root / "src" / "cgame" / "race" / "ui"
  for candidate in race_ui.rglob("*"):
    if not candidate.is_file():
      continue
    relative = candidate.relative_to(race_ui)
    baseline = stock_ui / relative
    if baseline.is_file():
      discovered.add((baseline.relative_to(root).as_posix(),
                      candidate.relative_to(root).as_posix()))

  return discovered


def production_portability_errors(root: Path) -> list[str]:
  errors = []
  roots = (root / "src" / "game" / "race",
           root / "src" / "cgame" / "race")
  forbidden = {
    "GNU statement expression": re.compile(r"\(\s*\{"),
    "GNU omitted-middle conditional": re.compile(r"\?\s*:"),
    "GNU typeof": re.compile(r"\b(?:__)?typeof(?:__)?\b"),
    "packed native layout": re.compile(r"#\s*pragma\s+pack|__attribute__\s*\(\(\s*packed"),
  }
  os_conditional = re.compile(
    r"^\s*#\s*(?:if|ifdef|ifndef).*\b(?:_WIN32|WIN32|__APPLE__|__linux__)\b")
  os_conditional_owners = {
    "src/cgame/race/cg_discord.c",
    "src/game/race/race_admin_password.c",
    "src/game/race/race_persistence.c",
  }

  for source_root in roots:
    for path in sorted((*source_root.rglob("*.c"), *source_root.rglob("*.h"))):
      text = path.read_text(encoding="utf-8", errors="surrogateescape")
      relative = path.relative_to(root).as_posix()
      for label, pattern in forbidden.items():
        if pattern.search(text):
          errors.append(f"{relative}: {label}")
      for number, line in enumerate(text.splitlines(), 1):
        if os_conditional.search(line) and relative not in os_conditional_owners:
          errors.append(f"{relative}:{number}: OS conditional outside approved facility owners")
  return errors


def git_blob_at(root: Path, ref: str, path: str) -> bytes | None:
  """Return a path's bytes at a ref, or None when the ref does not track it."""

  result = subprocess.run(
    ["git", "show", f"{ref}:{path}"],
    cwd=root,
    stdout=subprocess.PIPE,
    stderr=subprocess.DEVNULL,
  )
  if result.returncode:
    return None
  return result.stdout


def git_lines(root: Path, args: list[str]) -> list[str]:
  result = subprocess.run(
    ["git", *args],
    cwd=root,
    check=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
  )
  return [line for line in result.stdout.splitlines() if line]


def stock_owned(
  relative: str,
  vendored: tuple = VENDORED_ADDITIONS,
  override_sources: frozenset[str] = LEDGERED_OVERRIDE_SOURCES,
) -> bool:
  """True when a path is stock and must match the stock reference exactly."""

  if relative.startswith(RACE_OWNED_PREFIXES):
    return False
  if relative in override_sources:
    return False
  if relative in SHARED_REGISTRATION_FILES:
    return False
  if relative.startswith(SHARED_REGISTRATION_PREFIXES):
    return False
  if relative.startswith(tuple(prefix for prefix, _, _, _ in vendored)):
    return False
  return True


def validate_stock_tree_integrity(
  root: Path,
  reference: str = STOCK_REFERENCE_COMMIT,
  deviations: tuple = STOCK_DEVIATIONS,
  vendored: tuple = VENDORED_ADDITIONS,
  override_sources: frozenset[str] = LEDGERED_OVERRIDE_SOURCES,
) -> list[str]:
  """Reject any change to a stock file that is not a declared deviation.

  The ledgered override audit owns each stock counterpart and requires it to
  match BASELINE_COMMIT. This independently compares every other stock
  path against the upstream-only reference commit and requires each difference
  to be declared in STOCK_DEVIATIONS with matching reference/current hashes.
  """

  errors = []

  declared = {relative: (reference_hash, current_hash, reason)
              for relative, reference_hash, current_hash, reason in deviations}
  seen = set()

  for relative in sorted(git_lines(root, ["diff", "--name-only", reference])):
    if not stock_owned(relative, vendored, override_sources):
      continue

    path = root / relative
    blob = git_blob_at(root, reference, relative)

    if blob is None:
      errors.append(
        f"stock tree addition: {relative} is absent from the stock reference "
        "and is not Race-owned; move it under a Race directory")
      continue

    if not path.is_file():
      errors.append(f"stock tree deletion: {relative}")
      continue

    reference_hash = sha256(canonical_text(blob))
    current_hash = sha256(canonical_text(path.read_bytes()))

    if relative not in declared:
      errors.append(
        f"undeclared stock edit: {relative} differs from stock reference "
        f"{reference[:9]}. Race must not change stock files; "
        "revert it, shadow it as a ledgered override, or declare it in "
        "STOCK_DEVIATIONS with a justification")
      continue

    seen.add(relative)
    expected_reference, expected_current, _ = declared[relative]

    if reference_hash != expected_reference:
      errors.append(
        f"stock deviation reference moved: {relative} expected "
        f"{expected_reference} got {reference_hash}; upstream changed this "
        "file, so re-review the deviation before repinning it")
    if current_hash != expected_current:
      errors.append(
        f"stock deviation content moved: {relative} expected "
        f"{expected_current} got {current_hash}; a declared deviation was "
        "edited again")

  for relative in sorted(set(declared) - seen):
    errors.append(
      f"stale stock deviation: {relative} no longer differs from the stock "
      "reference; remove its STOCK_DEVIATIONS row")

  vendored_prefixes = tuple(prefix for prefix, _, _, _ in vendored)
  for relative in sorted(git_lines(root, ["ls-files", "--others",
                                          "--exclude-standard"])):
    if relative.startswith(RACE_OWNED_PREFIXES):
      continue
    if relative.startswith(vendored_prefixes):
      continue
    if relative.startswith(STOCK_SOURCE_PREFIXES):
      errors.append(
        f"untracked file in stock source tree: {relative}; Race additions "
        "belong under a Race directory, and a new third-party tree must be "
        "declared in VENDORED_ADDITIONS")

  errors.extend(validate_vendored_additions(root, vendored))
  return errors


def validate_vendored_additions(
  root: Path, vendored: tuple = VENDORED_ADDITIONS) -> list[str]:
  """Require every file in a declared vendored tree to be distributed."""

  errors = []

  for prefix, makefile_relative, variable, _ in vendored:
    tree = root / prefix
    if not tree.is_dir():
      errors.append(f"missing vendored addition: {prefix}")
      continue

    makefile_path = root / makefile_relative
    if not makefile_path.is_file():
      errors.append(f"missing vendored manifest: {makefile_relative}")
      continue

    distributed = set(make_variable_tokens(
      makefile_path.read_text(encoding="utf-8"), variable))
    base = (root / makefile_relative).parent

    for path in sorted(tree.rglob("*")):
      if not path.is_file():
        continue
      relative = path.relative_to(root).as_posix()
      token = path.relative_to(base).as_posix()
      if token not in distributed:
        errors.append(
          f"undistributed vendored file: {relative} is absent from "
          f"{makefile_relative} {variable}")

  return errors


def validate_api_constants(root: Path) -> list[str]:
  expected = (
    ("src/game/game.h", r"#define\s+GAME_API_VERSION\s+33", "GAME API 33"),
    ("src/cgame/cgame.h", r"#define\s+CGAME_API_VERSION\s+35",
     "CGAME API 35"),
    ("src/common/common.h", r"#define\s+PROTOCOL_MAJOR\s+2029", "protocol major 2029"),
  )
  errors = []
  for relative, pattern, label in expected:
    path = root / relative
    if not path.is_file() or not re.search(pattern, path.read_text()):
      errors.append(f"{relative}: expected {label}")
  return errors


def validate_current_cgame_contract(root: Path) -> list[str]:
  """Require Race CGAME to use the same current public ABI as the host."""

  errors = []

  header_path = root / "src/cgame/cgame.h"
  header = header_path.read_text(encoding="utf-8") if header_path.is_file() else ""
  for marker in (
    "#include <Objectively/PointerArray.h>",
    "#define CGAME_API_VERSION 35",
    "PointerArray *(*Servers)(void);",
  ):
    if marker not in header:
      errors.append(f"src/cgame/cgame.h: missing current contract marker: {marker}")

  race_header_path = root / "src/cgame/race/ui/play/JoinServerViewController.h"
  race_source_path = root / "src/cgame/race/ui/play/JoinServerViewController.c"
  race_header = race_header_path.read_text(encoding="utf-8") if race_header_path.is_file() else ""
  race_source = race_source_path.read_text(encoding="utf-8") if race_source_path.is_file() else ""

  if "PointerArray *servers;" not in race_header:
    errors.append(
      "Race Join Server controller must store the host PointerArray directly")
  if "const PointerArray *servers = cgi.Servers();" not in race_source:
    errors.append(
      "Race Join Server controller must consume the host PointerArray directly")
  if re.search(r"\b(?:List|ListNode)\b", race_header + race_source):
    errors.append(
      "Race Join Server controller still contains the retired List contract")

  retired_path = root / "src/cgame/race/api34"
  if retired_path.is_dir() and any(path.is_file()
                                   for path in retired_path.rglob("*")):
    errors.append("retired Race API-34 compatibility directory still exists")

  build_files = (
    "src/cgame/race/Makefile.am",
    "Quetoo.vs15/cgame-race.vcxproj",
    "Quetoo.vs15/cgame-race.vcxproj.filters",
    "Quetoo.xcodeproj/project.pbxproj",
  )
  for relative in build_files:
    path = root / relative
    text = path.read_text(encoding="utf-8") if path.is_file() else ""
    if re.search(r"api[-_]?34", text, re.IGNORECASE):
      errors.append(f"{relative}: retired API-34 build registration remains")

  return errors


def validate_portability_fixture_graph(root: Path) -> list[str]:
  errors = []
  makefile = (root / "src/tests/Makefile.am").read_text()
  project = (root / "Quetoo.vs15/race-portability-fixture.vcxproj").read_text()
  xcode = (root / "Quetoo.xcodeproj/project.pbxproj").read_text()

  if "race_portability_fixture" not in makefile:
    errors.append("src/tests/Makefile.am: portability fixture target missing")
  if "$(top_builddir)/deps/minizip/libminizip.la" not in makefile:
    errors.append("src/tests/Makefile.am: portability fixture miniz dependency missing")
  if "..\\deps\\minizip\\miniz.c" not in project:
    errors.append("Quetoo.vs15 portability fixture: miniz source missing")
  if "race_portability_fixture.c" not in xcode:
    errors.append("Quetoo.xcodeproj: portability fixture source is not tracked")
  for solution in ("Quetoo.vs15/quetoo.sln", "Quetoo.vs15/quetoo_all.sln"):
    if "race-portability-fixture.vcxproj" not in (root / solution).read_text():
      errors.append(f"{solution}: portability fixture project missing")

  for relative in PORTABILITY_FIXTURE_SOURCES:
    make_path = Path(relative).name if relative.startswith("src/tests/") \
      else f"$(top_srcdir)/{relative}"
    project_path = "..\\" + relative.replace("/", "\\")
    if make_path not in makefile:
      errors.append(f"src/tests/Makefile.am: fixture source missing: {relative}")
    if project_path not in project:
      errors.append(
        f"Quetoo.vs15/race-portability-fixture.vcxproj: source missing: {relative}")
  return errors


def make_variable_tokens(makefile: str, variable: str) -> tuple[str, ...]:
  """Return the whitespace-separated tokens in one continued Make variable."""

  lines = makefile.splitlines()
  for index, line in enumerate(lines):
    match = re.fullmatch(rf"{re.escape(variable)}\s*=\s*(.*)", line)
    if not match:
      continue

    chunks = []
    value = match.group(1)
    while True:
      value = value.rstrip()
      continued = value.endswith("\\")
      chunks.append(value[:-1] if continued else value)
      if not continued or index + 1 >= len(lines):
        break
      index += 1
      value = lines[index].strip()
    return tuple(" ".join(chunks).split())
  return ()


def validate_race_native_test_graph(root: Path) -> list[str]:
  errors = []
  makefile = (root / "src/tests/Makefile.am").read_text()
  project = (root / "Quetoo.vs15/check-race-native.vcxproj").read_text()
  xcode = (root / "Quetoo.xcodeproj/project.pbxproj").read_text()
  scheme = root / "Quetoo.xcodeproj/xcshareddata/xcschemes/check-race-native.xcscheme"
  test_targets = set(make_variable_tokens(makefile, "TESTS"))

  if RACE_NATIVE_AUTOTOOLS_TARGET not in test_targets:
    errors.append("src/tests/Makefile.am: native Race test missing from TESTS")
  if "check_race_native_SOURCES" not in makefile:
    errors.append("src/tests/Makefile.am: native Race test target missing")
  if f"<TargetName>{RACE_NATIVE_PROJECT_TARGET}</TargetName>" not in project:
    errors.append(
      "Quetoo.vs15/check-race-native.vcxproj: native test target name differs")
  if (f'name = "{RACE_NATIVE_PROJECT_TARGET}";' not in xcode
      or not scheme.is_file()):
    errors.append("Quetoo.xcodeproj: native Race test target or scheme missing")
  elif (f'BuildableName="{RACE_NATIVE_PROJECT_TARGET}"' not in scheme.read_text()
        or f'BlueprintName="{RACE_NATIVE_PROJECT_TARGET}"' not in scheme.read_text()):
    errors.append("Quetoo.xcodeproj: native Race scheme target name differs")
  for solution in ("Quetoo.vs15/quetoo.sln", "Quetoo.vs15/quetoo_all.sln"):
    if "check-race-native.vcxproj" not in (root / solution).read_text():
      errors.append(f"{solution}: native Race test project missing")

  native_source = (root / "src/tests/check_race_native.c").read_text()
  if ('printf("RACE_NATIVE_TEST PASS assertions=%" PRIu32' not in native_source
      or '" differential=100000\\n"' not in native_source):
    errors.append("src/tests/check_race_native.c: native integration marker differs")

  for relative in RACE_NATIVE_TEST_SOURCES:
    basename = Path(relative).name
    make_path = basename if relative.startswith("src/tests/") \
      else f"$(top_srcdir)/{relative}"
    project_path = "..\\" + relative.replace("/", "\\")
    if make_path not in makefile:
      errors.append(f"src/tests/Makefile.am: native test source missing: {relative}")
    if project_path not in project:
      errors.append(
        f"Quetoo.vs15/check-race-native.vcxproj: source missing: {relative}")
    if basename not in xcode:
      errors.append(f"Quetoo.xcodeproj: native test source missing: {relative}")
  return errors


def replace_exact(text: str, old: str, new: str, count: int = 1) -> str:
  if text.count(old) != count:
    raise ValueError(f"expected {count} exact occurrence(s): {old!r}")
  return text.replace(old, new)


def validate_ai_override_delta(root: Path) -> list[str]:
  errors = []
  try:
    main = canonical_text(git_blob(root, "src/game/common/g_ai_main.c")).decode()
    main = replace_exact(
      main, '#include "bg_pmove.h"\n',
      '#include "bg_pmove.h"\n#include "race_module_compat.h"\n')
    main = replace_exact(
      main,
      "    return gi.Trace(start, end, bounds, ent, CONTENTS_MASK_CLIP_CORPSE);",
      "    return G_Module_TraceMovement(g_ai_current_entity, start, end, bounds,\n"
      "                                  CONTENTS_MASK_CLIP_CORPSE);")
    main = replace_exact(
      main,
      "    return gi.Trace(start, end, bounds, ent, CONTENTS_MASK_CLIP_PLAYER);",
      "    return G_Module_TraceMovement(g_ai_current_entity, start, end, bounds,\n"
      "                                  ent->clip_mask);")
    main = replace_exact(main, "Box3_Size(PM_BOUNDS).z",
                          "Box3_Size(Pm_PlayerBounds(false)).z", 2)
    main = replace_exact(
      main,
      "  pm.debug_mask = DEBUG_PMOVE_SERVER;\n\n"
      "  // perform a move; predict our next frame",
      "  pm.debug_mask = DEBUG_PMOVE_SERVER;\n\n"
      "  // These Pm_Move calls are AI lookahead only. Their traces must not mutate\n"
      "  // the authoritative one-way-wall state consumed by the client's real move.\n"
      "  const uint64_t oneway_latches = cl->race_oneway_latches;\n\n"
      "  // perform a move; predict our next frame")
    main = replace_exact(
      main,
      "    cl->ai->lookahead_no_ground = !pm_ahead.ground.ent;\n"
      "  }\n\n"
      "  // predicted ground is gone",
      "    cl->ai->lookahead_no_ground = !pm_ahead.ground.ent;\n"
      "  }\n"
      "  cl->race_oneway_latches = oneway_latches;\n\n"
      "  // predicted ground is gone")

    node = canonical_text(git_blob(root, "src/game/common/g_ai_node.c")).decode()
    node = replace_exact(
      node, '#include "bg_pmove.h"\n',
      '#include "bg_pmove.h"\n#include "race_module_compat.h"\n')
    node = replace_exact(
      node, "bool G_Ai_Node_CanPathTo(const vec3_t position) {\n\n",
      "bool G_Ai_Node_CanPathTo(const vec3_t position) {\n\n"
      "  const box3_t player_bounds = Pm_PlayerBounds(false);\n\n")
    node = replace_exact(
      node, "Box3_Expand3(PM_BOUNDS, Vec3(1.f, 1.f, 0.f))",
      "Box3_Expand3(player_bounds, Vec3(1.f, 1.f, 0.f))")
    node = replace_exact(
      node,
      "Box3(Vec3(-4.f, -4.f, PM_BOUNDS.mins.z), Vec3(4.f, 4.f, PM_BOUNDS.maxs.z))",
      "Box3(Vec3(-4.f, -4.f, player_bounds.mins.z), Vec3(4.f, 4.f, player_bounds.maxs.z))")
    node = replace_exact(
      node, "#define AI_NODE_VERSION 2\n",
      "#define AI_NODE_VERSION 2\n"
      "#define AI_NODE_MAX_FILE_BYTES (64u * 1024u * 1024u)\n\n"
      "typedef struct {\n"
      "  uint16_t id;\n"
      "  uint16_t reserved;\n"
      "  float cost;\n"
      "} ai_node_file_link_t;\n\n"
      "_Static_assert(sizeof(ai_node_file_link_t) == 8u,\n"
      "               \"Navigation v2 requires eight-byte link records\");\n")
    node = replace_exact(
      node,
      "  file_t *file = gi.OpenFile(filename);\n"
      "  int32_t magic, version;\n"
      "  \n"
      "  gi.ReadFile(file, &magic, sizeof(magic), 1);\n",
      "  file_t *file = gi.OpenFile(filename);\n"
      "  if (!file) {\n"
      "    G_Warn(\"Could not open navigation file %s\\n\", filename);\n"
      "    return;\n"
      "  }\n"
      "  int32_t magic = 0, version = 0;\n"
      "  uint32_t num_nodes = 0u;\n"
      "  size_t file_bytes = sizeof(magic) + sizeof(version) + sizeof(num_nodes);\n"
      "  size_t total_links = 0u;\n\n"
      "  if (gi.ReadFile(file, &magic, sizeof(magic), 1) != 1) {\n"
      "    goto invalid;\n"
      "  }\n"
      "  magic = LittleLong(magic);\n")
    node = replace_exact(
      node,
      "  gi.ReadFile(file, &version, sizeof(version), 1);\n",
      "  if (gi.ReadFile(file, &version, sizeof(version), 1) != 1) {\n"
      "    goto invalid;\n"
      "  }\n"
      "  version = LittleLong(version);\n")
    node = replace_exact(
      node,
      "  uint32_t num_nodes;\n"
      "  gi.ReadFile(file, &num_nodes, sizeof(num_nodes), 1);\n",
      "  if (gi.ReadFile(file, &num_nodes, sizeof(num_nodes), 1) != 1 ||\n"
      "      (num_nodes = (uint32_t) LittleLong((int32_t) num_nodes)) >\n"
      "        (uint32_t) AI_NODE_INVALID) {\n"
      "    goto invalid;\n"
      "  }\n")
    node = replace_exact(node, "  size_t total_links = 0;\n\n", "")
    node = replace_exact(
      node,
      "    gi.ReadFile(file, &node->position, sizeof(node->position), 1);\n\n"
      "    uint32_t num_links;\n"
      "  \n"
      "    gi.ReadFile(file, &num_links, sizeof(num_links), 1);\n",
      "    if (file_bytes > AI_NODE_MAX_FILE_BYTES - sizeof(node->position) -\n"
      "                     sizeof(uint32_t) ||\n"
      "        gi.ReadFile(file, &node->position, sizeof(node->position), 1) != 1) {\n"
      "      goto invalid;\n"
      "    }\n"
      "    node->position = LittleVec3(node->position);\n"
      "    if (!isfinite(node->position.x) || !isfinite(node->position.y) ||\n"
      "        !isfinite(node->position.z) ||\n"
      "        fabsf(node->position.x) > MAX_WORLD_COORD ||\n"
      "        fabsf(node->position.y) > MAX_WORLD_COORD ||\n"
      "        fabsf(node->position.z) > MAX_WORLD_COORD) {\n"
      "      goto invalid;\n"
      "    }\n"
      "    file_bytes += sizeof(node->position);\n\n"
      "    uint32_t num_links;\n\n"
      "    if (gi.ReadFile(file, &num_links, sizeof(num_links), 1) != 1) {\n"
      "      goto invalid;\n"
      "    }\n"
      "    num_links = (uint32_t) LittleLong((int32_t) num_links);\n"
      "    file_bytes += sizeof(num_links);\n"
      "    if (num_links > num_nodes ||\n"
      "        num_links > (AI_NODE_MAX_FILE_BYTES - file_bytes) /\n"
      "                      sizeof(ai_node_file_link_t)) {\n"
      "      goto invalid;\n"
      "    }\n")
    node = replace_exact(
      node,
      "      gi.ReadFile(file, node->links->elements, sizeof(ai_link_t), num_links);\n"
      "      total_links += num_links;\n",
      "      for (size_t l = 0; l < num_links; l++) {\n"
      "        ai_node_file_link_t encoded;\n"
      "        if (gi.ReadFile(file, &encoded, sizeof(encoded), 1) != 1) {\n"
      "          goto invalid;\n"
      "        }\n"
      "        ai_link_t *link = AI_LINK(node->links, l);\n"
      "        link->id = (ai_node_id_t) LittleShort((int16_t) encoded.id);\n"
      "        link->cost = LittleFloat(encoded.cost);\n"
      "        if (encoded.reserved || link->id >= num_nodes ||\n"
      "            !isfinite(link->cost) || link->cost < 0.f) {\n"
      "          goto invalid;\n"
      "        }\n"
      "      }\n"
      "      file_bytes += num_links * sizeof(ai_node_file_link_t);\n"
      "      total_links += num_links;\n")
    node = replace_exact(
      node,
      "      g_ai_player_roam.file_links += (uint32_t) node->links->count;\n"
      "    }\n"
      "  }\n"
      "}\n\n"
      "/**\n"
      " * @brief Validates node integrity",
      "      g_ai_player_roam.file_links += (uint32_t) node->links->count;\n"
      "    }\n"
      "  }\n"
      "  return;\n\n"
      "invalid:\n"
      "  gi.CloseFile(file);\n"
      "  G_Ai_ShutdownNodes();\n"
      "  G_Warn(\"Navigation file %s is truncated, malformed, or exceeds safe bounds\\n\",\n"
      "         filename);\n"
      "}\n\n"
      "/**\n"
      " * @brief Validates node integrity")
    node = replace_exact(
      node,
      "  file_t *file = gi.OpenFileWrite(filename);\n"
      "  int32_t magic = AI_NODE_MAGIC;\n"
      "  int32_t version = AI_NODE_VERSION;\n",
      "  file_t *file = gi.OpenFileWrite(filename);\n"
      "  int32_t magic = LittleLong(AI_NODE_MAGIC);\n"
      "  int32_t version = LittleLong(AI_NODE_VERSION);\n")
    node = replace_exact(
      node,
      "  const uint32_t num_nodes = (uint32_t) g_ai_nodes->count;\n",
      "  const uint32_t num_nodes = (uint32_t) LittleLong(\n"
      "    (int32_t) g_ai_nodes->count);\n")
    node = replace_exact(
      node,
      "    gi.WriteFile(file, &node->position, sizeof(node->position), 1);\n",
      "    const vec3_t position = LittleVec3(node->position);\n"
      "    gi.WriteFile(file, &position, sizeof(position), 1);\n")
    node = replace_exact(
      node,
      "      const uint32_t num_links = (uint32_t) node->links->count;\n"
      "      gi.WriteFile(file, &num_links, sizeof(num_links), 1);\n"
      "      gi.WriteFile(file, node->links->elements, sizeof(ai_link_t), node->links->count);\n",
      "      const uint32_t num_links = (uint32_t) LittleLong(\n"
      "        (int32_t) node->links->count);\n"
      "      gi.WriteFile(file, &num_links, sizeof(num_links), 1);\n"
      "      for (size_t l = 0; l < node->links->count; l++) {\n"
      "        const ai_link_t *link = AI_LINK(node->links, l);\n"
      "        const ai_node_file_link_t encoded = {\n"
      "          .id = (uint16_t) LittleShort((int16_t) link->id),\n"
      "          .cost = LittleFloat(link->cost)\n"
      "        };\n"
      "        gi.WriteFile(file, &encoded, sizeof(encoded), 1);\n"
      "      }\n")
    node = replace_exact(
      node, "      uint32_t len = 0;\n",
      "      uint32_t len = (uint32_t) LittleLong(0);\n")
  except (UnicodeError, ValueError) as error:
    return [f"AI override baseline transform failed: {error}"]

  expected = {
    "src/game/race/g_ai_main.c": main.encode(),
    "src/game/race/g_ai_node.c": node.encode(),
  }
  for relative, content in expected.items():
    actual = canonical_text((root / relative).read_bytes())
    if actual != content:
      errors.append(
        f"{relative}: not the exact pinned stock blob plus approved Race delta")
  return errors


def validate_xcode_runtime_contract(root: Path) -> list[str]:
  """Ensure Race inherits and verifies the current Quetoo macOS contract."""

  errors = []
  project = (root / "Quetoo.xcodeproj/project.pbxproj").read_text()
  configuration_ids = (
    "FA11C2D33022486E0012862B",
    "FA11C2D43022486E0012862B",
    "FA11C3F030224900C0DE0910",
    "FA11C3F030224900C0DE0911",
  )

  for configuration_id in configuration_ids:
    match = re.search(
      rf"\t\t{configuration_id} /\* .*? \*/ = \{{.*?"
      rf"buildSettings = \{{(?P<body>.*?)\n\t\t\t\}};",
      project,
      re.DOTALL,
    )
    if not match:
      errors.append(
        f"Quetoo.xcodeproj: Race configuration missing: {configuration_id}")
      continue
    if "MACOSX_DEPLOYMENT_TARGET" in match.group("body"):
      errors.append(
        "Quetoo.xcodeproj: Race configuration must inherit the current "
        f"project deployment target: {configuration_id}")

  baseline_project = canonical_text(git_blob(
    root, "Quetoo.xcodeproj/project.pbxproj")).decode(
      "utf-8", errors="surrogateescape")
  baseline_targets = re.findall(
    r"MACOSX_DEPLOYMENT_TARGET = ([0-9.]+);", baseline_project)
  current_targets = re.findall(
    r"MACOSX_DEPLOYMENT_TARGET = ([0-9.]+);", project)
  if not baseline_targets:
    errors.append("Quetoo.xcodeproj: current baseline has no deployment target")
  elif current_targets != baseline_targets:
    errors.append(
      "Quetoo.xcodeproj: Race must not add or replace the current project "
      f"deployment targets; expected {baseline_targets}, found {current_targets}")

  verifier = (root / "apple/Verify-QuetooRace.sh").read_text()
  for marker in (
      'host="$app/Contents/MacOS/quetoo"',
      "host_deployment_targets=()",
      "Race module does not match the Quetoo host deployment target",
      "deployment_targets=",
  ):
    if marker not in verifier:
      errors.append(
        f"apple/Verify-QuetooRace.sh: host parity gate missing: {marker}")

  smoke = (root / "apple/Smoke-QuetooRace.sh").read_text()
  for marker in (
      "matching-source Quetoo app",
      'module_root="$app/Contents/MacOS/lib/quetoo/race"',
      'resource_root="$app/Contents/Resources/race"',
      "+game race",
      "client_sha256=",
      "dedicated_sha256=",
  ):
    if marker not in smoke:
      errors.append(
        f"apple/Smoke-QuetooRace.sh: current runtime contract missing: {marker}")
  for retired in ("standalone Race package", "stock_client_sha256="):
    if retired in smoke:
      errors.append(
        f"apple/Smoke-QuetooRace.sh: retired overlay contract remains: {retired}")

  workflow = (root / ".github/workflows/release.yml").read_text()
  for marker in (
      "- name: Verify and test Race",
      "--root apple/target/Quetoo.app --layout macos-app",
      "make -C apple verify-race ARCH=",
      "--root arm64/Quetoo.app --layout macos-app",
      "make -C quetoo/apple verify-race",
  ):
    if marker not in workflow:
      errors.append(
        f"release workflow: current macOS Race gate missing: {marker}")

  for relative in (
      "apple/Package-QuetooRace.sh",
      "apple/Install-QuetooRace.sh",
  ):
    if (root / relative).exists():
      errors.append(f"{relative}: retired standalone entry point remains")
  return errors


def validate_linux_runtime_contract(root: Path) -> list[str]:
  """Validate the matching-source Linux package and smoke contracts."""

  errors = []
  smoke = (root / "linux/Smoke-QuetooRace.sh").read_text()
  for marker in (
      "matching-source",
      'race_module_root="$quetoo_root/lib/quetoo/race"',
      'race_data_root="$quetoo_root/share/quetoo/race"',
      "+game race",
      "client_sha256=",
      "dedicated_sha256=",
      'runtime_evidence="$output/runtime-evidence.meta"',
  ):
    if marker not in smoke:
      errors.append(
        f"linux/Smoke-QuetooRace.sh: current runtime contract missing: {marker}")
  for retired in (
      "standalone Race package",
      "stock_client_sha256=",
      "expected_dedicated_sha256=",
  ):
    if retired in smoke:
      errors.append(
        f"linux/Smoke-QuetooRace.sh: retired overlay contract remains: {retired}")

  workflow = (root / ".github/workflows/release.yml").read_text()
  for marker in (
      "  build-linux:",
      "./configure --with-version=$(git rev-list --count HEAD) --with-tests",
      "- name: Verify and test Race",
      "make archive deb rpm VERSION=",
      "--root linux/target/staging --layout linux",
      "matrix.build }}-archive",
  ):
    if marker not in workflow:
      errors.append(
        f"release workflow: current Linux Race gate missing: {marker}")

  for relative in (
      "linux/Package-QuetooRace.sh",
      "linux/Install-QuetooRace.sh",
  ):
    if (root / relative).exists():
      errors.append(f"{relative}: retired standalone entry point remains")
  return errors


def validate_windows_runtime_contract(root: Path) -> list[str]:
  """Validate the matching-source Windows package and smoke contracts."""

  errors = []
  smoke = (root / "Quetoo.vs15/Smoke-QuetooRace.ps1").read_text()
  for marker in (
      "$game = Join-Path $QuetooRoot 'lib\\race\\game.dll'",
      "$cgame = Join-Path $QuetooRoot 'lib\\race\\cgame.dll'",
      "$dedicatedStdin = Join-Path $OutputRoot 'dedicated.stdin'",
      "-RedirectStandardInput $dedicatedStdin",
      "pid=$($Process.Id) exit=$($Process.ExitCode)",
      "$clientHash = (Get-FileHash -LiteralPath $client",
      '"client_sha256=$clientHash"',
      '"dedicated_sha256=$dedicatedHash"',
      "+game', 'race",
  ):
    if marker not in smoke:
      errors.append(
        f"Quetoo.vs15/Smoke-QuetooRace.ps1: current runtime gate missing: {marker}")
  for retired in (
      "$ExpectedQuetooHash",
      "$ExpectedDedicatedHash",
      "stock_client_sha256=",
  ):
    if retired in smoke:
      errors.append(
        "Quetoo.vs15/Smoke-QuetooRace.ps1: retired frozen-host "
        f"contract remains: {retired}")

  workflow = (root / ".github/workflows/release.yml").read_text()
  for marker in (
      "QUETOO_HOME:",
      "\\race-stage",
      'Copy-Item "Quetoo.vs15\\bin\\x64Release\\race\\cgame.dll"',
      'Copy-Item "Quetoo.vs15\\bin\\x64Release\\race\\game.dll"',
      '$raceUiSource = "src\\cgame\\race\\ui"',
      'Copy-Item "share\\race\\manifest.mf","share\\race\\maps.lst"',
      "--root windows/target/quetoo --layout windows",
  ):
    if marker not in workflow:
      errors.append(
        f"release workflow: current Windows Race gate missing: {marker}")

  for relative in (
      "Quetoo.vs15/Package-QuetooRace.ps1",
      "Quetoo.vs15/Install-QuetooRace.ps1",
      "Quetoo.vs15/Install-QuetooRace.cmd",
  ):
    if (root / relative).exists():
      errors.append(f"{relative}: retired standalone entry point remains")
  return errors


def validate_runtime_portability_contract(root: Path) -> list[str]:
  """Require native persistence fixtures in build and release workflows."""

  errors = []
  build = (root / ".github/workflows/build.yml").read_text()
  release = (root / ".github/workflows/release.yml").read_text()

  for marker in (
      "src/tests/race_portability_fixture --write",
      "src/tests/race_portability_fixture --verify",
      "race-portability-fixture.exe' --write",
      "race-portability-fixture.exe' --verify",
  ):
    if marker not in build:
      errors.append(f"build workflow: portability fixture gate missing: {marker}")

  if release.count("src/tests/race_portability_fixture --write") < 2:
    errors.append(
      "release workflow: macOS and Linux portability write gates are required")
  if release.count("src/tests/race_portability_fixture --verify") < 2:
    errors.append(
      "release workflow: macOS and Linux portability verify gates are required")
  for marker in (
      "race-portability-fixture.exe' --write",
      "race-portability-fixture.exe' --verify",
  ):
    if marker not in release:
      errors.append(
        f"release workflow: Windows portability fixture gate missing: {marker}")
  return errors

def workflow_job_blocks(workflow: str) -> dict[str, str]:
  """Return top-level workflow job blocks without requiring a YAML package."""
  lines = workflow.splitlines()
  jobs_index = next((i for i, line in enumerate(lines) if line == "jobs:"), -1)
  if jobs_index == -1:
    return {}

  jobs = {}
  current = None
  block = []
  for line in lines[jobs_index + 1:]:
    if line and not line.startswith(" "):
      break
    match = re.fullmatch(r"  ([A-Za-z0-9_-]+):", line)
    if match:
      if current is not None:
        jobs[current] = "\n".join(block)
      current = match.group(1)
      block = [line]
    elif current is not None:
      block.append(line)
  if current is not None:
    jobs[current] = "\n".join(block)
  return jobs


def workflow_run_steps(workflow: str) -> tuple[WorkflowRunStep, ...]:
  """Return executable workflow steps and their run scalars."""

  steps = []
  for job_name, job in workflow_job_blocks(workflow).items():
    lines = job.splitlines()
    starts = [index for index, line in enumerate(lines)
              if re.match(r"^    -(?:\s|$)", line)]
    starts.append(len(lines))
    for offset in range(len(starts) - 1):
      block = lines[starts[offset]:starts[offset + 1]]
      name_match = re.match(r"^    -\s+name:\s*(.*?)\s*$", block[0])
      name = name_match.group(1).strip("'\"") if name_match else ""
      condition = None
      script = None
      for index, line in enumerate(block):
        condition_match = re.match(r"^      if:\s*(.*?)\s*$", line)
        if condition_match:
          condition = condition_match.group(1)
        run_match = re.match(r"^      run:\s*(.*?)\s*$", line)
        if not run_match:
          continue
        scalar = run_match.group(1)
        if scalar.startswith(("|", ">")):
          body = []
          for body_line in block[index + 1:]:
            if body_line and len(body_line) - len(body_line.lstrip()) < 8:
              break
            body.append(body_line[8:] if len(body_line) >= 8 else "")
          script = "\n".join(body)
        else:
          script = scalar
        break
      if script is not None:
        steps.append(WorkflowRunStep(job_name, name, script, condition))
  return tuple(steps)


def workflow_test_target_references(script: str) -> tuple[str, ...]:
  """Extract actual Race test targets from one run scalar."""

  references = []
  lines = [line for line in script.splitlines()
           if not line.lstrip().startswith("#")]
  executable = "\n".join(lines)

  for match in re.finditer(
      r"\bTESTS\s*=\s*(?:'([^']*)'|\"([^\"]*)\"|([^\s\\]+))",
      executable):
    value = next(group for group in match.groups() if group is not None)
    references.extend(token for token in value.split()
                      if re.fullmatch(r"check[-_]race[A-Za-z0-9_-]*", token))

  unix_direct = re.compile(
    r"(?:^|\$\()\s*"
    r"(?:[A-Za-z_][A-Za-z0-9_]*=(?:'[^']*'|\"[^\"]*\"|\S+)\s+)*"
    r"(?:\./)?src/tests/(check[-_]race[A-Za-z0-9_-]*)(?=\s|\)|$)")
  windows_direct = re.compile(
    r"^\s*(?:\$[A-Za-z_][A-Za-z0-9_]*\s*=\s*)?&\s+"
    r"['\"]?[^'\"\r\n]*[\\/](check[-_]race[A-Za-z0-9_-]*)\.exe",
    re.IGNORECASE)
  for line in lines:
    match = unix_direct.search(line) or windows_direct.match(line)
    if match:
      references.append(match.group(1))
  return tuple(dict.fromkeys(references))


def workflow_matrix_arches(job: str) -> set[str]:
  arches = set(re.findall(
    r"^\s{12}arch:\s*([A-Za-z0-9_]+)\s*$", job, re.MULTILINE))
  for build in re.findall(
      r"^\s{12}build:\s*([A-Za-z0-9_-]+)\s*$", job, re.MULTILINE):
    architecture = build.split("-", 1)[0]
    if architecture in {"aarch64", "arm64", "x86_64"}:
      arches.add(architecture)
  return arches


def validate_race_native_workflow_texts(
    build_workflow: str, release_workflow: str,
    canonical_test_targets: set[str]) -> list[str]:
  """Validate native Race execution in every applicable workflow leg."""

  errors = []
  workflows = {
    "build workflow": build_workflow,
    "release workflow": release_workflow,
  }
  parsed = {label: workflow_run_steps(text)
            for label, text in workflows.items()}

  for label, steps in parsed.items():
    for step in steps:
      for reference in workflow_test_target_references(step.script):
        normalized = reference.lower().replace("-", "_")
        if normalized not in canonical_test_targets:
          errors.append(
            f"{label}: {step.job}/{step.name or '<unnamed>'} refers to "
            f"non-existent test target: {reference}")

  required_unix_targets = {
    "check_race",
    RACE_NATIVE_AUTOTOOLS_TARGET,
    "check_race_game_landing",
    "check_race_jump_viewer",
    "check_race_pmove",
    RACE_WEAPON_MOVEMENT_AUTOTOOLS_TARGET,
    RACE_WEAPON_TUNING_AUTOTOOLS_TARGET,
  }
  unix_requirements = (
    ("build workflow", "build-linux", "Run focused Race tests", True),
    ("release workflow", "build-macos", "Verify and test Race", False),
    ("release workflow", "build-linux", "Verify and test Race", False),
  )
  for label, job, name, require_markers in unix_requirements:
    matches = [step for step in parsed[label]
               if step.job == job and step.name == name]
    if len(matches) != 1:
      errors.append(f"{label}: expected one {job}/{name} step; found {len(matches)}")
      continue
    step = matches[0]
    references = {
      reference.lower().replace("-", "_")
      for reference in workflow_test_target_references(step.script)
    }
    for target in sorted(required_unix_targets - references):
      errors.append(f"{label}: {job}/{name} omits {target}")
    if require_markers:
      if RACE_WEAPON_MOVEMENT_MARKER not in step.script:
        errors.append(f"{label}: {job}/{name} does not assert seven weapon tests")
      if RACE_WEAPON_TUNING_MARKER not in step.script:
        errors.append(
          f"{label}: {job}/{name} does not assert the weapon tuning service marker")
    for marker in ("src/tests/race_portability_fixture", "--write", "--verify"):
      if marker not in step.script:
        errors.append(
          f"{label}: {job}/{name} portability contract missing: {marker}")
    if step.condition is not None:
      errors.append(f"{label}: {job}/{name} must run on every matrix leg")

  windows_requirements = (
    ("build workflow", "build-windows"),
    ("release workflow", "build-windows"),
  )
  for label, job in windows_requirements:
    matches = [step for step in parsed[label]
               if step.job == job and step.name == "Run native Race tests"]
    if len(matches) != 1:
      errors.append(
        f"{label}: expected one {job}/Run native Race tests step; "
        f"found {len(matches)}")
    else:
      step = matches[0]
      references = workflow_test_target_references(step.script)
      for target in (
          RACE_NATIVE_PROJECT_TARGET,
          RACE_WEAPON_MOVEMENT_PROJECT_TARGET,
          RACE_WEAPON_TUNING_PROJECT_TARGET,
      ):
        if target not in references:
          errors.append(f"{label}: {job} omits direct {target}.exe execution")
      for marker, description in (
          (RACE_NATIVE_MARKER, "the native integration marker"),
          (RACE_WEAPON_MOVEMENT_MARKER, "seven weapon tests"),
          (RACE_WEAPON_TUNING_MARKER, "the weapon tuning service marker"),
      ):
        if marker not in step.script:
          errors.append(f"{label}: {job} does not assert {description}")
      if "$LASTEXITCODE" not in step.script:
        errors.append(f"{label}: {job} does not check the native test exit code")
      if step.condition is not None:
        errors.append(f"{label}: {job} native tests are conditional")

    portability = [step for step in parsed[label]
                   if step.job == job
                   and step.name == "Verify portable persistence corpus"]
    if len(portability) != 1:
      errors.append(
        f"{label}: expected one {job}/Verify portable persistence corpus step; "
        f"found {len(portability)}")
      continue
    step = portability[0]
    for marker in ("race-portability-fixture.exe", "--write", "--verify",
                   "$LASTEXITCODE"):
      if marker not in step.script:
        errors.append(
          f"{label}: {job} portability contract missing: {marker}")
    if step.condition is not None:
      errors.append(f"{label}: {job} portability test is conditional")

  release_jobs = workflow_job_blocks(release_workflow)
  expected_arches = {
    "build-macos": {"arm64", "x86_64"},
    "build-linux": {"aarch64", "x86_64"},
  }
  for job, expected in expected_arches.items():
    block = release_jobs.get(job)
    if block is None:
      errors.append(f"release workflow: required native job missing: {job}")
      continue
    actual = workflow_matrix_arches(block)
    if not expected.issubset(actual):
      errors.append(
        f"release workflow: {job} native architecture legs missing; "
        f"expected {sorted(expected)}, found {sorted(actual)}")
  return errors


def validate_race_native_workflow_contract(root: Path) -> list[str]:
  makefile = (root / "src/tests/Makefile.am").read_text()
  targets = set(make_variable_tokens(makefile, "TESTS"))
  build_workflow = (root / ".github/workflows/build.yml").read_text()
  release_workflow = (root / ".github/workflows/release.yml").read_text()
  return validate_race_native_workflow_texts(
    build_workflow, release_workflow, targets)


def workflow_job_value(job: str, key: str) -> str | None:
  match = re.search(rf"^    {re.escape(key)}:\s*(.*?)\s*$", job, re.MULTILINE)
  return match.group(1) if match else None


def workflow_job_contents_permission(job: str) -> str | None:
  match = re.search(
    r"^    permissions:\s*$\n(?:^      .*\n)*?^      contents:\s*(\S+)\s*$",
    job, re.MULTILINE)
  return match.group(1) if match else None


def validate_release_publication_text(workflow: str) -> list[str]:
  errors = []
  jobs = workflow_job_blocks(workflow)
  if not jobs:
    return ["release workflow: jobs mapping missing or malformed"]

  jobs_index = workflow.find("\njobs:\n")
  workflow_header = workflow[:jobs_index] if jobs_index != -1 else workflow
  if not re.search(
      r"^permissions:\s*$\n^  contents:\s*read\s*$",
      workflow_header, re.MULTILINE):
    errors.append("release workflow: root contents permission must be read")

  write_jobs = sorted(
    name for name, job in jobs.items()
    if workflow_job_contents_permission(job) == "write")
  if write_jobs != ["release"]:
    errors.append(
      "release workflow: release must be the only contents: write job; "
      f"found {write_jobs or 'none'}")

  release = jobs.get("release")
  if release is None:
    errors.append("release workflow: release job missing")
  else:
    needs_value = workflow_job_value(release, "needs") or ""
    needs = {
      item.strip()
      for item in needs_value.strip("[]").split(",")
      if item.strip()
    }
    expected_needs = {"lipo-macos", "build-linux", "build-windows"}
    if needs != expected_needs:
      errors.append(
        "release workflow: release must need every packaged platform; "
        f"found {sorted(needs)}")
    if workflow_job_value(release, "environment") != "release":
      errors.append("release workflow: release job must use the release environment")
    if workflow_job_contents_permission(release) != "write":
      errors.append("release workflow: release job must have contents: write")

    required = (
      'tag_commit=$(git rev-parse --verify "refs/tags/$VERSION^{commit}")',
      'source_commit=$(git rev-parse "$GITHUB_SHA^{commit}")',
      'test "$tag_commit" = "$source_commit"',
      'if gh release view "$VERSION" >/dev/null 2>&1; then',
      'gh release create "$VERSION" --verify-tag',
      "aws s3 cp version s3://quetoo/version",
    )
    for marker in required:
      if marker not in release:
        errors.append(
          f"release workflow: release publication contract missing: {marker}")

    if not re.search(
        r'if gh release view "\$VERSION" >/dev/null 2>&1; then\s+'
        r'(?:echo [^\n]*\n\s+)?exit 1\s+fi', release):
      errors.append("release workflow: release job must refuse an existing release")
    for mutable_command in ("gh release edit", "--clobber"):
      if mutable_command in release:
        errors.append(
          "release workflow: release job must not mutate an existing "
          f"release: {mutable_command}")

    ref_gate = release.find("tag_commit=$(git rev-parse")
    secret_positions = [
      position for position in (
        release.find("GH_TOKEN:"),
        release.find("AWS_SECRET_ACCESS_KEY:"),
      ) if position != -1
    ]
    if ref_gate == -1 or any(ref_gate > position for position in secret_positions):
      errors.append(
        "release workflow: release ref must be verified before secret exposure")
    github_publish = release.find('gh release create "$VERSION" --verify-tag')
    s3_publish = release.find("aws s3 cp version s3://quetoo/version")
    if (github_publish == -1 or s3_publish == -1
        or github_publish > s3_publish):
      errors.append(
        "release workflow: S3 version marker must follow GitHub publication")

  publish_itch = jobs.get("publish-itch")
  if publish_itch is None:
    errors.append("release workflow: publish-itch job missing")
  else:
    if workflow_job_value(publish_itch, "needs") != "release":
      errors.append("release workflow: publish-itch must need release")
    if workflow_job_value(publish_itch, "environment") != "release":
      errors.append(
        "release workflow: publish-itch must use the release environment")
    if workflow_job_contents_permission(publish_itch) == "write":
      errors.append(
        "release workflow: publish-itch must not write repository contents")
  return errors


def validate_release_publication_contract(root: Path) -> list[str]:
  workflow = (root / ".github/workflows/release.yml").read_text()
  return validate_release_publication_text(workflow)


def validate_workflow_action_pins_text(workflow: str,
                                       label: str) -> list[str]:
  """Reject mutable action tags and branches in a workflow."""
  errors = []
  references = re.findall(r"^\s*uses:\s*([^\s#]+)", workflow, re.MULTILINE)
  if not references:
    return [f"{label}: no action references found"]
  for reference in references:
    if reference.startswith("./"):
      continue
    if "@" not in reference:
      errors.append(f"{label}: action has no revision: {reference}")
      continue
    revision = reference.rsplit("@", 1)[1]
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
      errors.append(f"{label}: action is not pinned to a full commit: {reference}")
  return errors


def validate_weapon_movement_test_graph(root: Path) -> list[str]:
  errors = []
  makefile = (root / "src/tests/Makefile.am").read_text()
  project_path = root / "Quetoo.vs15/check-race-weapon-movement.vcxproj"
  xcode = (root / "Quetoo.xcodeproj/project.pbxproj").read_text()
  scheme = root / (
    "Quetoo.xcodeproj/xcshareddata/xcschemes/"
    "check-race-weapon-movement.xcscheme")
  test_targets = set(make_variable_tokens(makefile, "TESTS"))

  if RACE_WEAPON_MOVEMENT_AUTOTOOLS_TARGET not in test_targets:
    errors.append("src/tests/Makefile.am: weapon movement test missing from TESTS")
  if "check_race_weapon_movement_SOURCES" not in makefile:
    errors.append("src/tests/Makefile.am: weapon movement test target missing")
  if not project_path.is_file():
    errors.append("Quetoo.vs15: weapon movement test project missing")
  else:
    project = project_path.read_text()
    if f"<TargetName>{RACE_WEAPON_MOVEMENT_PROJECT_TARGET}</TargetName>" not in project:
      errors.append("Quetoo.vs15: weapon movement test target name differs")
  if (f'name = "{RACE_WEAPON_MOVEMENT_PROJECT_TARGET}";' not in xcode
      or not scheme.is_file()):
    errors.append("Quetoo.xcodeproj: weapon movement test target or scheme missing")
  elif (f'BuildableName="{RACE_WEAPON_MOVEMENT_PROJECT_TARGET}"'
        not in scheme.read_text()
        or f'BlueprintName="{RACE_WEAPON_MOVEMENT_PROJECT_TARGET}"'
        not in scheme.read_text()):
    errors.append("Quetoo.xcodeproj: weapon movement scheme target name differs")
  for solution in ("Quetoo.vs15/quetoo.sln", "Quetoo.vs15/quetoo_all.sln"):
    if "check-race-weapon-movement.vcxproj" not in (root / solution).read_text():
      errors.append(f"{solution}: weapon movement test project missing")
  sources = (
    "src/tests/check_race_weapon_movement.c",
    "src/game/race/race_physics.c",
    "src/game/race/race_weapon_movement.c",
    "src/shared/qstring.c",
  )
  project = project_path.read_text() if project_path.is_file() else ""
  for relative in sources:
    basename = Path(relative).name
    make_path = basename if relative.startswith("src/tests/") \
      else f"$(top_srcdir)/{relative}"
    project_source = "..\\" + relative.replace("/", "\\")
    if make_path not in makefile:
      errors.append(f"src/tests/Makefile.am: weapon test source missing: {relative}")
    if project_source not in project:
      errors.append(f"Quetoo.vs15: weapon test source missing: {relative}")
    if basename not in xcode:
      errors.append(f"Quetoo.xcodeproj: weapon test source missing: {relative}")
  return errors


def validate_weapon_tuning_test_graph(root: Path) -> list[str]:
  errors = []
  makefile = (root / "src/tests/Makefile.am").read_text()
  project_path = root / "Quetoo.vs15/check-race-weapon-tuning.vcxproj"
  xcode = (root / "Quetoo.xcodeproj/project.pbxproj").read_text()
  scheme = root / (
    "Quetoo.xcodeproj/xcshareddata/xcschemes/"
    "check-race-weapon-tuning.xcscheme")
  test_targets = set(make_variable_tokens(makefile, "TESTS"))

  if RACE_WEAPON_TUNING_AUTOTOOLS_TARGET not in test_targets:
    errors.append("src/tests/Makefile.am: weapon tuning test missing from TESTS")
  if "check_race_weapon_tuning_SOURCES" not in makefile:
    errors.append("src/tests/Makefile.am: weapon tuning test target missing")
  if not project_path.is_file():
    errors.append("Quetoo.vs15: weapon tuning test project missing")
  else:
    project = project_path.read_text()
    if f"<TargetName>{RACE_WEAPON_TUNING_PROJECT_TARGET}</TargetName>" not in project:
      errors.append("Quetoo.vs15: weapon tuning test target name differs")
  if (f'name = "{RACE_WEAPON_TUNING_PROJECT_TARGET}";' not in xcode
      or not scheme.is_file()):
    errors.append("Quetoo.xcodeproj: weapon tuning test target or scheme missing")
  elif (f'BuildableName="{RACE_WEAPON_TUNING_PROJECT_TARGET}"'
        not in scheme.read_text()
        or f'BlueprintName="{RACE_WEAPON_TUNING_PROJECT_TARGET}"'
        not in scheme.read_text()):
    errors.append("Quetoo.xcodeproj: weapon tuning scheme target name differs")
  for solution in ("Quetoo.vs15/quetoo.sln", "Quetoo.vs15/quetoo_all.sln"):
    if "check-race-weapon-tuning.vcxproj" not in (root / solution).read_text():
      errors.append(f"{solution}: weapon tuning test project missing")
  sources = (
    "src/tests/check_race_weapon_tuning.c",
    "src/tests/check_race_weapon_tuning_service.c",
    "src/cgame/race/cg_race_weapon_tuning.c",
    "src/game/race/race_weapon_tuning.c",
    "src/game/race/race_weapon_tuning_service.c",
    "src/game/race/race_weapon_tuning_wire.c",
    "src/shared/qstring.c",
  )
  project = project_path.read_text() if project_path.is_file() else ""
  for relative in sources:
    basename = Path(relative).name
    make_path = basename if relative.startswith("src/tests/") \
      else f"$(top_srcdir)/{relative}"
    project_source = "..\\" + relative.replace("/", "\\")
    if make_path not in makefile:
      errors.append(
        f"src/tests/Makefile.am: weapon tuning source missing: {relative}")
    if project_source not in project:
      errors.append(f"Quetoo.vs15: weapon tuning source missing: {relative}")
    if basename not in xcode:
      errors.append(f"Quetoo.xcodeproj: weapon tuning source missing: {relative}")
  return errors


def validate_workflow_action_pins(root: Path) -> list[str]:
  errors = []
  for name in ("build.yml", "release.yml"):
    workflow = (root / ".github" / "workflows" / name).read_text()
    errors.extend(validate_workflow_action_pins_text(workflow, name))
  return errors


def validate_butler_install_text(workflow: str) -> list[str]:
  errors = []
  if "broth.itch.zone/butler/linux-amd64/LATEST/" in workflow:
    errors.append("release workflow: Butler download uses mutable LATEST")
  required = (
    "broth.itch.zone/butler/linux-amd64/15.21.0/archive/default",
    "BUTLER_LINUX_AMD64_15_21_0_SHA256",
    "sha256sum --check",
    "./butler -V | grep -F '15.21.0'",
    "environment: release",
  )
  for marker in required:
    if marker not in workflow:
      errors.append(f"release workflow: pinned Butler contract missing: {marker}")
  digest_gate = workflow.find("sha256sum --check")
  api_key = workflow.find("BUTLER_API_KEY")
  if digest_gate == -1 or api_key == -1 or digest_gate > api_key:
    errors.append("release workflow: Butler must be verified before API key exposure")
  return errors


def validate_butler_install(root: Path) -> list[str]:
  workflow = (root / ".github/workflows/release.yml").read_text()
  return validate_butler_install_text(workflow)


def validate_windows_release_security(root: Path) -> list[str]:
  errors = []
  retired_files = (
    "Quetoo.vs15/Package-QuetooRace.ps1",
    "Quetoo.vs15/Package-QuetooRace.cmd",
    "Quetoo.vs15/Install-QuetooRace.ps1",
    "Quetoo.vs15/Install-QuetooRace.cmd",
    "Quetoo.vs15/Sign-QuetooRace.ps1",
    "src/tools/write_race_build_evidence.py",
    "src/tools/test_write_race_build_evidence.py",
  )
  for relative in retired_files:
    if (root / relative).exists():
      errors.append(f"retired standalone release file remains: {relative}")

  required_markers = (
    "QUETOO_HOME:",
    "race-stage",
    r"Quetoo.vs15\bin\x64Release\race\cgame.dll",
    r"Quetoo.vs15\bin\x64Release\race\game.dll",
    r"src\cgame\race\ui",
    r"share\race\manifest.mf",
    r"share\race\maps\*.bsp",
    "verify_race_payload.py --root windows/target/quetoo --layout windows",
    "race-portability-fixture.exe",
  )
  forbidden_markers = (
    "Package-QuetooRace",
    "Install-QuetooRace",
    "Sign-QuetooRace",
    "write_race_build_evidence.py",
    "-AllowUnsignedForDevelopment",
    "$ExpectedClientHash",
    "$ExpectedDedicatedHash",
    "$ExpectedGameHash",
    "$ExpectedCgameHash",
  )
  for name in ("build.yml", "release.yml"):
    path = root / ".github" / "workflows" / name
    if not path.is_file():
      errors.append(f"{name}: workflow missing")
      continue
    workflow = path.read_text()
    for marker in required_markers:
      if marker not in workflow:
        errors.append(f"{name}: current Windows Race contract missing: {marker}")
    for marker in forbidden_markers:
      if marker in workflow:
        errors.append(f"{name}: retired Windows Race contract remains: {marker}")
  return errors


def audit(root: Path, markdown: bool) -> int:
  errors = []
  manifest = override_manifest()
  expected = {(entry.stock, entry.race) for entry in manifest}
  discovered = discover_overrides(root)

  for pair in sorted(expected - discovered):
    errors.append(f"missing override: {pair[0]} -> {pair[1]}")
  for pair in sorted(discovered - expected):
    errors.append(f"unledgered override: {pair[0]} -> {pair[1]}")

  rows = []
  for entry in manifest:
    stock_path = root / entry.stock
    race_path = root / entry.race
    if not stock_path.is_file() or not race_path.is_file():
      errors.append(f"missing file: {entry.stock} or {entry.race}")
      continue
    baseline = git_blob(root, entry.stock)
    stock = canonical_bytes(entry.stock, stock_path.read_bytes())
    race = canonical_bytes(entry.race, race_path.read_bytes())
    if stock != baseline:
      errors.append(f"stock baseline drift: {entry.stock}")
    if is_binary(entry.race):
      # A replaced image has no meaningful line delta; the hashes carry the
      # whole of the difference.
      added, deleted = 0, 0
    else:
      added, deleted = line_delta(baseline, race)
    rows.append((entry, sha256(baseline), sha256(race), added, deleted))

  for relative in COMPATIBILITY_FILES:
    if not (root / relative).is_file():
      errors.append(f"missing compatibility file: {relative}")

  errors.extend(validate_stock_tree_integrity(root))
  errors.extend(production_portability_errors(root))
  errors.extend(validate_api_constants(root))
  errors.extend(validate_current_cgame_contract(root))
  errors.extend(validate_portability_fixture_graph(root))
  errors.extend(validate_race_native_test_graph(root))
  errors.extend(validate_weapon_movement_test_graph(root))
  errors.extend(validate_weapon_tuning_test_graph(root))
  errors.extend(validate_ai_override_delta(root))
  errors.extend(validate_xcode_runtime_contract(root))
  errors.extend(validate_linux_runtime_contract(root))
  errors.extend(validate_windows_runtime_contract(root))
  errors.extend(validate_runtime_portability_contract(root))
  errors.extend(validate_release_publication_contract(root))
  errors.extend(validate_race_native_workflow_contract(root))
  errors.extend(validate_workflow_action_pins(root))
  errors.extend(validate_butler_install(root))
  errors.extend(validate_windows_release_security(root))

  parity = subprocess.run(
    [sys.executable, str(root / "src" / "tools" / "verify_projects.py")],
    cwd=root,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
  )
  if parity.returncode:
    errors.append("project parity failed:\n" + parity.stdout.rstrip())

  if markdown:
    print("| Group | Current stock source | Race source | Stock SHA-256 | Race SHA-256 | Delta |")
    print("|---|---|---|---|---|---:|")
    for entry, stock_hash, race_hash, added, deleted in rows:
      print(f"| {entry.group} | `{entry.stock}` | `{entry.race}` | "
            f"`{stock_hash}` | `{race_hash}` | +{added}/-{deleted} |")
    print()
    print("Compatibility additions:")
    for relative in COMPATIBILITY_FILES:
      path = root / relative
      digest = sha256(path.read_bytes()) if path.is_file() else "missing"
      print(f"- `{relative}` `{digest}`")

  if errors:
    for error in errors:
      print(f"ERROR: {error}", file=sys.stderr)
    return 1

  print(
    "RACE_OVERRIDE_AUDIT_PASS "
    f"baseline={BASELINE_COMMIT} overrides={len(manifest)} "
    f"compatibility={len(COMPATIBILITY_FILES)} project_parity=1 "
    f"fixture_parity=1 portability=1 stock_ref={STOCK_REFERENCE_COMMIT} "
    f"stock_deviations={len(STOCK_DEVIATIONS)} "
    f"vendored={len(VENDORED_ADDITIONS)}")
  return 0


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--root", type=Path,
                      default=Path(__file__).resolve().parents[2])
  parser.add_argument("--markdown", action="store_true")
  args = parser.parse_args()
  return audit(args.root.resolve(), args.markdown)


if __name__ == "__main__":
  raise SystemExit(main())
