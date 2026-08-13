#!/usr/bin/env python3
"""Audit exact production DLL exports and reject kernel imports."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


EXPECTED_EXPORTS = {
    "campaign_runner": {"SmedleyPluginGetApiV1"},
    "interest_bug_fix": {"SmedleyPluginGetApiV1"},
    "scripting": {"SmedleyPluginGetApiV1"},
    "telemetry": {
        "SmedleyPluginGetApiV1",
        "SmedleyTelemetryDrainV1",
        "SmedleyTelemetryEmitReliableV1",
        "SmedleyTelemetryEmitV1",
    },
}
EXPECTED_KERNEL_EXPORTS = {
    "LoadPlugins",
    "LoadPluginsThread",
    "SmedleyGetCampaignControlApiV1",
    "SmedleyGetCampaignAutomationApiV1",
    "SmedleyGetCampaignRuntimeApiV1",
    "SmedleyGetEventApiV1",
    "SmedleyGetEventServicesApiV1",
    "SmedleyGetInterestPoolApiV1",
    "SmedleyGetLoggingApiV1",
    "SmedleyGetTelemetryGameApiV1",
    "SmedleyGetTelemetryObservationApiV1",
    "luaL_loadstring",
    "lua_pcall",
    "lua_tolstring",
}
EXPORT_LINE = re.compile(r"^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)", re.MULTILINE)
IMPORT_LINE = re.compile(r"^\s+[0-9A-F]+\s+(\S.*)$", re.MULTILINE)


def dump(dumpbin: Path, option: str, dll: Path) -> str:
    result = subprocess.run(
        [str(dumpbin), "/nologo", option, str(dll)],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def exports(dumpbin: Path, dll: Path) -> set[str]:
    return set(EXPORT_LINE.findall(dump(dumpbin, "/exports", dll)))


def kernel_imports(dumpbin: Path, dll: Path) -> list[str]:
    output = dump(dumpbin, "/imports", dll)
    marker = "smedley_kernel.dll"
    start = output.lower().find(marker)
    if start < 0:
        return []
    section = output[start + len(marker) :]
    next_dll = re.search(r"^\s+\S+\.dll\s*$", section, re.IGNORECASE | re.MULTILINE)
    if next_dll:
        section = section[: next_dll.start()]
    imports = []
    for line in section.splitlines():
        match = IMPORT_LINE.match(line)
        if match and not match.group(1).startswith(("Import ", "time date", "Index ")):
            imports.append(match.group(1).strip())
    return imports


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--linker", required=True, type=Path)
    parser.add_argument("--kernel", required=True, type=Path)
    for plugin in EXPECTED_EXPORTS:
        parser.add_argument(f"--{plugin.replace('_', '-')}", required=True, type=Path)
    args = parser.parse_args()
    dumpbin = args.linker.with_name("dumpbin.exe")
    if not dumpbin.is_file():
        print(f"dumpbin.exe not found beside linker: {args.linker}", file=sys.stderr)
        return 1

    failures: list[str] = []
    kernel_exports = exports(dumpbin, args.kernel)
    if kernel_exports != EXPECTED_KERNEL_EXPORTS:
        failures.append(
            f"smedley_kernel.dll exports {sorted(kernel_exports)}; expected {sorted(EXPECTED_KERNEL_EXPORTS)}"
        )

    for plugin, expected in EXPECTED_EXPORTS.items():
        dll = getattr(args, plugin)
        actual = exports(dumpbin, dll)
        if actual != expected:
            failures.append(f"{dll.name} exports {sorted(actual)}; expected {sorted(expected)}")
        imports = kernel_imports(dumpbin, dll)
        print(f"{dll.name}: {len(imports)} smedley_kernel.dll imports")
        for imported in imports:
            print(f"  {imported}")
        if imports:
            failures.append(f"{dll.name} imports smedley_kernel.dll: {imports}")

    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
