from __future__ import annotations

import tempfile
import unittest
import sys
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parents[1]))
from check_engine_ownership import ENGINE_SOURCES, audit


class EngineOwnershipAuditTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "game_state").mkdir()
        sources = "\n".join(f"  {source}" for source in sorted(ENGINE_SOURCES))
        (self.root / "game_state/CMakeLists.txt").write_text(
            f"target_sources(smedley_kernel PRIVATE\n{sources})\n", encoding="utf-8"
        )
        (self.root / "plugins/scripting/src").mkdir(parents=True)
        (self.root / "plugins/scripting/src/plugin.cpp").write_text(
            "#include <smedley/event_api.h>\n", encoding="utf-8"
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_accepts_single_kernel_owner(self) -> None:
        self.assertEqual(audit(self.root), [])

    def test_rejects_second_source_owner(self) -> None:
        (self.root / "plugins/CMakeLists.txt").write_text(
            "add_library(plugin game_state/src/runtime.cpp)\n", encoding="utf-8"
        )
        self.assertTrue(any("second owner" in finding for finding in audit(self.root)))

    def test_rejects_internal_header_in_migrated_plugin(self) -> None:
        (self.root / "plugins/scripting/src/plugin.cpp").write_text(
            "#include <smedley/game_state/runtime.hpp>\n", encoding="utf-8"
        )
        self.assertTrue(any("internal game_state" in finding for finding in audit(self.root)))


if __name__ == "__main__":
    unittest.main()
