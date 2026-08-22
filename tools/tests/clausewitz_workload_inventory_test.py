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
        self.assertEqual(report["flag_writes"][0]["target"], "enabled")
        self.assertEqual(report["modifier_mutations"][0]["target"], "mechanized")
        self.assertNotEqual(collision.returncode, 0)
        self.assertIn("output path is also an input script", collision.stderr)


if __name__ == "__main__":
    unittest.main()
