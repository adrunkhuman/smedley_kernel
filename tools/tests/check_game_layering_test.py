"""Tests for the game-layering source audit."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parents[1]))
import check_game_layering


class GameLayeringAuditTest(unittest.TestCase):
    def write_source(self, root: Path, relative_path: str, content: str, plugin: str = "interest_bug_fix") -> Path:
        path = root / "plugins" / plugin / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def test_rejects_raw_engine_access(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.write_source(
                root,
                "src/boundary.cpp",
                """#include <smedley/memory.hpp>
#include <smedley/v2/pop.hpp>
const void *pop;
auto base = memory::Map::base_addr;
constexpr auto address = 0x0055a5f0;
constexpr auto pop_money_offset = 0x180;
void __declspec(naked) Hook() {}
void GiveMoneyVerified() {}
""",
            )

            messages = [finding.message for finding in check_game_layering.audit(root)]

            self.assertEqual(
                set(messages),
                {
                    "raw engine header included: smedley/memory.hpp",
                    "raw engine header included: smedley/v2/pop.hpp",
                    "use typed game_state References, not void game-object pointers",
                    "memory::Map is a game runtime implementation detail",
                    "CPop::GiveMoney RVA belongs in smedley_game_runtime",
                    "engine RVAs and field offsets require a registered raw adapter",
                    "native engine calls require a registered raw adapter",
                    "local GiveMoney wrappers belong in smedley_game_runtime",
                },
            )

    def test_ignores_comments_tests_and_allowed_resolver_context(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.write_source(
                root,
                "src/reader.cpp",
                """// #include <smedley/memory.hpp>
/* memory::Map 0x0055a5f0 GiveMoneyVerified const void *pop */
const char *note = "memory::Map 0x0055a5f0 GiveMoneyVerified const void *pop";
static CountryRef ResolveCountry(const void *context, int ordinal)
{
    return {};
}
""",
            )
            self.write_source(root, "tests/raw_engine_test.cpp", "#include <smedley/memory.hpp>\nvoid *pop;\n")

            self.assertEqual(check_game_layering.audit(root), [])

    def test_allows_only_explicit_raw_adapter_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            adapter = self.write_source(
                root, "src/raw_adapter.cpp", "#include <smedley/memory.hpp>\n", plugin="telemetry"
            )
            self.write_source(root, "src/policy.h", "#include <smedley/v2/pop.hpp>\n", plugin="scripting")

            findings = check_game_layering.audit(root, {adapter})

            self.assertEqual(len(findings), 1)
            self.assertEqual(findings[0].path.name, "policy.h")
            self.assertEqual(findings[0].message, "raw engine header included: smedley/v2/pop.hpp")


if __name__ == "__main__":
    unittest.main()
