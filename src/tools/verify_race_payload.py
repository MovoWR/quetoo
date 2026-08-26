#!/usr/bin/env python3
"""Verify the exact integrated Race source or runtime payload inventory."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path, PurePosixPath


CONTRACT = Path(__file__).with_name("race_payload_files.json")
UI_EXTENSIONS = {".css", ".json", ".png", ".ttf", ".txt"}


def safe_paths(values: object, label: str) -> tuple[str, ...]:
  if not isinstance(values, list) or not values:
    raise ValueError(f"{label} must be a non-empty list")

  result: list[str] = []
  for value in values:
    if not isinstance(value, str):
      raise ValueError(f"{label} contains a non-string path")
    path = PurePosixPath(value)
    if (not value or value != path.as_posix() or path.is_absolute()
        or any(part in {"", ".", ".."} for part in path.parts)):
      raise ValueError(f"{label} contains an unsafe path: {value!r}")
    result.append(value)

  if len(result) != len(set(result)):
    raise ValueError(f"{label} contains a duplicate path")
  return tuple(result)


def load_contract(path: Path = CONTRACT) -> dict[str, tuple[str, ...]]:
  try:
    raw = json.loads(path.read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError) as error:
    raise ValueError(f"cannot read payload contract {path}: {error}") from error

  if not isinstance(raw, dict) or raw.get("format") != 1:
    raise ValueError("payload contract format must be 1")
  if set(raw) != {"format", "modules", "ui", "data"}:
    raise ValueError("payload contract has missing or unexpected fields")

  contract = {
    name: safe_paths(raw[name], name) for name in ("modules", "ui", "data")
  }
  if contract["modules"] != ("game", "cgame"):
    raise ValueError("payload contract must declare game and cgame modules")
  if any(PurePosixPath(path).suffix.lower() not in UI_EXTENSIONS
         for path in contract["ui"]):
    raise ValueError("payload contract contains a forbidden UI extension")
  if "manifest.mf" not in contract["data"]:
    raise ValueError("payload contract must declare manifest.mf")
  return contract


def expected_paths(layout: str,
                   contract: dict[str, tuple[str, ...]]) -> set[str]:
  modules = contract["modules"]
  ui = contract["ui"]
  data = contract["data"]

  if layout == "source":
    return {
      *(f"src/cgame/race/ui/{path}" for path in ui),
      *(f"share/race/{path}" for path in data),
    }
  if layout == "windows":
    return {
      *(f"lib/race/{module}.dll" for module in modules),
      *(f"lib/race/{module}.pdb" for module in modules),
      *(f"lib/race/ui/{path}" for path in ui),
      *(f"share/race/{path}" for path in data),
    }
  if layout == "linux":
    return {
      *(f"lib/quetoo/race/{module}.so" for module in modules),
      *(f"lib/quetoo/race/ui/{path}" for path in ui),
      *(f"share/quetoo/race/{path}" for path in data),
    }
  if layout == "macos-app":
    return {
      *(f"Contents/MacOS/lib/quetoo/race/{module}.so"
        for module in modules),
      *(f"Contents/Resources/race/ui/{path}" for path in ui),
      *(f"Contents/Resources/race/{path}" for path in data),
    }
  if layout == "macos-package":
    return {
      *(f"race/{module}.so" for module in modules),
      *(f"race/ui/{path}" for path in ui),
      *(f"race/{path}" for path in data),
    }
  raise ValueError(f"unknown payload layout: {layout}")


def inventory_roots(layout: str) -> tuple[str, ...]:
  return {
    "source": ("src/cgame/race/ui", "share/race"),
    "windows": ("lib/race", "share/race"),
    "linux": ("lib/quetoo/race", "share/quetoo/race"),
    "macos-app": ("Contents/MacOS/lib/quetoo/race",
                  "Contents/Resources/race"),
    "macos-package": ("race",),
  }[layout]


def inventory(root: Path, roots: tuple[str, ...],
              layout: str) -> tuple[set[str], list[str]]:
  actual: set[str] = set()
  unsafe: list[str] = []

  for name in roots:
    base = root / name
    if not base.exists() and not base.is_symlink():
      continue
    for current_root, directories, files in os.walk(base, followlinks=False):
      current = Path(current_root)
      for entry in [*directories, *files]:
        path = current / entry
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
          unsafe.append(relative)
        elif path.is_file():
          if layout == "source":
            if (relative.startswith("src/cgame/race/ui/")
                and path.suffix.lower() not in UI_EXTENSIONS):
              continue
            if relative in {"share/race/Makefile.am", "share/race/Makefile.in"}:
              continue
          actual.add(relative)
  return actual, unsafe


def verify(root: Path, layout: str, contract_path: Path = CONTRACT) -> list[str]:
  errors: list[str] = []
  try:
    contract = load_contract(contract_path)
    expected = expected_paths(layout, contract)
  except ValueError as error:
    return [str(error)]

  actual, unsafe = inventory(root, inventory_roots(layout), layout)
  for path in unsafe:
    errors.append(f"payload path is a symbolic link: {path}")
  for path in sorted(expected - actual):
    errors.append(f"payload file is missing: {path}")
  for path in sorted(actual - expected):
    errors.append(f"payload file is unexpected: {path}")
  return errors


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--root", type=Path, required=True)
  parser.add_argument("--layout", required=True, choices=(
    "source", "windows", "linux", "macos-app", "macos-package"))
  parser.add_argument("--contract", type=Path, default=CONTRACT)
  args = parser.parse_args()

  errors = verify(args.root.resolve(), args.layout, args.contract.resolve())
  if errors:
    for error in errors:
      print(f"ERROR: {error}", file=sys.stderr)
    return 1

  contract = load_contract(args.contract.resolve())
  runtime = len(contract["modules"]) + len(contract["ui"]) + len(contract["data"])
  verified = len(expected_paths(args.layout, contract))
  print("RACE_PAYLOAD_PASS "
        f"layout={args.layout} files={verified} runtime={runtime} "
        f"modules={len(contract['modules'])} ui={len(contract['ui'])} "
        f"data={len(contract['data'])}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
