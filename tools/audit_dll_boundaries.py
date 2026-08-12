#!/usr/bin/env python3
"""Audit production DLL exports and report transitional kernel C++ imports."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


EXPECTED_EXPORTS = {
    "campaign_runner": {"CreatePlugin"},
    "interest_bug_fix": {"CreatePlugin"},
    "scripting": {"CreatePlugin"},
    "telemetry": {
        "CreatePlugin",
        "SmedleyTelemetryDrainV1",
        "SmedleyTelemetryEmitReliableV1",
        "SmedleyTelemetryEmitV1",
    },
}
REQUIRED_KERNEL_EXPORTS = {
    "LoadPlugins",
    "LoadPluginsThread",
    "SmedleyGetCampaignControlApiV1",
    "SmedleyGetEventApiV1",
    "SmedleyGetEventServicesApiV1",
    "SmedleyGetLoggingApiV1",
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
    return [name.strip() for name in IMPORT_LINE.findall(section) if name.strip().startswith("?")]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--linker", required=True, type=Path)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--write-baseline", action="store_true")
    for plugin in EXPECTED_EXPORTS:
        parser.add_argument(f"--{plugin.replace('_', '-')}", required=True, type=Path)
    args = parser.parse_args()
    dumpbin = args.linker.with_name("dumpbin.exe")
    if not dumpbin.is_file():
        print(f"dumpbin.exe not found beside linker: {args.linker}", file=sys.stderr)
        return 1

    failures: list[str] = []
    current_imports: dict[str, list[str]] = {}
    kernel_exports = exports(dumpbin, args.kernel)
    missing_kernel = REQUIRED_KERNEL_EXPORTS - kernel_exports
    if missing_kernel:
        failures.append(f"smedley_kernel.dll missing exports: {sorted(missing_kernel)}")

    for plugin, expected in EXPECTED_EXPORTS.items():
        dll = getattr(args, plugin)
        actual = exports(dumpbin, dll)
        if actual != expected:
            failures.append(f"{dll.name} exports {sorted(actual)}; expected {sorted(expected)}")
        imports = kernel_imports(dumpbin, dll)
        current_imports[plugin] = sorted(imports)
        print(f"{dll.name}: {len(imports)} transitional smedley_kernel.dll imports")
        for imported in imports:
            print(f"  {imported}")

    if args.write_baseline:
        args.baseline.write_text(json.dumps(current_imports, indent=2) + "\n", encoding="utf-8", newline="\n")
    else:
        allowed_imports = json.loads(args.baseline.read_text(encoding="utf-8"))
        for plugin, imports in current_imports.items():
            unexpected = set(imports) - set(allowed_imports[plugin])
            if unexpected:
                failures.append(f"{plugin}.dll has new kernel imports: {sorted(unexpected)}")

    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
