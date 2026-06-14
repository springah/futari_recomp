#!/usr/bin/env python3
"""
Reads a decrypted/decompressed XEX2 binary and prints the values needed
for ppc/ppc_config.h and config/futari_rexglue.toml.

Requires the XEX to be already decrypted. If you have xextool or XenonUtils
built, use those to decrypt first:
    xextool -e u -o default_dec.xex default.xex

Or use 360tools extract_pe.py (from sp00nznet/360tools):
    python3 extract_pe.py assets/default.xex assets/default_pe.bin

Usage:
    python3 scripts/analyse_xex.py assets/default.xex
"""

import struct
import sys
from pathlib import Path


XEX2_MAGIC = b"XEX2"

# Known ABI helper signatures to scan for (PPC opcodes, big-endian)
# These are the save/restore stub patterns used by the Xbox 360 ABI.
# _savegprlr_14 starts with:  stw  r14, -72(r1)  = 0x91C1FFB8
# _restgprlr_14 starts with:  lwz  r14, -72(r1)  = 0x81C1FFB8
# _savefpr_14 starts with:    stfd f14, -144(r1) = 0xD9C1FF70
# _restfpr_14 starts with:    lfd  f14, -144(r1) = 0xC9C1FF70
ABI_PATTERNS = {
    "savegprlr_14": b"\x91\xC1\xFF\xB8",
    "restgprlr_14": b"\x81\xC1\xFF\xB8",
    "savefpr_14":   b"\xD9\xC1\xFF\x70",
    "restfpr_14":   b"\xC9\xC1\xFF\x70",
}

# _savevmx_14: stvx v14, r0, r12  where r12 holds the offset
# Approximate: look for 0x11800CE7 (stvx v12,r0,r12 - varies)
# These are harder to find by pattern alone; noted as TODO.


def parse_xex2_header(data: bytes):
    if data[:4] != XEX2_MAGIC:
        return None
    # XEX2 is big-endian (PowerPC)
    pe_offset    = struct.unpack_from(">I", data, 8)[0]
    sec_offset   = struct.unpack_from(">I", data, 16)[0]
    header_count = struct.unpack_from(">I", data, 20)[0]
    entry_point  = None
    image_base   = None
    image_size   = None

    # Walk optional headers (key + value, each 4 bytes, big-endian)
    off = 24
    for _ in range(header_count):
        if off + 8 > len(data):
            break
        key = struct.unpack_from(">I", data, off)[0]
        val = struct.unpack_from(">I", data, off + 4)[0]
        off += 8
        if key == 0x00010100:    # entry point (direct DWORD)
            entry_point = val
        elif key == 0x00010201:  # image base (direct DWORD)
            image_base = val

    # image_size is in the XEX2 security info structure at sec_offset + 4
    if sec_offset + 8 <= len(data):
        image_size = struct.unpack_from(">I", data, sec_offset + 4)[0]

    return {
        "pe_offset": pe_offset,
        "entry_point": entry_point,
        "image_base": image_base,
        "image_size": image_size,
    }


def scan_for_abi_helpers(data: bytes, image_base: int, pe_offset: int):
    results = {}
    for name, pattern in ABI_PATTERNS.items():
        idx = data.find(pattern, pe_offset)
        if idx != -1:
            addr = image_base + (idx - pe_offset)
            results[name] = addr
        else:
            results[name] = None
    return results


def main():
    if len(sys.argv) < 2:
        sys.exit("Usage: python3 scripts/analyse_xex.py assets/default.xex")

    path = Path(sys.argv[1])
    if not path.exists():
        sys.exit(f"File not found: {path}")

    data = path.read_bytes()
    print(f"Read {len(data):,} bytes from {path}")

    hdr = parse_xex2_header(data)
    if hdr is None:
        print("WARNING: XEX2 magic not found. File may be a raw PE (already extracted).")
        print("Run this on the original default.xex, not the extracted PE.")
        # Try to infer image base from PE header
        pe_off = data.find(b"MZ")
        if pe_off == -1:
            sys.exit("No MZ header found either — unrecognised format.")
        hdr = {"pe_offset": pe_off, "entry_point": None, "image_base": 0x82000000}
        print(f"Assuming image_base = 0x82000000 (Xbox 360 default)")
    else:
        print(f"\nXEX2 header parsed:")
        print(f"  PE data offset : 0x{hdr['pe_offset']:08X}")
        print(f"  Image base     : 0x{hdr['image_base']:08X}" if hdr['image_base'] else "  Image base     : (not found in headers)")
        print(f"  Entry point    : 0x{hdr['entry_point']:08X}" if hdr['entry_point'] else "  Entry point    : (not found in headers)")

    image_base = hdr["image_base"] or 0x82000000
    pe_offset  = hdr["pe_offset"]

    # Scan for ABI helpers
    abi = scan_for_abi_helpers(data, image_base, pe_offset)

    print(f"\n--- ppc/ppc_config.h values ---")
    print(f"#define PPC_IMAGE_BASE  0x{image_base:08X}U")
    if hdr.get("image_size"):
        print(f"#define PPC_IMAGE_SIZE  0x{hdr['image_size']:08X}U")
    if hdr.get("entry_point"):
        print(f"// entry_point     0x{hdr['entry_point']:08X}")

    print(f"\n--- config/futari_rexglue.toml ABI addresses ---")
    for name, addr in abi.items():
        if addr:
            print(f"{name}_address = 0x{addr:08X}")
        else:
            print(f"# {name}_address = ??? (pattern not found — use XenonAnalyse)")

    print("\n--- Next step ---")
    print("1. Fill in the values above into ppc/ppc_config.h and config/futari_rexglue.toml")
    print("2. Run XenonAnalyse on the XEX to generate switch tables:")
    print("   XenonAnalyse assets/default.xex config/futari_switch_tables.toml")
    print("3. Then run CMake configure + codegen.")


if __name__ == "__main__":
    main()
