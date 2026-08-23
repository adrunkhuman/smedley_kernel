#!/usr/bin/env python3
"""Build GFM-smedley from a pinned installed GFM v3.0 copy."""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path


PINNED_CLEANUP_SHA256 = {
    "8623751901cf6d7ad031a31db5e5ec672504b1409d00e8883a8ea76f4cc1bd68",
    "d430005b890b3e7da74d333d396251820c40cd16c23ecab59bb300701ca4a289",
}
PINNED_GFM_REVISION = "ee1486e251d3bb4dc5c355f5c2ec34abaea1078d"
DESCRIPTOR_NAME = "GFMSmedley.mod"
SUBMOD_DIRECTORY = "GFM-smedley"
CLEANUP_RELATIVE = Path("events/V2ME Cleanup (GFM).txt")
OUTPUT_RELATIVE = Path(f"mod/{SUBMOD_DIRECTORY}/events/V2ME Cleanup (GFM).txt")
STAMP_RELATIVE = Path(f"mod/{SUBMOD_DIRECTORY}/smedley-required-plugins.txt")
DESCRIPTOR = (
    'name = "GFM-smedley"\r\n'
    'path = "mod/GFM-smedley"\r\n'
    'user_dir = "GFM-smedley"\r\n'
    'replace_path = "history"\r\n'
    'replace_path = "events"\r\n'
    'replace_path = "decisions"\r\n'
    'replace_path = "gfx"\r\n'
    'replace_path = "tutorial"\r\n'
).encode()

_NL = rb"\r?\n"
_CLEANUP_BLOCK = re.compile(
    rb"\t\tany_land_province = \{"
    + _NL
    + rb"\t\t\tlimit = \{"
    + _NL
    + rb"\t\t\t\tOR = \{"
    + _NL
    + rb"\t\t\t\t\thas_province_modifier = slave_trade_decadence_1"
    + _NL
    + rb"\t\t\t\t\thas_province_modifier = slave_trade_decadence_2"
    + _NL
    + rb"\t\t\t\t\thas_province_modifier = slave_trade_decadence_3"
    + _NL
    + rb"\t\t\t\t\thas_province_modifier = slave_trade_decadence_4"
    + _NL
    + rb"\t\t\t\t\}"
    + _NL
    + rb"\t\t\t\towner = \{ slavery = no_slavery \}"
    + _NL
    + rb"\t\t\t\}"
    + _NL
    + rb"\t\t\tremove_province_modifier = slave_trade_decadence_1"
    + _NL
    + rb"\t\t\tremove_province_modifier = slave_trade_decadence_2"
    + _NL
    + rb"\t\t\tremove_province_modifier = slave_trade_decadence_3"
    + _NL
    + rb"\t\t\tremove_province_modifier = slave_trade_decadence_4"
    + _NL
    + rb"\t\t\}"
    + _NL
)


def remove_cleanup_block(source: bytes) -> bytes:
    """Remove the one pinned slave-trade reconciliation block."""
    matches = list(_CLEANUP_BLOCK.finditer(source))
    if len(matches) != 1:
        raise ValueError(f"expected one pinned slave-trade cleanup block, found {len(matches)}")
    match = matches[0]
    return source[: match.start()] + source[match.end() :]


def _export_pinned_gfm(repository: Path, destination: Path) -> Path:
    try:
        revision = subprocess.run(
            ["git", "-C", str(repository), "rev-parse", "--verify", f"{PINNED_GFM_REVISION}^{{commit}}"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError(f"could not resolve pinned GFM revision {PINNED_GFM_REVISION}") from error
    if revision != PINNED_GFM_REVISION:
        raise ValueError(f"pinned GFM revision resolved unexpectedly: {revision}")

    process = subprocess.Popen(
        ["git", "-C", str(repository), "archive", "--format=tar", PINNED_GFM_REVISION, "GFM"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert process.stdout is not None
    try:
        with tarfile.open(fileobj=process.stdout, mode="r|") as archive:
            archive.extractall(destination, filter="data")
    except (OSError, tarfile.TarError) as error:
        process.kill()
        process.wait()
        raise ValueError("could not extract pinned GFM archive") from error
    assert process.stderr is not None
    stderr = process.stderr.read()
    process.wait()
    if process.returncode != 0:
        raise ValueError(f"could not export pinned GFM revision: {stderr.decode(errors='replace').strip()}")
    return destination / "GFM"


def build(game_dir: Path, gfm_repository: Path) -> tuple[Path, Path]:
    """Export pinned GFM and write the generated GFM-smedley mod."""

    descriptor_path = game_dir / "mod" / DESCRIPTOR_NAME
    output_path = game_dir / OUTPUT_RELATIVE
    stamp_path = game_dir / STAMP_RELATIVE
    output_root = game_dir / "mod" / SUBMOD_DIRECTORY
    if (descriptor_path.exists() or output_root.exists()) and not stamp_path.is_file():
        raise FileExistsError(f"refusing to replace unmanaged descriptor: {descriptor_path}")

    (game_dir / "mod").mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".gfm-smedley-", dir=game_dir / "mod") as staging_directory:
        staged_root = _export_pinned_gfm(gfm_repository, Path(staging_directory))
        staged_cleanup = staged_root / CLEANUP_RELATIVE
        source = staged_cleanup.read_bytes()
        digest = hashlib.sha256(source).hexdigest()
        if digest not in PINNED_CLEANUP_SHA256:
            raise ValueError(f"unsupported GFM cleanup source SHA-256: {digest}")
        staged_cleanup.write_bytes(remove_cleanup_block(source))
        (staged_root / "smedley-required-plugins.txt").write_bytes(
            f"schema_version=1\nrequired_plugin=gfm-smedley\ngfm_revision={PINNED_GFM_REVISION}\n"
            f"gfm_cleanup_sha256={digest}\nremoved_blocks=1\n".encode()
        )
        staged_descriptor = Path(staging_directory) / f"{DESCRIPTOR_NAME}.new"
        staged_descriptor.write_bytes(DESCRIPTOR)
        backup_directory = Path(tempfile.mkdtemp(prefix=".gfm-smedley-backup-", dir=game_dir / "mod"))
        backup_root = backup_directory / SUBMOD_DIRECTORY
        backup_descriptor = backup_directory / DESCRIPTOR_NAME
        root_backed_up = False
        descriptor_backed_up = False
        root_installed = False
        descriptor_installed = False
        try:
            if output_root.exists():
                output_root.replace(backup_root)
                root_backed_up = True
            if descriptor_path.exists():
                descriptor_path.replace(backup_descriptor)
                descriptor_backed_up = True
            staged_root.replace(output_root)
            root_installed = True
            staged_descriptor.replace(descriptor_path)
            descriptor_installed = True
        except OSError:
            if descriptor_installed:
                descriptor_path.unlink()
            if descriptor_backed_up:
                backup_descriptor.replace(descriptor_path)
            if root_installed:
                shutil.rmtree(output_root)
            if root_backed_up:
                backup_root.replace(output_root)
            shutil.rmtree(backup_directory)
            raise
        shutil.rmtree(backup_directory)
    return descriptor_path, output_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", required=True, type=Path)
    parser.add_argument("--gfm-repository", required=True, type=Path)
    args = parser.parse_args()
    try:
        descriptor, event_file = build(args.game_dir.resolve(), args.gfm_repository.resolve())
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(descriptor)
    print(event_file)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
