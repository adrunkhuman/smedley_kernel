#!/usr/bin/env python3
"""Produce a deterministic static workload inventory for Clausewitz scripts.

Every aggregate under ``workload_leads`` is a static lead, not a performance
measurement. The tool does not execute script or estimate engine cost.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


EVENT_KEYS = frozenset({"country_event", "province_event", "event", "news_event"})
ITERATOR_SCOPE_KEY = re.compile(r"^(?:any|all|every|random)_[A-Za-z0-9_]+$")
FLAG_READ_KEY = re.compile(r"^has_[A-Za-z0-9_]*flag$")
FLAG_WRITE_KEY = re.compile(r"^(?:set|clear|clr|remove)_[A-Za-z0-9_]*flag$")
MODIFIER_MUTATION_KEY = re.compile(r"^(?:add|remove)_[A-Za-z0-9_]*modifier$")
MUTATION_KEY = re.compile(r"^(?:set|add|remove|clear|clr|change|create|delete|destroy|activate|deactivate|kill|give)_")
DELAY_KEYS = {"days", "months", "years", "hours"}
SCRIPT_SUFFIXES = {".txt"}


@dataclass(frozen=True)
class Token:
    """A Clausewitz token and its source line."""

    value: str
    line: int
    quoted: bool = False


@dataclass
class Node:
    """A Clausewitz assignment with either a scalar value or a child block."""

    key: str
    line: int
    value: str | None = None
    children: list[Node] = field(default_factory=list)


def tokenize(source: str) -> list[Token]:
    """Tokenize comments, quoted values, assignments, and blocks with line data."""
    tokens: list[Token] = []
    index = 0
    line = 1
    while index < len(source):
        character = source[index]
        if character.isspace():
            line += character == "\n"
            index += 1
        elif character == "#":
            newline = source.find("\n", index)
            if newline == -1:
                break
            line += 1
            index = newline + 1
        elif character in "={}":
            tokens.append(Token(character, line))
            index += 1
        elif character == '"':
            start_line = line
            index += 1
            value: list[str] = []
            while index < len(source) and source[index] != '"':
                if source[index] == "\\" and index + 1 < len(source):
                    value.append(source[index + 1])
                    line += source[index + 1] == "\n"
                    index += 2
                    continue
                value.append(source[index])
                line += source[index] == "\n"
                index += 1
            if index < len(source):
                index += 1
            tokens.append(Token("".join(value), start_line, quoted=True))
        else:
            start = index
            while index < len(source) and not source[index].isspace() and source[index] not in "#={}":
                index += 1
            tokens.append(Token(source[start:index], line))
    return tokens


def parse(source: str) -> Node:
    """Parse the assignment-and-block subset used by Victoria II scripts."""
    tokens = tokenize(source)
    position = 0

    def parse_block() -> list[Node]:
        nonlocal position
        nodes: list[Node] = []
        while position < len(tokens) and (tokens[position].value != "}" or tokens[position].quoted):
            key = tokens[position]
            position += 1
            if position >= len(tokens) or tokens[position].value != "=" or tokens[position].quoted:
                continue
            position += 1
            if position >= len(tokens):
                break
            value = tokens[position]
            position += 1
            if value.value == "{" and not value.quoted:
                children = parse_block()
                if position < len(tokens) and tokens[position].value == "}" and not tokens[position].quoted:
                    position += 1
                nodes.append(Node(key.value, key.line, children=children))
            elif value.value != "}" or value.quoted:
                nodes.append(Node(key.value, key.line, value=value.value))
        return nodes

    return Node("", 1, children=parse_block())


def walk(node: Node) -> list[Node]:
    """Return descendants in source order."""
    result: list[Node] = []
    for child in node.children:
        result.append(child)
        result.extend(walk(child))
    return result


def scalar_children(node: Node) -> dict[str, str]:
    """Return direct scalar assignments, preserving the first assignment per key."""
    values: dict[str, str] = {}
    for child in node.children:
        if child.value is not None:
            values.setdefault(child.key, child.value)
    return values


def source(path: Path, line: int) -> dict[str, int | str]:
    """Create the common machine-readable source location."""
    return {"line": line, "path": path.as_posix()}


def report_path(path: Path, root: Path) -> Path:
    """Use root-relative paths when possible, retaining usable paths for external inputs."""
    try:
        return path.resolve().relative_to(root.resolve())
    except ValueError:
        return path.resolve()


def target_name(node: Node) -> str | None:
    """Extract the direct Clausewitz value used by flag and modifier directives."""
    if node.value is not None:
        return node.value
    for child in node.children:
        if child.key in {"which", "flag", "name"} and child.value is not None:
            return child.value
    return None


def is_event_definition(node: Node, parent: Node) -> bool:
    """Recognize top-level event blocks without relying on any event identifiers."""
    return bool(parent.key == "" and node.children and node.key in EVENT_KEYS and "id" in scalar_children(node))


def script_files(paths: list[Path]) -> list[Path]:
    """Return explicitly named files and Clausewitz text files below named directories."""
    files: set[Path] = set()
    for path in paths:
        if path.is_file():
            files.add(path.resolve())
        elif path.is_dir():
            files.update(
                candidate.resolve() for candidate in path.rglob("*") if candidate.suffix.lower() in SCRIPT_SUFFIXES
            )
        else:
            raise FileNotFoundError(path)
    return sorted(files, key=lambda path: path.as_posix())


def inventory(paths: list[Path], root: Path) -> dict[str, Any]:
    """Build a stable static inventory for the provided script files or directories."""
    event_definitions: list[dict[str, object]] = []
    scheduling_edges: list[dict[str, object]] = []
    mtth_cadence: list[dict[str, object]] = []
    flag_reads: list[dict[str, object]] = []
    flag_writes: list[dict[str, object]] = []
    modifier_mutations: list[dict[str, object]] = []
    block_keys: Counter[str] = Counter()
    iterator_scope_keys: Counter[str] = Counter()
    random_selector_keys: Counter[str] = Counter()
    mutation_keys: Counter[str] = Counter()
    random_list_blocks = 0
    random_list_entries = 0

    for file_path in script_files(paths):
        relative_path = report_path(file_path, root)
        document = parse(file_path.read_text(encoding="utf-8", errors="replace"))
        definitions = [node for node in document.children if is_event_definition(node, document)]
        definition_data = {id(node): scalar_children(node) for node in definitions}

        for node in walk(document):
            if node.children:
                block_keys[node.key] += 1
            if node.children and node.key != "random_list" and ITERATOR_SCOPE_KEY.match(node.key):
                iterator_scope_keys[node.key] += 1
            if node.children and node.key == "random_list":
                random_list_blocks += 1
                random_list_entries += len(node.children)
            elif node.children and (node.key == "random" or node.key.startswith("random_")):
                random_selector_keys[node.key] += 1
            if MUTATION_KEY.match(node.key):
                mutation_keys[node.key] += 1
            observation: dict[str, object] = {"key": node.key, "source": source(relative_path, node.line)}
            name = target_name(node)
            if name is not None:
                observation["target"] = name
            if FLAG_READ_KEY.match(node.key):
                flag_reads.append(observation)
            if FLAG_WRITE_KEY.match(node.key):
                flag_writes.append(observation)
            if MODIFIER_MUTATION_KEY.match(node.key):
                modifier_mutations.append(observation)

        for definition in definitions:
            metadata = definition_data[id(definition)]
            event: dict[str, object] = {
                "id": metadata["id"],
                "kind": definition.key,
                "source": source(relative_path, definition.line),
            }
            event_definitions.append(event)
            for child in definition.children:
                if child.key != "mean_time_to_happen" or not child.children:
                    continue
                cadence = {
                    item.key: item.value for item in child.children if item.key in DELAY_KEYS and item.value is not None
                }
                mtth_cadence.append(
                    {
                        "cadence": cadence,
                        "event_id": metadata["id"],
                        "event_kind": definition.key,
                        "source": source(relative_path, child.line),
                    }
                )
            for node in walk(definition):
                if node.key == definition.key and node is definition:
                    continue
                target_id = node.value if node.key in EVENT_KEYS and node.value is not None else None
                if node.children and node.key in EVENT_KEYS:
                    target_id = scalar_children(node).get("id")
                if target_id is None:
                    continue
                delays = {
                    item.key: item.value for item in node.children if item.key in DELAY_KEYS and item.value is not None
                }
                edge: dict[str, object] = {
                    "command": node.key,
                    "source_event_id": metadata["id"],
                    "source_event_kind": definition.key,
                    "source": source(relative_path, node.line),
                    "target_event_id": target_id,
                }
                if delays:
                    edge["delays"] = delays
                scheduling_edges.append(edge)

    sort_key = lambda item: (item["source"]["path"], item["source"]["line"], item.get("id", item.get("key", "")))
    for observations in (
        event_definitions,
        scheduling_edges,
        mtth_cadence,
        flag_reads,
        flag_writes,
        modifier_mutations,
    ):
        observations.sort(key=sort_key)
    return {
        "schema_version": 1,
        "static_counts_are_leads_not_performance_measurements": True,
        "event_definitions": event_definitions,
        "scheduling_edges": scheduling_edges,
        "mtth_cadence": mtth_cadence,
        "flag_reads": flag_reads,
        "flag_writes": flag_writes,
        "modifier_mutations": modifier_mutations,
        "workload_leads": {
            "block_key_leads": dict(sorted(block_keys.items())),
            "event_definition_lead": len(event_definitions),
            "flag_read_lead": len(flag_reads),
            "flag_write_lead": len(flag_writes),
            "iterator_scope_key_leads": dict(sorted(iterator_scope_keys.items())),
            "modifier_mutation_lead": len(modifier_mutations),
            "mtth_cadence_lead": len(mtth_cadence),
            "mutation_key_leads": dict(sorted(mutation_keys.items())),
            "random_list_leads": {"blocks": random_list_blocks, "weighted_entries": random_list_entries},
            "random_selector_key_leads": dict(sorted(random_selector_keys.items())),
            "scheduling_edge_lead": len(scheduling_edges),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path, help="Clausewitz files or directories to inventory")
    parser.add_argument("--root", type=Path, default=Path.cwd(), help="base used for reported source paths")
    parser.add_argument("--output", type=Path, help="write JSON to this file instead of standard output")
    args = parser.parse_args()

    files = script_files(args.paths)
    output = args.output.resolve() if args.output else None
    if output is not None and output in files:
        parser.error(f"output path is also an input script: {output}")

    result = inventory(files, args.root.resolve())
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if output:
        output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
