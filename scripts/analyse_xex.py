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

# VMX (vector) save/restore stubs can't be pinned to a single 32-bit value the
# way the GPR/FPR ones can: the leading `li r11, imm` immediate changes per
# register and the XDK's choice of base/index register (r11 vs r12) varies, so a
# fixed byte pattern is brittle. Instead we match the structural prologue of the
# v14 entry point:
#     __savevmx_14:  li r11, <neg imm>  ;  stvx v14, rA, rB
#     __restvmx_14:  li r11, <neg imm>  ;  lvx  v14, rA, rB
# i.e. opcode + extended opcode + target vector register (14), anchored by the
# immediately-preceding `li` (addi rD, r0, imm) to avoid false positives from
# ordinary `stvx v14` / `lvx v14` uses elsewhere in the code section.
#
# PPC encodings (big-endian, 4-byte words):
#   addi rD, r0, imm  -> primary opcode 14, RA field == 0   ("li rD, imm")
#   stvx vS, rA, rB   -> primary opcode 31, extended opcode 231
#   lvx  vT, rA, rB   -> primary opcode 31, extended opcode 103
PPC_OP_ADDI = 14
PPC_OP_X = 31
PPC_XO_STVX = 231
PPC_XO_LVX = 103
VMX_HELPER_REG = 14  # the v14 entry is the start of the save/restore run

# The VMX128 high-register stubs (__savevmx_64 / __restvmx_64, covering v64-v127)
# use the stvx128/lvx128 VMX128 encodings (primary opcode 4 with a split register
# field) and are not detected here — run XenonAnalyse for those addresses.


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


def scan_for_vmx_helpers(data: bytes, image_base: int, pe_offset: int):
    """Locate __savevmx_14 / __restvmx_14 by their (li ; stvx/lvx v14) prologue.

    Scans the code section on 4-byte (instruction) boundaries for an `li`
    immediately followed by an stvx/lvx targeting vector register 14, which is
    the entry point of the v14..v31 save/restore run. Returns the first match
    of each (None if not found)."""
    results = {"savevmx_14": None, "restvmx_14": None}
    end = len(data) - 8  # need room for the li + the following store/load
    off = pe_offset
    while off <= end:
        w0 = struct.unpack_from(">I", data, off)[0]
        # `li rD, imm` is `addi rD, r0, imm`: opcode 14 with the RA field zeroed.
        if (w0 >> 26) & 0x3F == PPC_OP_ADDI and (w0 >> 16) & 0x1F == 0:
            w1 = struct.unpack_from(">I", data, off + 4)[0]
            op1 = (w1 >> 26) & 0x3F
            vs  = (w1 >> 21) & 0x1F
            xo  = (w1 >> 1) & 0x3FF
            if op1 == PPC_OP_X and vs == VMX_HELPER_REG:
                addr = image_base + (off - pe_offset)
                if xo == PPC_XO_STVX and results["savevmx_14"] is None:
                    results["savevmx_14"] = addr
                elif xo == PPC_XO_LVX and results["restvmx_14"] is None:
                    results["restvmx_14"] = addr
                if results["savevmx_14"] and results["restvmx_14"]:
                    break
        off += 4
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
    vmx = scan_for_vmx_helpers(data, image_base, pe_offset)

    print(f"\n--- ppc/ppc_config.h values ---")
    print(f"#define PPC_IMAGE_BASE  0x{image_base:08X}U")
    if hdr.get("image_size"):
        print(f"#define PPC_IMAGE_SIZE  0x{hdr['image_size']:08X}U")
    if hdr.get("entry_point"):
        print(f"// entry_point     0x{hdr['entry_point']:08X}")

    print(f"\n--- config/futari_rexglue.toml ABI addresses ---")
    for name, addr in {**abi, **vmx}.items():
        if addr:
            print(f"{name}_address = 0x{addr:08X}")
        else:
            print(f"# {name}_address = ??? (pattern not found — use XenonAnalyse)")
    print(f"# savevmx_64 / restvmx_64 (VMX128, v64-v127) not scanned — use XenonAnalyse")

    print("\n--- Next step ---")
    print("1. Fill in the values above into ppc/ppc_config.h and config/futari_rexglue.toml")
    print("2. Run XenonAnalyse on the XEX to generate switch tables:")
    print("   XenonAnalyse assets/default.xex config/futari_switch_tables.toml")
    print("3. Then run CMake configure + codegen.")


if __name__ == "__main__":
    main()
