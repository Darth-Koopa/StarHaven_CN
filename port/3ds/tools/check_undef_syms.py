#!/usr/bin/env python3
"""Fail if the linked ELF leaves any libultra-like symbols unresolved.

Prefer arm-none-eabi-nm, then the host nm. If a particular nm cannot read the
ELF (some packaged binutils builds return status 1 instead of printing an empty
symbol list), fall back to readelf's symbol table. Tool failures are reported
with stderr so CI does not hide the actual cause behind CalledProcessError.

Usage: check_undef_syms.py <path-to-elf>
"""

import re
import subprocess
import sys
from pathlib import Path
from typing import Callable, Sequence

PREFIX_RE = re.compile(r"^(os|gSP|gDP|nu|gu|alSeq|alSnd|n_al|__os)")


def parse_nm_undefined(output: str) -> set[str]:
    """Parse `nm --undefined-only` output into symbol names."""
    symbols: set[str] = set()
    for line in output.splitlines():
        parts = line.strip().split()
        if len(parts) >= 2 and parts[-2] in {"U", "w", "v"}:
            symbols.add(parts[-1])
        elif len(parts) >= 2 and parts[0] in {"U", "w", "v"}:
            symbols.add(parts[1])
    return symbols


def parse_readelf_undefined(output: str) -> set[str]:
    """Parse `readelf --wide --symbols` output for Ndx=UND entries."""
    symbols: set[str] = set()
    for line in output.splitlines():
        parts = line.split()
        # Typical row: Num: Value Size Type Bind Vis Ndx Name
        if len(parts) >= 8 and parts[0].rstrip(":").isdigit() and parts[-2] == "UND":
            symbols.add(parts[-1])
    return symbols


def run_tool(command: Sequence[str], runner: Callable = subprocess.run):
    """Run a symbol tool without hiding its diagnostic on non-zero status."""
    try:
        return runner(list(command), capture_output=True, text=True, check=False)
    except FileNotFoundError as exc:
        return None, f"{command[0]} not found ({exc})"


def undefined_symbols(elf: str, runner: Callable = subprocess.run) -> set[str]:
    """Read undefined symbols, trying nm first and readelf as a fallback."""
    failures: list[str] = []
    tools = [
        (["arm-none-eabi-nm", "--undefined-only", elf], parse_nm_undefined),
        (["nm", "--undefined-only", elf], parse_nm_undefined),
        (["arm-none-eabi-readelf", "--wide", "--symbols", elf], parse_readelf_undefined),
        (["readelf", "--wide", "--symbols", elf], parse_readelf_undefined),
    ]
    for command, parser in tools:
        result = run_tool(command, runner)
        if isinstance(result, tuple):
            failures.append(result[1])
            continue
        if result.returncode == 0:
            return parser(result.stdout)
        detail = (result.stderr or result.stdout or "no diagnostic output").strip()
        failures.append(f"{' '.join(command)} exited {result.returncode}: {detail}")

    raise RuntimeError("Could not read ELF symbol table with nm or readelf:\n  " + "\n  ".join(failures))


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: check_undef_syms.py <elf>", file=sys.stderr)
        return 2
    elf = Path(argv[1])
    if not elf.is_file():
        print(f"FAIL: ELF file does not exist or is not a regular file: {elf}", file=sys.stderr)
        return 2

    try:
        symbols = undefined_symbols(str(elf))
    except RuntimeError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 2

    bad = sorted(sym for sym in symbols if PREFIX_RE.match(sym))
    if bad:
        print(f"FAIL: {len(bad)} unresolved libultra-like symbols:", file=sys.stderr)
        for symbol in bad:
            print(f"  {symbol}", file=sys.stderr)
        return 1

    print(f"OK: no unresolved libultra symbols in {elf}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
