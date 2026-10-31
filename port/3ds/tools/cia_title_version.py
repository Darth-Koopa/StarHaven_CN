#!/usr/bin/env python3
"""Set a CIA's title version (major << 10 | minor << 4 | micro) in its ticket and TMD.

usage: cia_title_version.py <file.cia> <major> <minor> <micro>

Our makerom drops the minor version (-minor, -ver and RemasterVersion all come
out as 0), so every release read as 1.0.<micro> and FBI refused 1.3.x over
1.2.1x as "older" (GitHub #73). The version sits outside what the content
hashes cover, and homebrew CIAs are not validly signed anyway.
"""
import struct
import sys

SIG_SIZE = {0x10003: 0x200 + 0x3C, 0x10004: 0x100 + 0x3C, 0x10005: 0x3C + 0x40}


def align(x):
    return (x + 63) & ~63


def main(path, major, minor, micro):
    major, minor, micro = int(major), int(minor), int(micro)
    if major > 63 or minor > 63 or micro > 15:
        sys.exit(f"version {major}.{minor}.{micro} doesn't fit (6.6.4 bits)")
    version = major << 10 | minor << 4 | micro
    with open(path, "r+b") as f:
        hdr, _, _, cert_size, ticket_size, _ = struct.unpack("<IHHIII", f.read(20))
        ticket = align(hdr) + align(cert_size)
        tmd = ticket + align(ticket_size)
        for name, base, field in (("ticket", ticket, 0xA6), ("tmd", tmd, 0x9C)):
            f.seek(base)
            sig = struct.unpack(">I", f.read(4))[0]
            if sig not in SIG_SIZE:
                sys.exit(f"{name}: unknown signature type {sig:#x}")
            f.seek(base + 4 + SIG_SIZE[sig] + field)
            f.write(struct.pack(">H", version))
    print(f"{path}: title version {version} ({major}.{minor}.{micro})")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(*sys.argv[1:])
