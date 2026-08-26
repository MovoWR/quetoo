#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from verify_race_payload import expected_paths, load_contract, verify


class RacePayloadVerifierTest(unittest.TestCase):

  def setUp(self) -> None:
    self.temporary = tempfile.TemporaryDirectory()
    self.root = Path(self.temporary.name)
    self.contract_path = self.root / "contract.json"
    self.contract = {
      "format": 1,
      "modules": ["game", "cgame"],
      "ui": ["main/Main.json", "icon.png"],
      "data": ["manifest.mf", "maps.lst"],
    }
    self.contract_path.write_text(json.dumps(self.contract), encoding="utf-8")

  def tearDown(self) -> None:
    self.temporary.cleanup()

  def populate(self, layout: str) -> None:
    contract = load_contract(self.contract_path)
    for relative in expected_paths(layout, contract):
      path = self.root / relative
      path.parent.mkdir(parents=True, exist_ok=True)
      path.write_bytes(b"fixture")

  def test_exact_windows_payload_passes(self) -> None:
    self.populate("windows")
    self.assertEqual(verify(self.root, "windows", self.contract_path), [])

  def test_windows_payload_rejects_development_artifacts(self) -> None:
    self.populate("windows")
    for filename in ("game.lib", "autoexec.cfg"):
      (self.root / "lib/race" / filename).write_bytes(b"unexpected")

    errors = verify(self.root, "windows", self.contract_path)
    self.assertIn("payload file is unexpected: lib/race/autoexec.cfg", errors)
    self.assertIn("payload file is unexpected: lib/race/game.lib", errors)

  def test_source_ignores_build_manifests(self) -> None:
    self.populate("source")
    race_data = self.root / "share/race"
    (race_data / "Makefile.am").write_bytes(b"source build input")
    (race_data / "Makefile.in").write_bytes(b"generated build input")
    self.assertEqual(verify(self.root, "source", self.contract_path), [])

  def test_missing_and_unexpected_files_fail(self) -> None:
    self.populate("linux")
    (self.root / "lib/quetoo/race/game.so").unlink()
    extra = self.root / "share/quetoo/race/maps/extra.bsp"
    extra.parent.mkdir(parents=True, exist_ok=True)
    extra.write_bytes(b"extra")
    errors = verify(self.root, "linux", self.contract_path)
    self.assertIn("payload file is missing: lib/quetoo/race/game.so", errors)
    self.assertIn("payload file is unexpected: share/quetoo/race/maps/extra.bsp",
                  errors)

  def test_duplicate_contract_path_fails_closed(self) -> None:
    self.contract["ui"].append("icon.png")
    self.contract_path.write_text(json.dumps(self.contract), encoding="utf-8")
    with self.assertRaisesRegex(ValueError, "duplicate"):
      load_contract(self.contract_path)

  def test_unsafe_contract_path_fails_closed(self) -> None:
    self.contract["data"].append("../escape")
    self.contract_path.write_text(json.dumps(self.contract), encoding="utf-8")
    with self.assertRaisesRegex(ValueError, "unsafe"):
      load_contract(self.contract_path)


if __name__ == "__main__":
  unittest.main()
