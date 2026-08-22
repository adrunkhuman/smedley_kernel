"""Tests for the static Clausewitz workload inventory."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parents[1]))
import clausewitz_workload_inventory


SCRIPT = Path(clausewitz_workload_inventory.__file__)


class ClausewitzWorkloadInventoryTest(unittest.TestCase):
    def test_inventories_events_and_static_workload_leads(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            script = root / "events" / "sample.txt"
            script.parent.mkdir()
            script.write_text(
                """namespace = sample
country_event = {
    id = sample.1
    title = "{"
    desc = "}"
    picture = "="
    mean_time_to_happen = { months = 6 }
    trigger = {
        has_country_flag = read_flag
        any_owned_province = { random = { always = yes } }
    }
    option = {
        set_country_flag = written_flag
        add_country_modifier = boosted
        remove_country_modifier = old_boost
        any_pop = { always = yes }
        random_list = { 40 = { prestige = 1 } 60 = { prestige = 2 } }
        country_event = { id = sample.2 days = 4 months = 1 }
    }
}
province_event = {
    id = sample.2
    is_triggered_only = yes
}
""",
                encoding="utf-8",
            )

            result = clausewitz_workload_inventory.inventory([root], root)

        self.assertTrue(result["static_counts_are_leads_not_performance_measurements"])
        self.assertEqual(result["schema_version"], 2)
        self.assertEqual([event["id"] for event in result["event_definitions"]], ["sample.1", "sample.2"])
        self.assertEqual(
            result["scheduling_edges"],
            [
                {
                    "command": "country_event",
                    "delays": {"days": "4", "months": "1"},
                    "source": {"line": 18, "path": "events/sample.txt"},
                    "source_event_id": "sample.1",
                    "source_event_kind": "country_event",
                    "target_event_id": "sample.2",
                }
            ],
        )
        self.assertEqual(result["mtth_cadence"][0]["cadence"], {"months": "6"})
        self.assertEqual(result["flag_reads"][0]["target"], "read_flag")
        self.assertEqual(result["flag_writes"][0]["target"], "written_flag")
        self.assertEqual(result["workload_leads"]["iterator_scope_key_leads"], {"any_owned_province": 1, "any_pop": 1})
        self.assertEqual(result["workload_leads"]["random_selector_key_leads"], {"random": 1})
        self.assertEqual(result["workload_leads"]["random_list_leads"], {"blocks": 1, "weighted_entries": 2})
        self.assertEqual(
            result["workload_leads"]["mutation_key_leads"],
            {"add_country_modifier": 1, "remove_country_modifier": 1, "set_country_flag": 1},
        )

    def test_ignores_non_scripts_and_produces_stable_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            later = root / "z_events.txt"
            later.write_text(
                """# country_event = { id = imaginary.1 }
country_event = {
    id = real.2
    option = { event = real.2 }
}
""",
                encoding="utf-8",
            )
            earlier = root / "a_events.txt"
            earlier.write_text("country_event = { id = real.1 }\n", encoding="utf-8")
            (root / "ignored.md").write_text("country_event = { id = imaginary.2 }\n", encoding="utf-8")

            first = clausewitz_workload_inventory.inventory([root], root)
            second = clausewitz_workload_inventory.inventory([root], root)

        self.assertEqual(json.dumps(first, sort_keys=True), json.dumps(second, sort_keys=True))
        self.assertEqual([event["id"] for event in first["event_definitions"]], ["real.1", "real.2"])
        self.assertEqual(first["scheduling_edges"][0]["target_event_id"], "real.2")

    def test_rejects_unrelated_event_suffixes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            script = root / "events.txt"
            script.write_text(
                """audit_event = { id = ignored.1 }
country_event = {
    id = real.1
    option = {
        audit_event = { id = ignored.2 }
        news_event = { id = real.2 }
    }
}
""",
                encoding="utf-8",
            )

            result = clausewitz_workload_inventory.inventory([root], root)

        self.assertEqual([event["id"] for event in result["event_definitions"]], ["real.1"])
        self.assertEqual(len(result["scheduling_edges"]), 1)
        self.assertEqual(result["scheduling_edges"][0]["command"], "news_event")
        self.assertEqual(result["scheduling_edges"][0]["target_event_id"], "real.2")

    def test_block_targets_and_cli_output_collision(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            script = root / "events.txt"
            output = root / "report.json"
            script.write_text(
                """country_event = {
    id = 42
    immediate = {
        set_country_flag = { which = enabled value = yes }
        add_province_modifier = { name = mechanized duration = -1 }
    }
}
""",
                encoding="utf-8",
            )

            completed = subprocess.run(
                [sys.executable, str(SCRIPT), str(script), "--root", str(root), "--output", str(output)],
                check=False,
                capture_output=True,
                text=True,
            )
            report = json.loads(output.read_text(encoding="utf-8"))
            collision = subprocess.run(
                [sys.executable, str(SCRIPT), str(script), "--output", str(script)],
                check=False,
                capture_output=True,
                text=True,
            )

        self.assertEqual(completed.returncode, 0)
        self.assertEqual(completed.stdout, "")
        self.assertEqual(report["schema_version"], 2)
        self.assertEqual(report["flag_writes"][0]["target"], "enabled")
        self.assertEqual(report["modifier_mutations"][0]["target"], "mechanized")
        self.assertEqual(report["province_modifier_workflows"][0]["event_id"], "42")
        self.assertEqual(report["province_modifier_workflows"][0]["event_source"]["start_line"], 1)
        self.assertEqual(report["province_modifier_scope_candidates"], [])
        self.assertNotEqual(collision.returncode, 0)
        self.assertIn("output path is also an input script", collision.stderr)

    def test_reports_province_modifier_workflow_context_and_risks(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            script = root / "events" / "modifier_workflow.txt"
            script.parent.mkdir()
            script.write_text(
                """province_event = {
    id = 2601
    trigger = {
        has_province_modifier = core_integration
        owner = { has_country_flag = integration_enabled }
    }
    mean_time_to_happen = { days = 3 }
    option = {
        state_scope = {
            any_owned = {
                limit = { has_province_modifier = core_integration }
                remove_province_modifier = core_integration
            }
        }
        province_event = { id = 2601 days = 4 }
    }
}
province_event = {
    id = random.1
    trigger = { has_province_modifier = temporary }
    option = {
        random_state = {
            add_province_modifier = { name = temporary duration = 365 }
        }
        secede_province = FROM
    }
}
country_event = {
    id = scheduler.1
    option = { province_event = { id = 2601 days = 4 } }
}
province_event = {
    id = weighted.1
    option = {
        random_list = {
            100 = { any_owned = { remove_province_modifier = weighted } }
        }
    }
}
country_event = {
    id = ignored.1
    option = { add_country_modifier = unrelated }
}
""",
                encoding="utf-8",
            )

            result = clausewitz_workload_inventory.inventory([root], root)

        workflows = result["province_modifier_workflows"]
        self.assertEqual([workflow["event_id"] for workflow in workflows], ["2601", "random.1", "weighted.1"])
        cleanup = workflows[0]
        self.assertEqual(
            cleanup["event_source"], {"end_line": 17, "path": "events/modifier_workflow.txt", "start_line": 1}
        )
        self.assertEqual(cleanup["mtth_cadence"], {"days": "3"})
        self.assertEqual(cleanup["risk_flags"], ["mtth_polling", "state_scope_write"])
        self.assertEqual(cleanup["recurrence"], ["engine_polled_mtth", "explicit_schedule_cycle"])
        self.assertEqual(cleanup["incoming_schedules"][0]["source_event_id"], "2601")
        self.assertEqual(cleanup["incoming_schedules"][0]["delays"], {"days": "4"})
        self.assertEqual(cleanup["flag_reads"], ["integration_enabled"])
        self.assertEqual(cleanup["modifier_reads"][1]["block_path"], ["option", "state_scope", "any_owned", "limit"])
        self.assertEqual(cleanup["modifier_writes"][0]["block_path"], ["option", "state_scope", "any_owned"])
        self.assertEqual(cleanup["iterator_scopes"], ["any_owned"])

        random_workflow = workflows[1]
        self.assertEqual(
            random_workflow["risk_flags"],
            ["finite_modifier_duration", "ownership_mutation", "random_selection"],
        )
        self.assertEqual(random_workflow["modifier_writes"][0]["duration"], "365")
        self.assertEqual(random_workflow["ownership_mutations"], ["secede_province"])

        scope_candidates = result["province_modifier_scope_candidates"]
        self.assertEqual(
            [candidate["scope"] for candidate in scope_candidates], ["any_owned", "random_state", "any_owned"]
        )
        cleanup_scope = scope_candidates[0]
        self.assertEqual(cleanup_scope["block_path"], ["option", "state_scope"])
        self.assertEqual(cleanup_scope["recurrence"], ["engine_polled_mtth", "explicit_schedule_cycle"])
        self.assertEqual(cleanup_scope["risk_flags"], ["mtth_polling", "state_scope_write"])
        self.assertEqual(cleanup_scope["predicate_keys"], ["has_province_modifier"])
        random_scope = scope_candidates[1]
        self.assertEqual(random_scope["risk_flags"], ["finite_modifier_duration", "random_selection"])
        self.assertEqual(random_scope["ownership_mutations"], [])
        weighted_scope = scope_candidates[2]
        self.assertEqual(weighted_scope["block_path"], ["option", "random_list", "100"])
        self.assertEqual(weighted_scope["risk_flags"], ["random_selection"])

    def test_detects_multi_event_schedule_cycles(self) -> None:
        edges: list[dict[str, object]] = [
            {"source_event_id": "a", "target_event_id": "b"},
            {"source_event_id": "b", "target_event_id": "a"},
            {"source_event_id": "c", "target_event_id": "d"},
        ]

        self.assertEqual(clausewitz_workload_inventory.explicit_schedule_cycle_ids(edges), {"a", "b"})

    def test_preserves_parser_spans_and_ancestor_order(self) -> None:
        document = clausewitz_workload_inventory.parse(
            """country_event = {
    id = sample.1
    option = {
        any_owned = { remove_province_modifier = obsolete }
    }
}
"""
        )
        event = document.children[0]
        descendants = clausewitz_workload_inventory.walk_with_ancestors(event)

        self.assertEqual((event.line, event.end_line), (1, 6))
        self.assertEqual((event.children[0].line, event.children[0].end_line), (2, 2))
        self.assertEqual(
            [(node.key, [ancestor.key for ancestor in ancestors]) for node, ancestors in descendants],
            [
                ("id", []),
                ("option", []),
                ("any_owned", ["option"]),
                ("remove_province_modifier", ["option", "any_owned"]),
            ],
        )


if __name__ == "__main__":
    unittest.main()
