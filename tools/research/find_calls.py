#!/usr/bin/env python3
"""Find direct x86 CALL instructions targeting a PE32 RVA."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from find_xrefs import parse_image


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--target-rva", type=lambda value: int(value, 0), required=True)
    args = parser.parse_args()

    data = args.executable.read_bytes()
    image_base, sections = parse_image(data)
    text = next(section for section in sections if section.name == ".text")
    start = text.raw_address
    end = start + text.raw_size - 4

    for offset in range(start, end):
        if data[offset] != 0xE8:
            continue
        call_rva = text.offset_to_rva(offset)
        displacement = struct.unpack_from("<i", data, offset + 1)[0]
        target_rva = call_rva + 5 + displacement
        if target_rva != args.target_rva:
            continue
        prologue_offset = data.rfind(b"\x55\x8b\xec", max(start, offset - 0x10000), offset)
        candidate = text.offset_to_rva(prologue_offset) if prologue_offset >= 0 else None
        suffix = f", candidate caller RVA 0x{candidate:08x}" if candidate is not None else ""
        print(f"CALL RVA 0x{call_rva:08x} (VA 0x{image_base + call_rva:08x}) -> 0x{target_rva:08x}{suffix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
