#!/usr/bin/env python3
"""Reject raw engine access from the interest_bug_fix production layer."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


RAW_HEADER_PREFIXES = (
    "smedley/v2/",
    "smedley/clausewitz/",
    "smedley/std/",
)
GAME_OBJECT_VOID_POINTER = re.compile(
    r"\b(?:const\s+)?void\s*\*\s*(?:country|province|pop|factory|game_state|controller|object|signal)\b"
)
MEMORY_MAP = re.compile(r"\b(?:smedley\s*::\s*)?memory\s*::\s*Map\b")
GIVE_MONEY_SYMBOL = re.compile(r"\b(?:\w*GiveMoney\w*|\w*give_money\w*)\b")
HEX_LITERAL = re.compile(r"\b0x[0-9a-fA-F]+\b")
ENGINE_LAYOUT_LITERAL = re.compile(r"\b\w*(?:rva|offset|vtable)\w*\s*=\s*[^;]*\b0x[0-9a-fA-F]+\b")
NATIVE_ENGINE_CALL = re.compile(r"\b__thiscall\b|\b__declspec\s*\(\s*naked\s*\)")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
RESOLVER_CONTEXT = re.compile(
    r"^\s*(?:static\s+)?(?:[\w:]+\s+)?(?:CountryRef|ProvinceRef)\s+Resolve\w*\s*\(\s*"
    r"const\s+void\s*\*\s*context\s*,[^)]*\)\s*(?:\{|;)?\s*$"
)


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    message: str


def strip_comments(source: str) -> str:
    """Replace C++ comments with spaces while preserving lines and literals."""
    result: list[str] = []
    index = 0
    in_block_comment = False
    quote = ""

    while index < len(source):
        character = source[index]
        next_character = source[index + 1] if index + 1 < len(source) else ""

        if in_block_comment:
            if character == "*" and next_character == "/":
                result.extend("  ")
                index += 2
                in_block_comment = False
            else:
                result.append("\n" if character == "\n" else " ")
                index += 1
            continue

        if quote:
            result.append(character)
            if character == "\\" and next_character:
                result.append(next_character)
                index += 2
            elif character == quote:
                quote = ""
                index += 1
            else:
                index += 1
            continue

        if character in {'"', "'"}:
            quote = character
            result.append(character)
            index += 1
        elif character == "/" and next_character == "/":
            newline = source.find("\n", index)
            if newline == -1:
                result.extend(" " * (len(source) - index))
                break
            result.extend(" " * (newline - index))
            result.append("\n")
            index = newline + 1
        elif character == "/" and next_character == "*":
            result.extend("  ")
            index += 2
            in_block_comment = True
        else:
            result.append(character)
            index += 1

    return "".join(result)


def strip_literals(source: str) -> str:
    """Replace quoted C++ literals with spaces while preserving line numbers."""
    result: list[str] = []
    index = 0
    quote = ""

    while index < len(source):
        character = source[index]
        next_character = source[index + 1] if index + 1 < len(source) else ""
        if quote:
            result.append("\n" if character == "\n" else " ")
            if character == "\\" and next_character:
                result.append("\n" if next_character == "\n" else " ")
                index += 2
            elif character == quote:
                quote = ""
                index += 1
            else:
                index += 1
        elif character in {'"', "'"}:
            quote = character
            result.append(" ")
            index += 1
        else:
            result.append(character)
            index += 1

    return "".join(result)


def production_sources(root: Path) -> list[Path]:
    source_root = root / "plugins"
    return sorted(
        (
            path
            for path in source_root.rglob("*")
            if path.suffix in {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
            and "tests" not in path.relative_to(source_root).parts
        ),
        key=lambda path: path.as_posix(),
    )


def audit(root: Path, raw_adapters: set[Path] | None = None) -> list[Finding]:
    """Return raw engine access outside explicitly registered plugin adapters."""
    allowed = {path.resolve() for path in raw_adapters or set()}
    findings: list[Finding] = []
    for path in production_sources(root):
        if path.resolve() in allowed:
            continue
        uncommented = strip_comments(path.read_text(encoding="utf-8"))
        code = strip_literals(uncommented)
        for line_number, line in enumerate(uncommented.splitlines(), start=1):
            include = INCLUDE.match(line)
            if include and (
                include.group(1) == "smedley/memory.hpp" or include.group(1).startswith(RAW_HEADER_PREFIXES)
            ):
                findings.append(Finding(path, line_number, f"raw engine header included: {include.group(1)}"))

        for line_number, line in enumerate(code.splitlines(), start=1):
            if MEMORY_MAP.search(line):
                findings.append(Finding(path, line_number, "memory::Map is a game runtime implementation detail"))
            if any(int(match.group(), 16) == 0x0055A5F0 for match in HEX_LITERAL.finditer(line)):
                findings.append(Finding(path, line_number, "CPop::GiveMoney RVA belongs in smedley_game_runtime"))
            if ENGINE_LAYOUT_LITERAL.search(line):
                findings.append(
                    Finding(path, line_number, "engine RVAs and field offsets require a registered raw adapter")
                )
            if NATIVE_ENGINE_CALL.search(line):
                findings.append(Finding(path, line_number, "native engine calls require a registered raw adapter"))
            if GIVE_MONEY_SYMBOL.search(line):
                findings.append(Finding(path, line_number, "local GiveMoney wrappers belong in smedley_game_runtime"))
            if GAME_OBJECT_VOID_POINTER.search(line) and not RESOLVER_CONTEXT.match(line):
                findings.append(
                    Finding(path, line_number, "use typed game_state References, not void game-object pointers")
                )

    return sorted(findings, key=lambda finding: (finding.path.as_posix(), finding.line, finding.message))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).parents[1])
    parser.add_argument("--allow-raw", action="append", type=Path, default=[])
    args = parser.parse_args()

    root = args.root.resolve()
    raw_adapters = {root / path for path in args.allow_raw}
    findings = audit(root, raw_adapters)
    for finding in findings:
        print(f"{finding.path.relative_to(root).as_posix()}:{finding.line}: {finding.message}", file=sys.stderr)
    if findings:
        return 1
    print("OK: first-party plugin raw access is confined to registered adapters")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
