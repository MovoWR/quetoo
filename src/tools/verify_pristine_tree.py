#!/usr/bin/env python3
"""Prove that a candidate tree preserves every stock file and symbolic link."""

import argparse
import hashlib
import os
from pathlib import Path
import sys


Entry = tuple[str, str]


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--baseline", required=True, type=Path,
                      help="Untouched stock tree")
  parser.add_argument("--candidate", required=True, type=Path,
                      help="Tree after additive installation")
  parser.add_argument("--allow-addition", action="append", default=[],
                      help="Permitted relative file-prefix for additions")
  parser.add_argument("--expected-additions", type=int,
                      help="Required number of added files and links")
  return parser.parse_args()


def file_sha256(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as stream:
    while chunk := stream.read(1024 * 1024):
      digest.update(chunk)
  return digest.hexdigest()


def inventory(root: Path) -> dict[str, Entry]:
  entries: dict[str, Entry] = {}

  def visit(directory: Path) -> None:
    for item in sorted(os.scandir(directory), key=lambda entry: entry.name):
      path = Path(item.path)
      relative = path.relative_to(root).as_posix()
      if item.is_symlink():
        entries[relative] = ("symlink", os.readlink(path))
      elif item.is_dir(follow_symlinks=False):
        visit(path)
      elif item.is_file(follow_symlinks=False):
        entries[relative] = ("file", file_sha256(path))
      else:
        raise RuntimeError(f"unsupported filesystem entry: {relative}")

  visit(root)
  return entries


def normalized_prefix(value: str) -> str:
  prefix = value.replace("\\", "/").strip("/")
  if not prefix or prefix == "." or prefix.startswith("../") or "/../" in prefix:
    raise ValueError(f"unsafe addition prefix: {value}")
  return prefix


def main() -> int:
  args = parse_args()
  try:
    baseline_root = args.baseline.resolve(strict=True)
    candidate_root = args.candidate.resolve(strict=True)
    if not baseline_root.is_dir() or not candidate_root.is_dir():
      raise RuntimeError("baseline and candidate must both be directories")
    if baseline_root == candidate_root:
      raise RuntimeError("baseline and candidate must be distinct trees")

    prefixes = [normalized_prefix(value) for value in args.allow_addition]
    baseline = inventory(baseline_root)
    candidate = inventory(candidate_root)

    missing = sorted(set(baseline) - set(candidate))
    changed = sorted(
      path for path in set(baseline) & set(candidate)
      if baseline[path] != candidate[path]
    )
    additions = sorted(set(candidate) - set(baseline))
    unexpected = [
      path for path in additions
      if not any(path == prefix or path.startswith(prefix + "/")
                 for prefix in prefixes)
    ]

    if missing:
      raise RuntimeError("missing stock entries: " + ", ".join(missing))
    if changed:
      raise RuntimeError("changed stock entries: " + ", ".join(changed))
    if unexpected:
      raise RuntimeError("unexpected additions: " + ", ".join(unexpected))
    if args.expected_additions is not None \
       and len(additions) != args.expected_additions:
      raise RuntimeError(
        f"expected {args.expected_additions} additions, found {len(additions)}"
      )

    stock_files = sum(entry[0] == "file" for entry in baseline.values())
    stock_symlinks = sum(entry[0] == "symlink" for entry in baseline.values())
    print(
      "PRISTINE_TREE_PASS "
      f"stock_files={stock_files} stock_symlinks={stock_symlinks} "
      f"additions={len(additions)}"
    )
    for path in additions:
      print(f"ADDITION={path}")
    return 0
  except (OSError, RuntimeError, ValueError) as error:
    print(f"PRISTINE_TREE_FAIL: {error}", file=sys.stderr)
    return 1


if __name__ == "__main__":
  raise SystemExit(main())
