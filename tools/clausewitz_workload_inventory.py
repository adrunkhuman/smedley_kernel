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
VARIABLE_READ_KEYS = frozenset({"check_variable", "is_variable_equal"})
VARIABLE_WRITE_KEYS = frozenset(
    {
        "change_variable",
        "clear_variable",
        "clr_variable",
        "divide_variable",
        "multiply_variable",
        "remove_variable",
        "set_variable",
        "subtract_variable",
    }
)
OWNERSHIP_MUTATION_KEYS = frozenset({"annex_to", "change_tag", "inherit", "release_vassal", "secede_province"})
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
    end_line: int
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

    def parse_block() -> tuple[list[Node], int]:
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
                children, end_line = parse_block()
                if position < len(tokens) and tokens[position].value == "}" and not tokens[position].quoted:
                    end_line = tokens[position].line
                    position += 1
                nodes.append(Node(key.value, key.line, end_line, children=children))
            elif value.value != "}" or value.quoted:
                nodes.append(Node(key.value, key.line, value.line, value=value.value))
        end_line = tokens[position].line if position < len(tokens) else (tokens[-1].line if tokens else 1)
        return nodes, end_line

    children, end_line = parse_block()
    return Node("", 1, end_line, children=children)


def walk(node: Node) -> list[Node]:
    """Return descendants in source order."""
    result: list[Node] = []
    for child in node.children:
        result.append(child)
        result.extend(walk(child))
    return result


def walk_with_ancestors(node: Node, ancestors: tuple[Node, ...] = ()) -> list[tuple[Node, tuple[Node, ...]]]:
    """Return descendants with their block ancestors in source order."""
    result: list[tuple[Node, tuple[Node, ...]]] = []
    for child in node.children:
        result.append((child, ancestors))
        result.extend(walk_with_ancestors(child, (*ancestors, child)))
    return result


def is_iterator_scope(node: Node) -> bool:
    """Return whether a block is an entity iterator rather than a weighted choice."""
    return bool(node.children and node.key != "random_list" and ITERATOR_SCOPE_KEY.match(node.key))


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


def source_span(path: Path, node: Node) -> dict[str, int | str]:
    """Create a source range for semantic candidate records."""
    return {"end_line": node.end_line, "path": path.as_posix(), "start_line": node.line}


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


def contextual_observation(
    node: Node, ancestors: tuple[Node, ...], path: Path, *, target: str | None = None
) -> dict[str, object]:
    """Describe a command and the blocks that contain it."""
    observation: dict[str, object] = {
        "block_path": [ancestor.key for ancestor in ancestors],
        "command": node.key,
        "source": source_span(path, node),
    }
    if target is not None:
        observation["target"] = target
    return observation


def province_modifier_workflow(definition: Node, path: Path) -> dict[str, object] | None:
    """Summarize source semantics for an event that reads or writes province modifiers."""
    metadata = scalar_children(definition)
    descendants = walk_with_ancestors(definition)
    modifier_reads: list[dict[str, object]] = []
    modifier_writes: list[dict[str, object]] = []
    flag_reads: set[str] = set()
    flag_writes: set[str] = set()
    variable_reads: set[str] = set()
    variable_writes: set[str] = set()
    iterator_scopes: set[str] = set()
    random_selectors: set[str] = set()
    ownership_mutations: set[str] = set()

    for node, ancestors in descendants:
        target = target_name(node)
        if node.key == "has_province_modifier":
            modifier_reads.append(contextual_observation(node, ancestors, path, target=target))
        elif node.key in {"add_province_modifier", "remove_province_modifier"}:
            observation = contextual_observation(node, ancestors, path, target=target)
            duration = scalar_children(node).get("duration")
            if duration is not None:
                observation["duration"] = duration
            modifier_writes.append(observation)
        if FLAG_READ_KEY.match(node.key):
            flag_reads.add(target or "<dynamic>")
        if FLAG_WRITE_KEY.match(node.key):
            flag_writes.add(target or "<dynamic>")
        if node.key in VARIABLE_READ_KEYS:
            variable_reads.add(target or "<dynamic>")
        if node.key in VARIABLE_WRITE_KEYS:
            variable_writes.add(target or "<dynamic>")
        if is_iterator_scope(node):
            iterator_scopes.add(node.key)
        if node.children and (node.key == "random" or node.key.startswith("random_")):
            random_selectors.add(node.key)
        if node.key in OWNERSHIP_MUTATION_KEYS:
            ownership_mutations.add(node.key)

    if not modifier_reads and not modifier_writes:
        return None

    cadence: dict[str, str] = {}
    for child in definition.children:
        if child.key == "mean_time_to_happen" and child.children:
            cadence = {
                item.key: item.value for item in child.children if item.key in DELAY_KEYS and item.value is not None
            }
            break

    risk_flags: set[str] = set()
    if random_selectors:
        risk_flags.add("random_selection")
    if ownership_mutations:
        risk_flags.add("ownership_mutation")
    if cadence:
        risk_flags.add("mtth_polling")
    if any(
        observation.get("command") == "add_province_modifier" and observation.get("duration") not in {None, "-1"}
        for observation in modifier_writes
    ):
        risk_flags.add("finite_modifier_duration")
    if any(
        isinstance(observation["block_path"], list) and "state_scope" in observation["block_path"]
        for observation in modifier_writes
    ):
        risk_flags.add("state_scope_write")

    workflow: dict[str, object] = {
        "event_id": metadata["id"],
        "event_kind": definition.key,
        "event_source": source_span(path, definition),
        "flag_reads": sorted(flag_reads),
        "flag_writes": sorted(flag_writes),
        "iterator_scopes": sorted(iterator_scopes),
        "modifier_reads": modifier_reads,
        "modifier_writes": modifier_writes,
        "ownership_mutations": sorted(ownership_mutations),
        "random_selectors": sorted(random_selectors),
        "risk_flags": sorted(risk_flags),
        "variable_reads": sorted(variable_reads),
        "variable_writes": sorted(variable_writes),
    }
    if cadence:
        workflow["mtth_cadence"] = cadence
    for key in ("fire_only_once", "is_triggered_only"):
        if key in metadata:
            workflow[key] = metadata[key]
    return workflow


def province_modifier_scope_blocks(definition: Node, path: Path) -> list[dict[str, object]]:
    """Describe outer iterator blocks that contain province-modifier writes."""
    metadata = scalar_children(definition)
    cadence = any(child.key == "mean_time_to_happen" and child.children for child in definition.children)
    blocks: list[dict[str, object]] = []

    for scope_node, scope_ancestors in walk_with_ancestors(definition):
        if not is_iterator_scope(scope_node):
            continue
        if any(is_iterator_scope(ancestor) for ancestor in scope_ancestors):
            continue

        descendants = walk_with_ancestors(scope_node)
        if not any(node.key in {"add_province_modifier", "remove_province_modifier"} for node, _ in descendants):
            continue

        modifier_reads: list[dict[str, object]] = []
        modifier_writes: list[dict[str, object]] = []
        flag_reads: set[str] = set()
        flag_writes: set[str] = set()
        variable_reads: set[str] = set()
        variable_writes: set[str] = set()
        random_selectors: set[str] = set()
        ownership_mutations: set[str] = set()
        predicate_keys: set[str] = set()

        if scope_node.key == "random" or scope_node.key.startswith("random_"):
            random_selectors.add(scope_node.key)
        for ancestor in scope_ancestors:
            if ancestor.children and (ancestor.key == "random" or ancestor.key.startswith("random_")):
                random_selectors.add(ancestor.key)
        for node, local_ancestors in descendants:
            ancestors = (*scope_ancestors, scope_node, *local_ancestors)
            target = target_name(node)
            if node.key == "has_province_modifier":
                modifier_reads.append(contextual_observation(node, ancestors, path, target=target))
            elif node.key in {"add_province_modifier", "remove_province_modifier"}:
                observation = contextual_observation(node, ancestors, path, target=target)
                duration = scalar_children(node).get("duration")
                if duration is not None:
                    observation["duration"] = duration
                modifier_writes.append(observation)
            if FLAG_READ_KEY.match(node.key):
                flag_reads.add(target or "<dynamic>")
            if FLAG_WRITE_KEY.match(node.key):
                flag_writes.add(target or "<dynamic>")
            if node.key in VARIABLE_READ_KEYS:
                variable_reads.add(target or "<dynamic>")
            if node.key in VARIABLE_WRITE_KEYS:
                variable_writes.add(target or "<dynamic>")
            if node.children and (node.key == "random" or node.key.startswith("random_")):
                random_selectors.add(node.key)
            if node.key in OWNERSHIP_MUTATION_KEYS:
                ownership_mutations.add(node.key)
            if any(ancestor.key == "limit" for ancestor in local_ancestors):
                predicate_keys.add(node.key)

        risk_flags: set[str] = set()
        if cadence:
            risk_flags.add("mtth_polling")
        if random_selectors:
            risk_flags.add("random_selection")
        if ownership_mutations:
            risk_flags.add("ownership_mutation")
        if any(
            observation.get("command") == "add_province_modifier" and observation.get("duration") not in {None, "-1"}
            for observation in modifier_writes
        ):
            risk_flags.add("finite_modifier_duration")
        if any(
            isinstance(observation["block_path"], list) and "state_scope" in observation["block_path"]
            for observation in modifier_writes
        ):
            risk_flags.add("state_scope_write")

        blocks.append(
            {
                "block_path": [ancestor.key for ancestor in scope_ancestors],
                "event_id": metadata["id"],
                "event_kind": definition.key,
                "flag_reads": sorted(flag_reads),
                "flag_writes": sorted(flag_writes),
                "modifier_reads": modifier_reads,
                "modifier_writes": modifier_writes,
                "ownership_mutations": sorted(ownership_mutations),
                "predicate_keys": sorted(predicate_keys),
                "random_selectors": sorted(random_selectors),
                "risk_flags": sorted(risk_flags),
                "scope": scope_node.key,
                "source": source_span(path, scope_node),
                "variable_reads": sorted(variable_reads),
                "variable_writes": sorted(variable_writes),
            }
        )
    return blocks


def explicit_schedule_cycle_ids(scheduling_edges: list[dict[str, object]]) -> set[str]:
    """Return event IDs that participate in a statically resolved scheduling cycle."""
    adjacency: dict[str, set[str]] = {}
    for edge in scheduling_edges:
        adjacency.setdefault(str(edge["source_event_id"]), set()).add(str(edge["target_event_id"]))

    cycle_ids: set[str] = set()
    for start in adjacency:
        pending = list(adjacency[start])
        visited: set[str] = set()
        while pending:
            current = pending.pop()
            if current == start:
                cycle_ids.add(start)
                break
            if current in visited:
                continue
            visited.add(current)
            pending.extend(adjacency.get(current, ()))
    return cycle_ids


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
    province_modifier_workflows: list[dict[str, object]] = []
    province_modifier_scope_candidates: list[dict[str, object]] = []
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
            if is_iterator_scope(node):
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
            workflow = province_modifier_workflow(definition, relative_path)
            if workflow is not None:
                province_modifier_workflows.append(workflow)
            province_modifier_scope_candidates.extend(province_modifier_scope_blocks(definition, relative_path))
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
    incoming_schedules: dict[str, list[dict[str, object]]] = {}
    for edge in scheduling_edges:
        incoming_schedules.setdefault(str(edge["target_event_id"]), []).append(edge)
    schedule_cycle_ids = explicit_schedule_cycle_ids(scheduling_edges)
    for record in (*province_modifier_workflows, *province_modifier_scope_candidates):
        event_id = str(record["event_id"])
        incoming = incoming_schedules.get(event_id, [])
        if incoming:
            record["incoming_schedules"] = incoming
        recurrence: list[str] = []
        if event_id in schedule_cycle_ids:
            recurrence.append("explicit_schedule_cycle")
        risk_flags = record["risk_flags"]
        if isinstance(risk_flags, list) and "mtth_polling" in risk_flags:
            recurrence.append("engine_polled_mtth")
        record["recurrence"] = sorted(recurrence)
    province_modifier_workflows.sort(
        key=lambda item: (
            item["event_source"]["path"],
            item["event_source"]["start_line"],
            item["event_id"],
        )
    )
    province_modifier_scope_candidates.sort(
        key=lambda item: (item["source"]["path"], item["source"]["start_line"], item["event_id"])
    )
    return {
        "schema_version": 2,
        "static_counts_are_leads_not_performance_measurements": True,
        "event_definitions": event_definitions,
        "scheduling_edges": scheduling_edges,
        "mtth_cadence": mtth_cadence,
        "flag_reads": flag_reads,
        "flag_writes": flag_writes,
        "modifier_mutations": modifier_mutations,
        "province_modifier_workflows": province_modifier_workflows,
        "province_modifier_scope_candidates": province_modifier_scope_candidates,
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
