"""Structurally validate native Race UI resources and source contracts."""

import argparse
import json
import re
import sys
from pathlib import Path


RESOURCE = re.compile(r'"(ui/[A-Za-z0-9_./-]+\.(?:json|css|png|ttf))"')
OUTLET = re.compile(r'MakeOutlet\("([A-Za-z0-9_-]+)"')


def json_identifiers(value: object) -> set[str]:
  identifiers: set[str] = set()
  if isinstance(value, dict):
    identifier = value.get("identifier")
    if isinstance(identifier, str):
      identifiers.add(identifier)
    for child in value.values():
      identifiers.update(json_identifiers(child))
  elif isinstance(value, list):
    for child in value:
      identifiers.update(json_identifiers(child))
  return identifiers


def empty_text_bindings(value: object, path: str = "$") -> list[str]:
  bindings: list[str] = []
  if isinstance(value, dict):
    for key, child in value.items():
      child_path = f"{path}.{key}"
      if key == "text" and child == "":
        bindings.append(child_path)
      bindings.extend(empty_text_bindings(child, child_path))
  elif isinstance(value, list):
    for index, child in enumerate(value):
      bindings.extend(empty_text_bindings(child, f"{path}[{index}]"))
  return bindings


def css_error(text: str) -> str | None:
  """Check comments, strings and block balance without accepting junk tails."""

  depth = 0
  quote = ""
  escaped = False
  comment = False
  index = 0
  while index < len(text):
    char = text[index]
    next_char = text[index + 1] if index + 1 < len(text) else ""
    if comment:
      if char == "*" and next_char == "/":
        comment = False
        index += 2
        continue
    elif quote:
      if escaped:
        escaped = False
      elif char == "\\":
        escaped = True
      elif char == quote:
        quote = ""
    elif char == "/" and next_char == "*":
      comment = True
      index += 2
      continue
    elif char in "\"'":
      quote = char
    elif char == "{":
      depth += 1
    elif char == "}":
      depth -= 1
      if depth < 0:
        return "unmatched closing brace"
    index += 1
  if comment:
    return "unterminated comment"
  if quote:
    return "unterminated string"
  if depth:
    return "unclosed rule block"
  return None


def require_markers(root: Path, relative: str,
                    markers: tuple[str, ...]) -> list[str]:
  text = (root / relative).read_text(encoding="utf-8")
  return [f"{relative}: missing lifecycle marker: {marker}"
          for marker in markers if marker not in text]


def reject_markers(root: Path, relative: str,
                   markers: tuple[str, ...]) -> list[str]:
  text = (root / relative).read_text(encoding="utf-8")
  return [f"{relative}: legacy optimistic marker remains: {marker}"
          for marker in markers if marker in text]


def verify(root: Path) -> int:
  errors: list[str] = []
  ui = root / "src/cgame/race/ui"
  json_files = sorted(ui.rglob("*.json"))
  css_files = sorted(ui.rglob("*.css"))
  sources = sorted(ui.rglob("*.c"))

  fonts = ui / "fonts"
  expected_fonts = {"Coda-Regular.ttf"}
  actual_fonts = {path.name for path in fonts.iterdir() if path.is_file()}
  if actual_fonts != expected_fonts:
    errors.append(
      f"src/cgame/race/ui/fonts: expected {sorted(expected_fonts)}, "
      f"found {sorted(actual_fonts)}")
  generated = sorted(
    path.relative_to(root).as_posix() for path in ui.rglob("*")
    if path.name == "__pycache__" or path.suffix == ".pyc")
  if generated:
    errors.append(f"Race UI contains generated Python artifacts: {generated}")

  makefile = (root / "src/cgame/race/Makefile.am").read_text()
  project = (root / "Quetoo.vs15/cgame-race.vcxproj").read_text()
  for font in sorted(expected_fonts):
    if f"ui/fonts/{font}" not in makefile:
      errors.append(f"src/cgame/race/Makefile.am: font missing: {font}")
    if f"ui\\fonts\\{font}" not in project:
      errors.append(f"Quetoo.vs15/cgame-race.vcxproj: font missing: {font}")

  parsed: dict[Path, set[str]] = {}
  for path in json_files:
    try:
      document = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
      errors.append(f"{path.relative_to(root).as_posix()}: JSON parse: {error}")
      continue
    identifiers = json_identifiers(document)
    if not identifiers:
      errors.append(f"{path.relative_to(root).as_posix()}: no native identifiers")
    relative = path.relative_to(root).as_posix()
    for binding in empty_text_bindings(document):
      errors.append(
        f"{relative}: empty text binding is unsafe during native binding: "
        f"{binding}")
    parsed[path.resolve()] = identifiers

  for path in css_files:
    error = css_error(path.read_text(encoding="utf-8"))
    if error:
      errors.append(f"{path.relative_to(root).as_posix()}: CSS parse: {error}")

  resource_count = 0
  outlet_count = 0
  for source in sources:
    text = source.read_text(encoding="utf-8")
    resources = RESOURCE.findall(text)
    resource_count += len(resources)
    json_resources = []
    for resource in resources:
      path = (root / "src/cgame/race" / resource).resolve()
      common_path = (root / "src/cgame/common" / resource).resolve()
      if not path.is_file() and not common_path.is_file():
        errors.append(
          f"{source.relative_to(root).as_posix()}: missing resource {resource}")
      if resource.endswith(".json"):
        json_resources.append(path)

    outlets = set(OUTLET.findall(text))
    outlet_count += len(outlets)
    if outlets:
      identifiers = set()
      for resource in json_resources:
        identifiers.update(parsed.get(resource, set()))
      missing = sorted(outlets - identifiers)
      if not json_resources:
        errors.append(
          f"{source.relative_to(root).as_posix()}: outlets have no JSON owner")
      elif missing:
        errors.append(
          f"{source.relative_to(root).as_posix()}: missing outlets {missing}")

  behavior = {
    "src/cgame/race/ui/main/MainView.c": (
      "static void layoutSubviews(View *self)",
      "super(View, self, layoutSubviews);",
      "MainView_RevealView",
      "Cg_Module_UpdateUi(&cgi.client->frame.ps);",
    ),
    "src/cgame/race/ui/main/MainViewController.c": (
      "super(ViewController, self, viewWillAppear);",
      "super(ViewController, self, respondToEvent, event);",
      "this->focusedIdentifier",
      "becomeKeyResponder",
      "if (menuActive && !this->menuActive)",
      "cgi.SetKeyDest(KEY_GAME);",
      "_HomeViewController()",
      "_ControlsViewController()",
      "_SettingsViewController()",
      "_VotingViewController()",
      "_MapBrowserViewController()",
      "_AdminViewController()",
      "capabilities == 0",
      "SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED",
      "relayoutForViewportChange(this);",
    ),
    "src/cgame/race/ui/home/HomeViewController.c": (
      "super(ViewController, self, loadView);",
      "super(ViewController, self, viewWillAppear);",
      "super(ViewController, self, viewWillDisappear);",
      "const bool isActive = *cgi.state == CL_ACTIVE",
      "this->disconnectedPanel->view.hidden = isActive;",
      "this->dashboardRoot->view.hidden = !isActive;",
    ),
    "src/cgame/race/ui/admin/AdminViewController.c": (
      "super(ViewController, viewController, loadView);",
      "super(ViewController, viewController, viewWillAppear);",
      "super(ViewController, viewController, respondToEvent, event);",
      "descriptor->capability && !(self->capabilities & descriptor->capability)",
      "!(self->capabilities & adminRows[row].capability)",
      "Cg_RaceAdminCommand_MapToken(cgi.ConfigString(CS_BSP), buffer)",
      "Cg_RaceAdminCommand_TokenValid(textValue(self->mapName))",
    ),
    "src/cgame/race/cg_race_admin_command.c": (
      "length <= 4u || length - 4u >= MAX_QPATH",
      "Cg_RaceAdminCommand_EqualsIgnoringCase(name + length - 4u,",
      "Cg_RaceAdminCommand_TokenValid(output)",
    ),
    "src/game/race/race_weapon_tuning.c": (
      "static const race_weapon_tuning_descriptor_t race_weapon_tuning_catalog[]",
      "Race_WeaponTuning_CatalogHash",
      "Race_WeaponTuning_SnapshotValid",
      "isfinite(",
    ),
    "src/game/race/race_weapon_tuning_wire.c": (
      "message->descriptor_count != RACE_WEAPON_TUNING_VALUE_COUNT",
      "parsed.descriptor_count != RACE_WEAPON_TUNING_VALUE_COUNT",
      "message->request_id",
      "isfinite(",
    ),
    "src/cgame/race/cg_race_weapon_tuning.c": (
      "Cg_RaceWeaponTuning_StreamMatches",
      "sync->request_id != begin.request_id",
      "pending->request_id != result.request_id",
      "end.descriptor_count != RACE_WEAPON_TUNING_VALUE_COUNT",
      "next.complete = true;",
      "!cache->complete || !cache->synchronized",
      "if (!request_id) {",
      "Cg_RaceWeaponTuning_RequestSync",
      "Cg_RaceWeaponTuning_RequestApply",
      "Cg_RaceWeaponTuning_RequestResetAll",
      "isfinite(",
    ),
    "src/cgame/race/ui/admin/WeaponLabViewController.h": (
      "#define WEAPON_LAB_ROW_COUNT RACE_WEAPON_TUNING_VALUE_COUNT",
    ),
    "src/cgame/race/ui/admin/WeaponLabViewController.c": (
      "return Cg_RaceWeaponTuning_Cache();",
      "cache->complete && cache->synchronized",
      "Cg_RaceWeaponTuning_CanonicalValue",
      "Cg_RaceWeaponTuning_RequestApply",
      "Cg_RaceWeaponTuning_RequestResetAll",
      "Cg_RaceWeaponTuning_MutationPending",
      "cache->revision == activeWeaponLab->cacheRevision",
      "statusState == activeWeaponLab->statusState",
      "mutationPending == activeWeaponLab->mutationPending",
      "isfinite(",
    ),
  }
  for relative, markers in behavior.items():
    errors.extend(require_markers(root, relative, markers))

  # The native route must derive the requested 22-row catalog from the shared
  # GAME wire contract and must never promote its own request or generation
  # locally.
  tuning_types = (root / "src/game/race/race_weapon_tuning_types.h").read_text(
    encoding="utf-8")
  value_count = re.search(
    r"^#define\s+RACE_WEAPON_TUNING_VALUE_COUNT\s+(\d+)u?\s*$",
    tuning_types, re.MULTILINE)
  if not value_count or int(value_count.group(1)) != 22:
    found = value_count.group(1) if value_count else "missing"
    errors.append(
      "src/game/race/race_weapon_tuning_types.h: authoritative weapon "
      f"catalog count is {found}, expected 22")

  legacy = {
    "src/cgame/race/cg_race_weapon_tuning.h": (
      "Cg_RaceWeaponTuning_RequestBegin",
      "Cg_RaceWeaponTuning_RequestEnd",
      "Cg_RaceWeaponTuning_RequestAbort",
      "Cg_RaceWeaponTuning_RequestUndo",
      "Cg_RaceWeaponTuning_RequestResetKey",
      "Cg_RaceWeaponTuning_RequestResetGroup",
      "Cg_RaceWeaponTuning_RequestSlot",
      "Cg_RaceWeaponTuning_RequestExport",
      "Cg_RaceWeaponTuning_RequestLoadNamed",
    ),
    "src/cgame/race/cg_race_weapon_tuning.c": (
      "Cg_RaceWeaponTuning_RequestBegin",
      "Cg_RaceWeaponTuning_RequestEnd",
      "Cg_RaceWeaponTuning_RequestAbort",
      "Cg_RaceWeaponTuning_RequestUndo",
      "Cg_RaceWeaponTuning_RequestResetKey",
      "Cg_RaceWeaponTuning_RequestResetGroup",
      "Cg_RaceWeaponTuning_RequestSlot",
      "Cg_RaceWeaponTuning_RequestExport",
      "Cg_RaceWeaponTuning_RequestLoadNamed",
    ),
    "src/cgame/race/ui/admin/WeaponLabViewController.h": (
      "#define WEAPON_LAB_ROW_COUNT 15",
      "local and optimistic in exactly",
      "presetButtons",
      "sessionButton",
      "readiness",
      "snapshotName",
      "slotButtons",
      "undoButton",
      "exportButton",
      "beginButton",
      "endButton",
    ),
    "src/cgame/race/ui/admin/WeaponLabViewController.c": (
      "commitSnapshot(",
      "postCommand(",
      "self->generation++",
      "presetButtons",
      "sessionButton",
      "snapshotName",
      "slotButtons",
      "undoButton",
      "exportButton",
      "beginButton",
      "endButton",
    ),
    "src/cgame/race/ui/admin/WeaponLabViewController.json": (
      '"identifier": "weaponLabPreset',
      '"identifier": "weaponLabSession',
      '"identifier": "weaponLabReadiness',
      '"identifier": "weaponLabSnapshot',
      '"identifier": "weaponLabName',
      '"identifier": "weaponLabSlot',
      '"identifier": "weaponLabUndo',
      '"identifier": "weaponLabExport',
      '"identifier": "weaponLabBegin',
      '"identifier": "weaponLabEnd',
      '"identifier": "weaponLabAbort',
      '"identifier": "weaponLabLoad',
    ),
  }
  for relative, markers in legacy.items():
    errors.extend(reject_markers(root, relative, markers))

  # The design ships one tab strip of exactly seven routes, in this order; the
  # tier-1 drawer and the Maps route reach voting instead of a Voting tab. This
  # is the strip itself, read back from the shell rather than asserted as a
  # count, so a route added or reordered here fails rather than passing quietly.
  design_routes = ["Home", "Play", "Controls", "Settings",
                   "Maps", "Credits", "Admin"]
  shell = (root / "src/cgame/race/ui/main/MainViewController.c").read_text(
    encoding="utf-8")
  actual_routes = re.findall(r'primaryButton, "([^"]+)"', shell)
  if actual_routes != design_routes:
    errors.append(
      "src/cgame/race/ui/main/MainViewController.c: route strip is "
      f"{actual_routes}, expected {design_routes}")

  if errors:
    for error in errors:
      print(f"ERROR: {error}", file=sys.stderr)
    return 1

  print("RACE_UI_VERIFY_PASS "
        f"json={len(json_files)} css={len(css_files)} "
        f"resources={resource_count} outlets={outlet_count} "
        f"production_fonts={len(actual_fonts)} "
        f"routes={len(actual_routes)} "
        f"weapon_tuning_values={int(value_count.group(1))} "
        "structural_lifecycle=1 structural_focus=1 structural_escape=1 "
        "structural_resize=1 structural_home_states=2 "
        "structural_admin_capabilities=1 structural_authoritative_cache=1 "
        "structural_request_correlation=1 structural_finite_values=1")
  return 0


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--root", type=Path,
                      default=Path(__file__).resolve().parents[2])
  args = parser.parse_args()
  return verify(args.root.resolve())


if __name__ == "__main__":
  raise SystemExit(main())
