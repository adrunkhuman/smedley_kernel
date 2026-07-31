#!/usr/bin/env python3
"""Validate a Smedley mapping catalog against a Victoria 2 executable."""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import tomllib
from pathlib import Path


def read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def rva_to_offset(data: bytes, rva: int) -> int:
    pe_offset = read_u32(data, 0x3C)
    section_count = read_u16(data, pe_offset + 6)
    optional_size = read_u16(data, pe_offset + 20)
    section_table = pe_offset + 24 + optional_size

    for index in range(section_count):
        section = section_table + index * 40
        virtual_size = read_u32(data, section + 8)
        virtual_address = read_u32(data, section + 12)
        raw_size = read_u32(data, section + 16)
        raw_address = read_u32(data, section + 20)
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            offset = raw_address + rva - virtual_address
            if offset >= len(data):
                raise ValueError(f"RVA 0x{rva:08x} is in an uninitialized section")
            return offset
    raise ValueError(f"RVA 0x{rva:08x} is outside all PE sections")


def parse_signature(value: str) -> list[int | None]:
    return [None if byte == "??" else int(byte, 16) for byte in value.split()]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument(
        "--catalog",
        type=Path,
        default=Path(__file__).parents[1] / "mappings" / "v2game-3.04.toml",
    )
    args = parser.parse_args()

    data = args.executable.read_bytes()
    with args.catalog.open("rb") as catalog_file:
        catalog = tomllib.load(catalog_file)

    expected = catalog["executable"]
    failures: list[str] = []
    digest = hashlib.sha256(data).hexdigest()
    if digest != expected["sha256"]:
        failures.append(f"SHA-256 mismatch: expected {expected['sha256']}, got {digest}")
    if len(data) != expected["size"]:
        failures.append(f"size mismatch: expected {expected['size']}, got {len(data)}")

    checked = 0
    for symbol in catalog.get("symbols", []):
        if "signature" not in symbol:
            continue
        signature = parse_signature(symbol["signature"])
        try:
            offset = rva_to_offset(data, symbol["rva"])
        except ValueError as error:
            failures.append(f"{symbol['name']}: {error}")
            continue
        actual = data[offset : offset + len(signature)]
        if len(actual) != len(signature) or any(
            expected_byte is not None and expected_byte != actual_byte
            for expected_byte, actual_byte in zip(signature, actual, strict=True)
        ):
            failures.append(
                f"{symbol['name']} at RVA 0x{symbol['rva']:08x}: expected {symbol['signature']}, got {actual.hex(' ')}"
            )
        checked += 1

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print(f"OK: executable identity and {checked} signatures match {args.catalog}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
