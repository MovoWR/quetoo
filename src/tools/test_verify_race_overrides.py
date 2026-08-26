"""Regression tests for the release boundary and stock-tree integrity."""

import hashlib
import subprocess
import tempfile
import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from verify_race_overrides import (
  RACE_NATIVE_MARKER,
  RACE_WEAPON_TUNING_MARKER,
  validate_api_constants,
  validate_butler_install_text,
  validate_current_cgame_contract,
  validate_race_native_workflow_texts,
  validate_release_publication_text,
  validate_windows_release_security,
  validate_windows_runtime_contract,
  validate_workflow_action_pins_text,
  validate_stock_tree_integrity,
  stock_owned,
)


CANONICAL_RACE_TESTS = {
  "check_race",
  "check_race_native",
  "check_race_game_landing",
  "check_race_jump_viewer",
  "check_race_pmove",
  "check_race_weapon_movement",
  "check_race_weapon_tuning",
}


def race_test_list(native: str | None = "check_race_native",
                   extra: str = "") -> str:
  targets = ["check_race"]
  if native:
    targets.append(native)
  targets.extend(("check_race_game_landing", "check_race_jump_viewer",
                  "check_race_pmove", "check_race_weapon_movement",
                  "check_race_weapon_tuning"))
  if extra:
    targets.append(extra)
  return " ".join(targets)


def native_windows_steps(target: str = "check-race-native",
                         portability: bool = True) -> str:
  portability_step = """
    - name: Verify portable persistence corpus
      shell: pwsh
      run: |
        & '.\\Quetoo.vs15\\bin\\x64Release\\race-portability-fixture.exe' --write $output
        if ($LASTEXITCODE -ne 0) { throw 'failed' }
        & '.\\Quetoo.vs15\\bin\\x64Release\\race-portability-fixture.exe' --verify $output
        if ($LASTEXITCODE -ne 0) { throw 'failed' }
""" if portability else ""
  return f"""    - name: Run native Race tests
      shell: pwsh
      run: |
        $nativeOutput = & '.\\Quetoo.vs15\\bin\\x64Release\\{target}.exe' 2>&1
        $nativeExit = $LASTEXITCODE
        if ($nativeExit -ne 0) {{ throw 'failed' }}
        if (-not ($nativeOutput -match '{RACE_NATIVE_MARKER}')) {{ throw 'missing' }}
        $weaponOutput = & '.\\Quetoo.vs15\\bin\\x64Release\\check-race-weapon-movement.exe' 2>&1
        $weaponExit = $LASTEXITCODE
        if ($weaponExit -ne 0) {{ throw 'failed' }}
        if (-not ($weaponOutput -match 'Checks: 7, Failures: 0, Errors: 0')) {{ throw 'missing' }}
        $tuningOutput = & '.\\Quetoo.vs15\\bin\\x64Release\\check-race-weapon-tuning.exe' 2>&1
        $tuningExit = $LASTEXITCODE
        if ($tuningExit -ne 0) {{ throw 'failed' }}
        if (-not ($tuningOutput -match '{RACE_WEAPON_TUNING_MARKER}')) {{ throw 'missing' }}
{portability_step}"""


def race_build_workflow(*, linux_native: str | None = "check_race_native",
                         windows_target: str = "check-race-native",
                         portability: bool = True) -> str:
  linux_command = (f"CK_FORK=no src/tests/{linux_native}"
                   if linux_native else
                   "# check_race_native\n        echo check_race_native")
  portability_commands = """
        src/tests/race_portability_fixture --write build/race-portability-x86_64
        src/tests/race_portability_fixture --verify build/race-portability-x86_64""" \
    if portability else ""
  return f"""name: Build
jobs:
  build-linux:
    runs-on: ubuntu-22.04
    steps:
    - name: Run focused Race tests
      run: |
        CK_FORK=no src/tests/check_race
        {linux_command}
        CK_FORK=no src/tests/check_race_game_landing
        CK_FORK=no src/tests/check_race_jump_viewer
        CK_FORK=no src/tests/check_race_pmove
        weapon_output=$(CK_FORK=no src/tests/check_race_weapon_movement 2>&1)
        grep -Fq 'Checks: 7, Failures: 0, Errors: 0' <<< "$weapon_output"
        tuning_output=$(src/tests/check_race_weapon_tuning 2>&1)
        grep -Fq '{RACE_WEAPON_TUNING_MARKER}' <<< "$tuning_output"
{portability_commands}
  build-windows:
    runs-on: windows-latest
    steps:
{native_windows_steps(windows_target)}"""


def race_release_workflow(
    *, mac_native: str | None = "check_race_native",
    linux_native: str | None = "check_race_native",
    linux_extra: str = "", mac_condition: str = "",
    linux_second_build: str = "aarch64-pc-linux",
    windows_target: str = "check-race-native",
    portability: bool = True) -> str:
  condition = f"      if: {mac_condition}\n" if mac_condition else ""
  mac_portability = (
    "\n        src/tests/race_portability_fixture --write build/mac\n"
    "        src/tests/race_portability_fixture --verify build/mac"
    if portability else "")
  linux_portability = (
    "\n        src/tests/race_portability_fixture --write build/linux\n"
    "        src/tests/race_portability_fixture --verify build/linux"
    if portability else "")
  return f"""name: Release
jobs:
  build-macos:
    strategy:
      matrix:
        include:
          - runner: macos-15
            arch: arm64
            build: arm64-apple-darwin
          - runner: macos-15-intel
            arch: x86_64
            build: x86_64-apple-darwin
    steps:
    - name: Verify and test Race
{condition}      run: |
        make check TESTS='{race_test_list(mac_native)}'{mac_portability}
  build-linux:
    strategy:
      matrix:
        include:
          - runner: ubuntu-22.04
            build: x86_64-pc-linux
          - runner: ubuntu-22.04-arm
            build: {linux_second_build}
    steps:
    - name: Verify and test Race
      run: |
        make check TESTS='{race_test_list(linux_native, linux_extra)}'{linux_portability}
  build-windows:
    runs-on: windows-latest
    steps:
{native_windows_steps(windows_target, portability)}"""


def publication_workflow(*, root_permission: str = "read",
                         release_permission: str = "write",
                         release_environment: str = "release",
                         itch_needs: str = "release",
                         s3_before_github: bool = False,
                         extra_job: str = "") -> str:
  publish_commands = (
    '        aws s3 cp version s3://quetoo/version\n'
    '        gh release create "$VERSION" --verify-tag'
    if s3_before_github else
    '        gh release create "$VERSION" --verify-tag\n'
    '        aws s3 cp version s3://quetoo/version'
  )
  return f"""name: Release

on:
  push:
    tags: [ "v*" ]
  workflow_dispatch:
    inputs:
      version:
        required: true

permissions:
  contents: {root_permission}

jobs:
  build-macos:
    runs-on: macos-15
    steps:
    - run: true

  lipo-macos:
    needs: build-macos
    runs-on: macos-15
    steps:
    - run: true

  build-linux:
    runs-on: ubuntu-latest
    steps:
    - run: true

  build-windows:
    runs-on: windows-latest
    steps:
    - run: true

  release:
    needs: [lipo-macos, build-linux, build-windows]
    runs-on: ubuntu-latest
    environment: {release_environment}
    permissions:
      contents: {release_permission}
    steps:
    - name: Verify release ref
      run: |
        tag_commit=$(git rev-parse --verify "refs/tags/$VERSION^{{commit}}")
        source_commit=$(git rev-parse "$GITHUB_SHA^{{commit}}")
        test "$tag_commit" = "$source_commit"
    - name: Publish
      env:
        GH_TOKEN: token
        AWS_SECRET_ACCESS_KEY: secret
      run: |
        if gh release view "$VERSION" >/dev/null 2>&1; then
          exit 1
        fi
{publish_commands}

  publish-itch:
    needs: {itch_needs}
    runs-on: ubuntu-latest
    environment: release
    permissions:
      contents: read
    steps:
    - run: true
{extra_job}"""


class RaceNativeWorkflowContractTest(unittest.TestCase):

  def validate(self, build: str | None = None,
               release: str | None = None) -> list[str]:
    return validate_race_native_workflow_texts(
      build or race_build_workflow(),
      release or race_release_workflow(),
      CANONICAL_RACE_TESTS,
    )

  def test_complete_native_workflow_contract_passes(self) -> None:
    self.assertEqual(self.validate(), [])

  def test_comment_and_echo_do_not_satisfy_execution(self) -> None:
    errors = self.validate(build=race_build_workflow(linux_native=None))
    self.assertTrue(any("build-linux/Run focused Race tests omits" in error
                        for error in errors))

  def test_release_test_list_omission_is_rejected(self) -> None:
    errors = self.validate(release=race_release_workflow(linux_native=None))
    self.assertTrue(any("build-linux/Verify and test Race omits" in error
                        for error in errors))

  def test_condition_that_omits_a_native_matrix_leg_is_rejected(self) -> None:
    errors = self.validate(release=race_release_workflow(
      mac_condition="matrix.arch == 'arm64'"))
    self.assertTrue(any("must run on every matrix leg" in error
                        for error in errors))

  def test_nonexistent_test_target_is_rejected(self) -> None:
    errors = self.validate(release=race_release_workflow(
      linux_extra="check_race_missing"))
    self.assertTrue(any("non-existent test target: check_race_missing" in error
                        for error in errors))

  def test_missing_portability_corpus_is_rejected(self) -> None:
    errors = self.validate(release=race_release_workflow(portability=False))
    self.assertTrue(any("portability contract missing" in error
                        for error in errors))

  def test_missing_architecture_leg_is_rejected(self) -> None:
    errors = self.validate(release=race_release_workflow(
      linux_second_build="x86_64-pc-linux"))
    self.assertTrue(any("build-linux native architecture legs missing" in error
                        for error in errors))

  def test_windows_target_spelling_must_match_project(self) -> None:
    errors = self.validate(build=race_build_workflow(
      windows_target="check_race_native"))
    self.assertTrue(any("omits direct check-race-native.exe" in error
                        for error in errors))


class ReleasePublicationContractTest(unittest.TestCase):

  def test_current_publication_contract_passes(self) -> None:
    self.assertEqual(validate_release_publication_text(
      publication_workflow()), [])

  def test_root_write_permission_is_rejected(self) -> None:
    errors = validate_release_publication_text(
      publication_workflow(root_permission="write"))
    self.assertTrue(any("root contents permission must be read" in error
                        for error in errors))

  def test_second_write_job_is_rejected(self) -> None:
    errors = validate_release_publication_text(publication_workflow(
      extra_job="""

  unsafe:
    runs-on: ubuntu-latest
    permissions:
      contents: write
    steps:
    - run: true
"""))
    self.assertTrue(any("only contents: write job" in error
                        for error in errors))

  def test_missing_release_write_permission_is_rejected(self) -> None:
    errors = validate_release_publication_text(
      publication_workflow(release_permission="read"))
    self.assertTrue(any("only contents: write job" in error
                        for error in errors))

  def test_release_tag_must_resolve_to_workflow_sha(self) -> None:
    unsafe = publication_workflow().replace(
      'source_commit=$(git rev-parse "$GITHUB_SHA^{commit}")',
      'source_commit=$(git rev-parse "HEAD^{commit}")')
    errors = validate_release_publication_text(unsafe)
    self.assertTrue(any("publication contract missing" in error
                        and "GITHUB_SHA" in error for error in errors))

  def test_existing_release_mutation_is_rejected(self) -> None:
    unsafe = publication_workflow().replace(
      'if gh release view "$VERSION" >/dev/null 2>&1; then\n'
      '          exit 1\n'
      '        fi',
      'if gh release view "$VERSION" >/dev/null 2>&1; then\n'
      '          gh release edit "$VERSION" --clobber\n'
      '        fi')
    errors = validate_release_publication_text(unsafe)
    self.assertTrue(any("must refuse an existing release" in error
                        for error in errors))
    self.assertTrue(any("must not mutate" in error for error in errors))

  def test_s3_marker_requires_successful_github_publication(self) -> None:
    errors = validate_release_publication_text(
      publication_workflow(s3_before_github=True))
    self.assertTrue(any("S3 version marker must follow" in error
                        for error in errors))

  def test_itch_requires_successful_github_publication(self) -> None:
    errors = validate_release_publication_text(
      publication_workflow(itch_needs="build-linux"))
    self.assertTrue(any("publish-itch must need release" in error
                        for error in errors))

  def test_release_environment_is_required(self) -> None:
    errors = validate_release_publication_text(
      publication_workflow(release_environment="preview"))
    self.assertTrue(any("release job must use the release environment" in error
                        for error in errors))


class WorkflowActionPinTest(unittest.TestCase):

  def test_full_commit_pin_passes(self) -> None:
    workflow_text = "steps:\n  uses: actions/checkout@" + "1" * 40 + "\n"
    self.assertEqual(
      validate_workflow_action_pins_text(workflow_text, "fixture"), [])

  def test_mutable_tag_fails(self) -> None:
    errors = validate_workflow_action_pins_text(
      "steps:\n  uses: actions/checkout@v6\n", "fixture")
    self.assertEqual(
      errors,
      ["fixture: action is not pinned to a full commit: actions/checkout@v6"])


class ButlerInstallTest(unittest.TestCase):

  PINNED = """environment: release
https://broth.itch.zone/butler/linux-amd64/15.21.0/archive/default
BUTLER_LINUX_AMD64_15_21_0_SHA256
sha256sum --check
./butler -V | grep -F '15.21.0'
BUTLER_API_KEY
"""

  def test_pinned_verified_butler_passes(self) -> None:
    self.assertEqual(validate_butler_install_text(self.PINNED), [])

  def test_latest_and_late_verification_fail(self) -> None:
    workflow_text = self.PINNED.replace(
      "15.21.0/archive/default", "LATEST/archive/default")
    workflow_text = workflow_text.replace(
      "sha256sum --check\n", "").replace(
        "BUTLER_API_KEY\n", "BUTLER_API_KEY\nsha256sum --check\n")
    errors = validate_butler_install_text(workflow_text)
    self.assertTrue(any("mutable LATEST" in error for error in errors))
    self.assertTrue(any("before API key" in error for error in errors))


class WindowsReleaseContractTest(unittest.TestCase):

  CURRENT = r"""QUETOO_HOME: race-stage
Quetoo.vs15\bin\x64Release\race\cgame.dll
Quetoo.vs15\bin\x64Release\race\game.dll
src\cgame\race\ui
share\race\manifest.mf
share\race\maps\*.bsp
verify_race_payload.py --root windows/target/quetoo --layout windows
race-portability-fixture.exe
"""

  def setUp(self) -> None:
    self.directory = tempfile.TemporaryDirectory()
    self.addCleanup(self.directory.cleanup)
    self.root = Path(self.directory.name)
    workflows = self.root / ".github/workflows"
    workflows.mkdir(parents=True)
    for name in ("build.yml", "release.yml"):
      (workflows / name).write_text(self.CURRENT, encoding="utf-8")

  def test_current_windows_contract_passes(self) -> None:
    self.assertEqual(validate_windows_release_security(self.root), [])

  def test_retired_standalone_script_is_rejected(self) -> None:
    path = self.root / "Quetoo.vs15/Sign-QuetooRace.ps1"
    path.parent.mkdir(parents=True)
    path.write_text("retired", encoding="utf-8")
    errors = validate_windows_release_security(self.root)
    self.assertTrue(any("retired standalone release file remains" in error
                        for error in errors))

  def test_missing_installed_payload_gate_is_rejected(self) -> None:
    workflow = self.root / ".github/workflows/release.yml"
    workflow.write_text(
      self.CURRENT.replace(
        "verify_race_payload.py --root windows/target/quetoo --layout windows",
        "python -m unittest"),
      encoding="utf-8")
    errors = validate_windows_release_security(self.root)
    self.assertTrue(any("current Windows Race contract missing" in error
                        for error in errors))


class WindowsRuntimeContractTest(unittest.TestCase):

  SMOKE = r"""$game = Join-Path $QuetooRoot 'lib\race\game.dll'
$cgame = Join-Path $QuetooRoot 'lib\race\cgame.dll'
$dedicatedStdin = Join-Path $OutputRoot 'dedicated.stdin'
-RedirectStandardInput $dedicatedStdin
pid=$($Process.Id) exit=$($Process.ExitCode)
$clientHash = (Get-FileHash -LiteralPath $client
"client_sha256=$clientHash"
"dedicated_sha256=$dedicatedHash"
+game', 'race
"""

  WORKFLOW = r"""QUETOO_HOME:
\race-stage
Copy-Item "Quetoo.vs15\bin\x64Release\race\cgame.dll"
Copy-Item "Quetoo.vs15\bin\x64Release\race\game.dll"
$raceUiSource = "src\cgame\race\ui"
Copy-Item "share\race\manifest.mf","share\race\maps.lst"
--root windows/target/quetoo --layout windows
"""

  def setUp(self) -> None:
    self.directory = tempfile.TemporaryDirectory()
    self.addCleanup(self.directory.cleanup)
    self.root = Path(self.directory.name)
    smoke = self.root / "Quetoo.vs15/Smoke-QuetooRace.ps1"
    smoke.parent.mkdir(parents=True)
    smoke.write_text(self.SMOKE, encoding="utf-8")
    workflow = self.root / ".github/workflows/release.yml"
    workflow.parent.mkdir(parents=True)
    workflow.write_text(self.WORKFLOW, encoding="utf-8")

  def test_current_non_tty_runtime_contract_passes(self) -> None:
    self.assertEqual(validate_windows_runtime_contract(self.root), [])

  def test_inherited_stdin_is_rejected(self) -> None:
    smoke = self.root / "Quetoo.vs15/Smoke-QuetooRace.ps1"
    smoke.write_text(
      self.SMOKE.replace("-RedirectStandardInput $dedicatedStdin",
                         "# inherited stdin"),
      encoding="utf-8")
    errors = validate_windows_runtime_contract(self.root)
    self.assertTrue(any("RedirectStandardInput" in error for error in errors))


class ApiContractTest(unittest.TestCase):

  CURRENT = """#include <Objectively/PointerArray.h>
#include <Objectively/RESTClient.h>
#define CGAME_API_VERSION 35
PointerArray *(*Servers)(void);
"""

  def setUp(self) -> None:
    self.directory = tempfile.TemporaryDirectory()
    self.addCleanup(self.directory.cleanup)
    self.root = Path(self.directory.name)
    self.write("src/game/game.h", "#define GAME_API_VERSION 33\n")
    self.write("src/cgame/cgame.h", self.CURRENT)
    self.write("src/common/common.h", "#define PROTOCOL_MAJOR 2029\n")
    self.write("src/cgame/race/ui/play/JoinServerViewController.h",
               "PointerArray *servers;\n")
    self.write("src/cgame/race/ui/play/JoinServerViewController.c",
               "const PointerArray *servers = cgi.Servers();\n")
    for relative in (
      "src/cgame/race/Makefile.am",
      "Quetoo.vs15/cgame-race.vcxproj",
      "Quetoo.vs15/cgame-race.vcxproj.filters",
      "Quetoo.xcodeproj/project.pbxproj",
    ):
      self.write(relative, "current host registration\n")

  def write(self, relative: str, text: str) -> None:
    path = self.root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")

  def test_current_api_contract_passes(self) -> None:
    self.assertEqual(validate_api_constants(self.root), [])
    self.assertEqual(validate_current_cgame_contract(self.root), [])

  def test_wrong_host_api_is_rejected(self) -> None:
    self.write("src/cgame/cgame.h", self.CURRENT.replace(
      "CGAME_API_VERSION 35", "CGAME_API_VERSION 34"))
    self.assertTrue(any("expected CGAME API 35" in error
                        for error in validate_api_constants(self.root)))

  def test_retired_list_consumer_is_rejected(self) -> None:
    self.write("src/cgame/race/ui/play/JoinServerViewController.h",
               "List *servers;\n")
    errors = validate_current_cgame_contract(self.root)
    self.assertTrue(any("PointerArray" in error for error in errors))
    self.assertTrue(any("retired List contract" in error for error in errors))

  def test_retired_adapter_registration_is_rejected(self) -> None:
    self.write("src/cgame/race/api34/cgame/cgame.h", "retired\n")
    self.write("src/cgame/race/Makefile.am", "-I$(srcdir)/api34\n")
    errors = validate_current_cgame_contract(self.root)
    self.assertTrue(any("compatibility directory" in error for error in errors))
    self.assertTrue(any("build registration" in error for error in errors))

class StockTreeIntegrityTest(unittest.TestCase):
  """The whole-tree guard against editing stock files.

  The ledgered override audit only inspects the declared override pairs, so
  these cases cover the gap it leaves: an edit anywhere else in the stock
  tree, a re-edit of an accepted deviation, an upstream move underneath a
  deviation, and an undeclared file dropped into stock or vendored source.
  """

  STOCK = "// stock"
  RACE = "// race"

  def setUp(self) -> None:
    self.directory = tempfile.TemporaryDirectory()
    self.addCleanup(self.directory.cleanup)
    self.root = Path(self.directory.name)

    self.git("init", "--quiet", "--initial-branch=main")
    self.git("config", "user.email", "test@example.invalid")
    self.git("config", "user.name", "Test")
    self.git("config", "commit.gpgsign", "false")

    self.write("src/game/common/g_local.h", self.STOCK)
    self.write("src/game/lithium/Makefile.am", self.STOCK)
    self.write("src/client/ui/ui_types.h", self.STOCK)
    self.write("src/game/race/race.c", self.RACE)
    self.write("deps/Makefile.am",
               "EXTRA_DIST = " + chr(92) + chr(10) + "\targon2/src/ref.c")
    self.git("add", "-A")
    self.git("commit", "--quiet", "-m", "stock")
    self.reference = self.git("rev-parse", "HEAD").strip()

  def git(self, *args: str) -> str:
    return subprocess.run(["git", *args], cwd=self.root, check=True,
                          stdout=subprocess.PIPE, text=True).stdout

  def write(self, relative: str, text: str) -> None:
    path = self.root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")

  def digest(self, relative: str) -> str:
    data = (self.root / relative).read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha256(data).hexdigest()

  def run_check(self, deviations=(), vendored=(),
                override_sources=frozenset()) -> list[str]:
    return validate_stock_tree_integrity(
      self.root, self.reference, deviations, vendored, override_sources)

  def deviate(self, relative: str, text: str) -> tuple[str, str]:
    """Return the (reference, current) hashes after editing a stock file."""

    reference = self.digest(relative)
    self.write(relative, text)
    return reference, self.digest(relative)

  def test_unchanged_stock_tree_passes(self) -> None:
    self.assertEqual(self.run_check(), [])

  def test_race_owned_change_is_ignored(self) -> None:
    self.write("src/game/race/race.c", self.RACE + " edited")
    self.assertEqual(self.run_check(), [])

  def test_race_delivery_files_are_shared_registration(self) -> None:
    for relative in (
      "Quetoo.vs15/Smoke-QuetooRace.ps1",
      "Quetoo.vs15/check-race-weapon-movement.vcxproj",
      "Quetoo.vs15/check-race-weapon-movement.vcxproj.filters",
      "Quetoo.vs15/check-race-weapon-tuning.vcxproj",
      "Quetoo.vs15/check-race-weapon-tuning.vcxproj.filters",
      "share/Makefile.am",
    ):
      with self.subTest(relative=relative):
        self.assertFalse(stock_owned(relative))

  def test_undeclared_stock_edit_is_rejected(self) -> None:
    self.write("src/game/common/g_local.h", self.STOCK + " edited")
    self.assertTrue(any("undeclared stock edit: src/game/common/g_local.h" in e
                        for e in self.run_check()))

  def test_ledgered_override_source_uses_the_override_audit(self) -> None:
    relative = "src/game/common/g_local.h"
    self.write(relative, self.STOCK + " overridden")
    self.assertEqual(self.run_check(override_sources=frozenset((relative,))), [])

  def test_editing_another_modules_build_file_is_rejected(self) -> None:
    self.write("src/game/lithium/Makefile.am", self.STOCK + " edited")
    self.assertTrue(any("undeclared stock edit: src/game/lithium/Makefile.am"
                        in e for e in self.run_check()))

  def test_declared_deviation_is_accepted(self) -> None:
    relative = "src/client/ui/ui_types.h"
    reference, current = self.deviate(relative, self.STOCK + " deviated")
    self.assertEqual(
      self.run_check(((relative, reference, current, "declared"),)), [])

  def test_re_editing_a_declared_deviation_is_rejected(self) -> None:
    relative = "src/client/ui/ui_types.h"
    reference, current = self.deviate(relative, self.STOCK + " deviated")
    self.write(relative, self.STOCK + " deviated again")
    errors = self.run_check(((relative, reference, current, "declared"),))
    self.assertTrue(any("stock deviation content moved" in e for e in errors))

  def test_upstream_move_underneath_a_deviation_is_rejected(self) -> None:
    relative = "src/client/ui/ui_types.h"
    _, current = self.deviate(relative, self.STOCK + " deviated")
    errors = self.run_check(((relative, "0" * 64, current, "declared"),))
    self.assertTrue(any("stock deviation reference moved" in e
                        for e in errors))

  def test_stale_deviation_is_rejected(self) -> None:
    deviation = (("src/client/ui/ui_types.h", "0" * 64, "1" * 64, "stale"),)
    self.assertTrue(any("stale stock deviation" in e
                        for e in self.run_check(deviation)))

  def test_untracked_stock_addition_is_rejected(self) -> None:
    self.write("src/game/common/g_race_hack.c", self.RACE)
    self.assertTrue(any("untracked file in stock source tree" in e
                        for e in self.run_check()))

  def test_deleted_stock_file_is_rejected(self) -> None:
    (self.root / "src/game/common/g_local.h").unlink()
    self.assertTrue(any("stock tree deletion" in e
                        for e in self.run_check()))

  def test_declared_vendored_tree_is_accepted(self) -> None:
    self.write("deps/argon2/src/ref.c", "/* vendored */")
    vendored = (("deps/argon2/", "deps/Makefile.am", "EXTRA_DIST", "argon2"),)
    self.assertEqual(self.run_check(vendored=vendored), [])

  def test_undistributed_vendored_file_is_rejected(self) -> None:
    self.write("deps/argon2/src/ref.c", "/* vendored */")
    self.write("deps/argon2/src/injected.c", "/* injected */")
    vendored = (("deps/argon2/", "deps/Makefile.am", "EXTRA_DIST", "argon2"),)
    self.assertTrue(any("undistributed vendored file" in e
                        for e in self.run_check(vendored=vendored)))


if __name__ == "__main__":
  unittest.main()
