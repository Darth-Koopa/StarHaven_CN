#!/usr/bin/env python3
"""Unit tests for the ELF undefined-symbol checker and its readelf fallback."""
import importlib.util
import subprocess
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "check_undef_syms.py"
spec = importlib.util.spec_from_file_location("check_undef_syms", SCRIPT)
checker = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(checker)


class UndefinedSymbolCheckerTests(unittest.TestCase):
    def test_parse_nm_undefined(self):
        output = "         U osSendMesg\n         U memcpy\n         w __osProbe\n"
        self.assertEqual(checker.parse_nm_undefined(output), {"osSendMesg", "memcpy", "__osProbe"})

    def test_parse_readelf_undefined(self):
        output = """Symbol table '.symtab' contains 3 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 00000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND osSendMesg
     2: 00000100     4 FUNC    GLOBAL DEFAULT    1 memcpy
"""
        self.assertEqual(checker.parse_readelf_undefined(output), {"osSendMesg"})

    def test_falls_back_to_readelf_when_nm_fails(self):
        readelf_output = """Symbol table '.symtab' contains 2 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     1: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND osSendMesg
"""
        calls = []

        def fake_run(command, **kwargs):
            calls.append(command[0])
            if "nm" in Path(command[0]).name:
                return subprocess.CompletedProcess(command, 1, "", "file format not recognized")
            if "readelf" in Path(command[0]).name:
                return subprocess.CompletedProcess(command, 0, readelf_output, "")
            raise AssertionError(f"unexpected command: {command}")

        symbols = checker.undefined_symbols("fake.elf", runner=fake_run)
        self.assertEqual(symbols, {"osSendMesg"})
        self.assertIn("arm-none-eabi-readelf", calls)


if __name__ == "__main__":
    unittest.main()
