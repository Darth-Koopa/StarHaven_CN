#!/usr/bin/env python3
"""Quick utility to verify the 3DS port file structure is correct."""

import os
import sys
from pathlib import Path

def check_file(path, description):
    if Path(path).exists():
        print(f"  [OK] {description}")
        return True
    else:
        print(f"  [MISSING] {description}: {path}")
        return False

def main():
    base = Path(__file__).resolve().parent.parent
    
    print("Paper Mario 3DS Port - Structure Verification")
    print("=" * 50)
    
    all_ok = True
    
    print("\nHeaders:")
    all_ok &= check_file(base / "include/ultra64.h", "Unified ultra64 replacement")
    all_ok &= check_file(base / "include/platform.h", "Platform configuration")
    all_ok &= check_file(base / "include/n64/os_types.h", "OS type aliases")
    all_ok &= check_file(base / "include/n64/os_shim.h", "OS API shim")
    all_ok &= check_file(base / "include/n64/cont_shim.h", "Controller shim")
    all_ok &= check_file(base / "include/n64/gfx_shim.h", "Graphics shim")
    all_ok &= check_file(base / "include/n64/audio_shim.h", "Audio shim")
    all_ok &= check_file(base / "include/n64/math_shim.h", "Math shim")
    all_ok &= check_file(base / "include/n64/heap_shim.h", "Heap shim")
    all_ok &= check_file(base / "include/n64/romfs.h", "ROMFS shim")
    all_ok &= check_file(base / "include/nu/nusys.h", "NuSystem shim")
    all_ok &= check_file(base / "include/nu/nualsgi.h", "NuAudio shim")
    
    print("\nSource:")
    all_ok &= check_file(base / "src/3ds_main.c", "3DS entry point")
    all_ok &= check_file(base / "src/n64_shim/os_impl.c", "OS implementation")
    all_ok &= check_file(base / "src/n64_shim/cont_impl.c", "Controller implementation")
    all_ok &= check_file(base / "src/n64_shim/gfx_impl.c", "Graphics implementation")
    all_ok &= check_file(base / "src/n64_shim/audio_impl.c", "Audio implementation")
    all_ok &= check_file(base / "src/n64_shim/math_impl.c", "Math implementation")
    all_ok &= check_file(base / "src/n64_shim/heap_impl.c", "Heap implementation")
    all_ok &= check_file(base / "src/n64_shim/romfs_impl.c", "ROMFS implementation")
    
    print("\nBuild:")
    all_ok &= check_file(base / "Makefile", "Build system")
    all_ok &= check_file(base / "setup_build.sh", "Setup script")
    all_ok &= check_file(base / "README.md", "Port documentation")
    
    print("\nTools:")
    all_ok &= check_file(base / "tools/extract_assets.py", "Asset extractor")
    
    print("\n" + "=" * 50)
    if all_ok:
        print("All required files present!")
        return 0
    else:
        print("Some files are missing.")
        return 1

if __name__ == "__main__":
    sys.exit(main())
