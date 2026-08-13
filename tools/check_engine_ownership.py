#!/usr/bin/env python3
"""Enforce single ownership of engine runtime implementation sources."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


ENGINE_SOURCES = {
    "src/readers.cpp",
    "src/runtime.cpp",
    "src/campaign_control_abi.cpp",
    "src/artisan_consumption_hook.cpp",
    "src/factory_consumption_hook.cpp",
    "src/factory_sales_hook.cpp",
    "src/pop_cash_flow_hook.cpp",
}
REMOVED_TARGETS = ("add_library(smedley_game_state", "add_library(smedley_game_runtime")
MIGRATED_PLUGINS = (Path("plugins/campaign_runner"), Path("plugins/scripting"))


def audit(root: Path) -> list[str]:
    findings: list[str] = []
    game_state_cmake = (root / "game_state/CMakeLists.txt").read_text(encoding="utf-8")
    if "target_sources(smedley_kernel_runtime PRIVATE" not in game_state_cmake:
        findings.append("game_state/CMakeLists.txt: engine sources must belong to smedley_kernel")
    for source in sorted(ENGINE_SOURCES):
        if game_state_cmake.count(source) != 1:
            findings.append(f"game_state/CMakeLists.txt: expected one smedley_kernel source entry for {source}")

    for cmake in root.rglob("CMakeLists.txt"):
        text = cmake.read_text(encoding="utf-8")
        relative = cmake.relative_to(root).as_posix()
        for removed_target in REMOVED_TARGETS:
            if removed_target in text:
                findings.append(f"{relative}: removed engine runtime target recreated: {removed_target}")
        if cmake != root / "game_state/CMakeLists.txt":
            for source in ENGINE_SOURCES:
                if f"game_state/{source}" in text or source in text:
                    findings.append(f"{relative}: engine source has a second owner: {source}")

    for plugin in MIGRATED_PLUGINS:
        for source in (root / plugin).rglob("*"):
            if source.suffix not in {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"} or "tests" in source.parts:
                continue
            text = source.read_text(encoding="utf-8")
            if "smedley/game_state/" in text:
                findings.append(
                    f"{source.relative_to(root).as_posix()}: migrated plugin imports internal game_state C++ API"
                )
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).parents[1])
    args = parser.parse_args()
    findings = audit(args.root.resolve())
    for finding in findings:
        print(finding, file=sys.stderr)
    if findings:
        return 1
    print("OK: engine runtime implementation has one owner")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
