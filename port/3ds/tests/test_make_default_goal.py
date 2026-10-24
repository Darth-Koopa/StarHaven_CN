#!/usr/bin/env python3
"""Regression tests for recursive Make target selection in the 3DS build."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
MAKEFILE = ROOT / "port" / "3ds" / "Makefile"
WORKFLOW = ROOT / ".github" / "workflows" / "ique-3ds.yml"


class MakeDefaultGoalTests(unittest.TestCase):
    def test_recursive_make_pins_output_target(self):
        source = MAKEFILE.read_text(encoding="utf-8")
        recursive_branch = source.index(".DEFAULT_GOAL := $(OUTPUT).3dsx", source.index("# The recursive make runs"))
        translation_rule = source.index("$(UI_TRANSLATION_HEADER):")
        output_rule = source.index("$(OUTPUT).3dsx\t:")
        self.assertLess(recursive_branch, translation_rule)
        self.assertLess(translation_rule, output_rule)

    def test_outer_make_names_output_target_and_checks_same_elf(self):
        source = MAKEFILE.read_text(encoding="utf-8")
        self.assertIn("-C $(BUILD) -f $(CURDIR)/Makefile $(OUTPUT).3dsx", source)
        self.assertIn("check_undef_syms.py $(OUTPUT).elf", source)
        self.assertIn("test -s $(OUTPUT).elf", source)

    def test_workflow_classifies_missing_elf_as_configuration_error(self):
        source = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("FAIL: expected linked ELF missing after \\.3dsx build:", source)


if __name__ == "__main__":
    unittest.main()
