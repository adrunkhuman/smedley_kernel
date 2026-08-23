"""Tests for the pinned GFM-smedley generator."""

from __future__ import annotations

import shutil
import sys
import tempfile
import unittest
from hashlib import sha256
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).parents[1]))
import build_gfm_smedley as submod


BLOCK = b"""\t\tany_land_province = {
\t\t\tlimit = {
\t\t\t\tOR = {
\t\t\t\t\thas_province_modifier = slave_trade_decadence_1
\t\t\t\t\thas_province_modifier = slave_trade_decadence_2
\t\t\t\t\thas_province_modifier = slave_trade_decadence_3
\t\t\t\t\thas_province_modifier = slave_trade_decadence_4
\t\t\t\t}
\t\t\t\towner = { slavery = no_slavery }
\t\t\t}
\t\t\tremove_province_modifier = slave_trade_decadence_1
\t\t\tremove_province_modifier = slave_trade_decadence_2
\t\t\tremove_province_modifier = slave_trade_decadence_3
\t\t\tremove_province_modifier = slave_trade_decadence_4
\t\t}
"""


class BuildGfmSmedleyTest(unittest.TestCase):
    def test_removes_only_the_exact_cleanup_block(self) -> None:
        source = b"before\n" + BLOCK + b"after\n"

        self.assertEqual(submod.remove_cleanup_block(source), b"before\nafter\n")

    def test_accepts_crlf_without_rewriting_other_bytes(self) -> None:
        block = BLOCK.replace(b"\n", b"\r\n")
        source = b"before\r\n" + block + b"after\r\n"

        self.assertEqual(submod.remove_cleanup_block(source), b"before\r\nafter\r\n")

    def test_rejects_missing_or_duplicate_blocks(self) -> None:
        with self.assertRaisesRegex(ValueError, "found 0"):
            submod.remove_cleanup_block(b"unrelated")
        with self.assertRaisesRegex(ValueError, "found 2"):
            submod.remove_cleanup_block(BLOCK + BLOCK)

    def test_upgrades_managed_pre_rename_package(self) -> None:
        source = b"before\n" + BLOCK + b"after\n"
        with tempfile.TemporaryDirectory() as directory:
            game_dir = Path(directory)
            source_path = game_dir / "source" / "GFM" / submod.CLEANUP_RELATIVE
            source_path.parent.mkdir(parents=True)
            source_path.write_bytes(source)
            unrelated = source_path.parents[1] / "map" / "provinces.bmp"
            unrelated.parent.mkdir()
            unrelated.write_bytes(b"not copied")
            managed_root = game_dir / "mod" / submod.SUBMOD_DIRECTORY
            managed_root.mkdir(parents=True)
            (game_dir / "mod" / submod.DESCRIPTOR_NAME).write_bytes(b"old descriptor")
            (game_dir / submod.STAMP_RELATIVE).write_bytes(b"schema_version=1\nrequired_plugin=gfm_adoption\n")

            def export_fixture(_repository: Path, destination: Path) -> Path:
                exported = destination / "GFM"
                shutil.copytree(source_path.parents[1], exported)
                return exported

            with (
                mock.patch.object(submod, "PINNED_CLEANUP_SHA256", {sha256(source).hexdigest()}),
                mock.patch.object(submod, "_export_pinned_gfm", side_effect=export_fixture),
            ):
                descriptor, event_file = submod.build(game_dir, game_dir / "source")

            self.assertEqual(descriptor.name, "GFMSmedley.mod")
            self.assertEqual(descriptor.read_bytes(), submod.DESCRIPTOR)
            self.assertEqual(event_file.read_bytes(), b"before\nafter\n")
            self.assertEqual(
                (game_dir / "mod" / submod.SUBMOD_DIRECTORY / "map" / "provinces.bmp").read_bytes(),
                b"not copied",
            )
            self.assertEqual(submod.DESCRIPTOR.count(b"replace_path"), 5)
            self.assertEqual(submod.DESCRIPTOR.count(b"\r\n"), 8)
            self.assertIn(b"required_plugin=gfm-smedley\n", (game_dir / submod.STAMP_RELATIVE).read_bytes())

    def test_preserves_installed_package_when_descriptor_swap_fails(self) -> None:
        source = b"before\n" + BLOCK + b"after\n"
        replacement = b"changed\n" + BLOCK + b"after\n"
        with tempfile.TemporaryDirectory() as directory:
            game_dir = Path(directory)
            source_path = game_dir / "source" / "GFM" / submod.CLEANUP_RELATIVE
            source_path.parent.mkdir(parents=True)
            source_path.write_bytes(source)

            def export_fixture(_repository: Path, destination: Path) -> Path:
                exported = destination / "GFM"
                shutil.copytree(source_path.parents[1], exported)
                return exported

            with (
                mock.patch.object(submod, "PINNED_CLEANUP_SHA256", {sha256(source).hexdigest()}),
                mock.patch.object(submod, "_export_pinned_gfm", side_effect=export_fixture),
            ):
                descriptor, event_file = submod.build(game_dir, game_dir / "source")

            source_path.write_bytes(replacement)
            original_replace = Path.replace

            def fail_descriptor_swap(path: Path, target: Path) -> Path:
                if path.name == f"{submod.DESCRIPTOR_NAME}.new":
                    raise OSError("simulated descriptor swap failure")
                return original_replace(path, target)

            with (
                mock.patch.object(submod, "PINNED_CLEANUP_SHA256", {sha256(replacement).hexdigest()}),
                mock.patch.object(submod, "_export_pinned_gfm", side_effect=export_fixture),
                mock.patch.object(Path, "replace", new=fail_descriptor_swap),
                self.assertRaisesRegex(OSError, "simulated descriptor swap failure"),
            ):
                submod.build(game_dir, game_dir / "source")

            self.assertEqual(descriptor.read_bytes(), submod.DESCRIPTOR)
            self.assertEqual(event_file.read_bytes(), b"before\nafter\n")
            self.assertEqual(list((game_dir / "mod").glob(".gfm-smedley-backup-*")), [])


if __name__ == "__main__":
    unittest.main()
