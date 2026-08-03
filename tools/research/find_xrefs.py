#!/usr/bin/env python3
"""Find direct x86 references to ASCII strings in a PE32 executable."""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import asdict, dataclass
from pathlib import Path


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_address: int
    raw_size: int

    def contains_offset(self, offset: int) -> bool:
        return self.raw_address <= offset < self.raw_address + self.raw_size

    def offset_to_rva(self, offset: int) -> int:
        return self.virtual_address + offset - self.raw_address


@dataclass(frozen=True)
class Reference:
    query: str
    string_rva: int
    string_va: int
    reference_rva: int
    reference_va: int
    candidate_function_rva: int | None


def parse_image(data: bytes) -> tuple[int, list[Section]]:
    pe = u32(data, 0x3C)
    if data[pe : pe + 4] != b"PE\0\0" or u16(data, pe + 24) != 0x10B:
        raise ValueError("expected a PE32 executable")

    image_base = u32(data, pe + 52)
    section_count = u16(data, pe + 6)
    optional_size = u16(data, pe + 20)
    table = pe + 24 + optional_size
    sections = []
    for index in range(section_count):
        offset = table + index * 40
        name = data[offset : offset + 8].rstrip(b"\0").decode("ascii")
        sections.append(
            Section(
                name=name,
                virtual_size=u32(data, offset + 8),
                virtual_address=u32(data, offset + 12),
                raw_size=u32(data, offset + 16),
                raw_address=u32(data, offset + 20),
            )
        )
    return image_base, sections


def find_all(data: bytes, needle: bytes, start: int = 0, end: int | None = None):
    position = start
    while True:
        position = data.find(needle, position, len(data) if end is None else end)
        if position < 0:
            return
        yield position
        position += 1


def containing_section(sections: list[Section], offset: int) -> Section | None:
    return next((section for section in sections if section.contains_offset(offset)), None)


def find_references(data: bytes, queries: list[str]) -> list[Reference]:
    image_base, sections = parse_image(data)
    text = next(section for section in sections if section.name == ".text")
    results = []

    for query in queries:
        encoded = query.encode("ascii") + b"\0"
        for string_offset in find_all(data, encoded):
            section = containing_section(sections, string_offset)
            if section is None:
                continue
            string_rva = section.offset_to_rva(string_offset)
            string_va = image_base + string_rva
            pointer = struct.pack("<I", string_va)
            for reference_offset in find_all(
                data,
                pointer,
                text.raw_address,
                text.raw_address + text.raw_size,
            ):
                reference_rva = text.offset_to_rva(reference_offset)
                prologue_offset = data.rfind(
                    b"\x55\x8b\xec",
                    max(text.raw_address, reference_offset - 0x10000),
                    reference_offset,
                )
                results.append(
                    Reference(
                        query=query,
                        string_rva=string_rva,
                        string_va=string_va,
                        reference_rva=reference_rva,
                        reference_va=image_base + reference_rva,
                        candidate_function_rva=(text.offset_to_rva(prologue_offset) if prologue_offset >= 0 else None),
                    )
                )
    return results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--string", action="append", required=True, dest="queries")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    references = find_references(args.executable.read_bytes(), args.queries)
    if args.json:
        print(json.dumps([asdict(reference) for reference in references], indent=2))
        return 0

    for reference in references:
        candidate = (
            f", candidate function RVA 0x{reference.candidate_function_rva:08x}"
            if reference.candidate_function_rva is not None
            else ""
        )
        print(
            f"{reference.query!r}: string RVA 0x{reference.string_rva:08x}, "
            f"xref RVA 0x{reference.reference_rva:08x} "
            f"(VA 0x{reference.reference_va:08x}){candidate}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
