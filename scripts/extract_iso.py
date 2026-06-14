#!/usr/bin/env python3
"""
Extracts default.xex and game assets from a Mushihimesama Futari Ver. 1.5
Xbox 360 disc ISO (XDVDFS filesystem).

Usage:
    python3 scripts/extract_iso.py [path/to/game.iso] [--out assets/]
"""

import struct
import os
import sys
import argparse
from pathlib import Path

XDVDFS_MAGIC = b"MICROSOFT*XBOX*MEDIA"
SECTOR_SIZE = 2048

# Known offsets where the XDVDFS volume descriptor can appear in an ISO
CANDIDATE_OFFSETS = [
    0x10000,       # Standard XGD / most disc rips
    0x2080000,     # XGD2 backup partition
    0x0FDA0000,    # XGD2 with video partition (Mushi Futari RF rip)
    0x1FB20000,    # XGD3
]


def find_volume_descriptor(f):
    # Try known offsets first
    for base in CANDIDATE_OFFSETS:
        f.seek(base)
        header = f.read(0x800)
        if len(header) < 0x800:
            continue
        if header[:20] == XDVDFS_MAGIC and header[0x7EC:0x7EC + 20] == XDVDFS_MAGIC:
            print(f"  Found XDVDFS volume descriptor at offset 0x{base:08X}")
            root_sector = struct.unpack_from("<I", header, 0x14)[0]
            root_size   = struct.unpack_from("<I", header, 0x18)[0]
            return base, root_sector, root_size

    # Fall back: scan the first 2 GB for the magic signature
    print("  Known offsets failed — scanning for XDVDFS magic (may take a moment)...")
    f.seek(0)
    chunk_size = 1 << 20  # 1 MB
    offset = 0
    limit = 2 * (1 << 30)
    while offset < limit:
        chunk = f.read(chunk_size)
        if not chunk:
            break
        idx = chunk.find(XDVDFS_MAGIC)
        while idx != -1:
            base = offset + idx
            # Read a full volume descriptor at this location
            f.seek(base)
            header = f.read(0x800)
            if len(header) == 0x800 and header[0x7EC:0x7EC + 20] == XDVDFS_MAGIC:
                print(f"  Found XDVDFS volume descriptor at offset 0x{base:08X}")
                root_sector = struct.unpack_from("<I", header, 0x14)[0]
                root_size   = struct.unpack_from("<I", header, 0x18)[0]
                return base, root_sector, root_size
            f.seek(offset + len(chunk))
            idx = chunk.find(XDVDFS_MAGIC, idx + 1)
        offset += len(chunk)

    return None


def read_directory(f, fs_base, sector, size):
    """Parse a flat XDVDFS directory table and return list of entry dicts."""
    f.seek(fs_base + sector * SECTOR_SIZE)
    data = f.read(size)
    entries = []
    offset = 0
    while offset < len(data):
        if offset + 14 > len(data):
            break
        left, right = struct.unpack_from("<HH", data, offset)
        if left == 0xFFFF and right == 0xFFFF:
            # Empty sentinel — skip 4 bytes and try to align
            offset += 4
            continue
        start_sector, file_size = struct.unpack_from("<II", data, offset + 4)
        attributes = data[offset + 12]
        name_len   = data[offset + 13]
        if name_len == 0 or offset + 14 + name_len > len(data):
            break
        name = data[offset + 14: offset + 14 + name_len].decode("ascii", errors="replace")
        entries.append({
            "name": name,
            "start_sector": start_sector,
            "size": file_size,
            "is_dir": bool(attributes & 0x10),
            "left": left,
            "right": right,
        })
        # Entries are 4-byte aligned
        entry_len = 14 + name_len
        offset += (entry_len + 3) & ~3
    return entries


def extract_tree(f, fs_base, sector, size, dest: Path, rel=""):
    entries = read_directory(f, fs_base, sector, size)
    for e in entries:
        item_path = dest / e["name"]
        rel_path  = f"{rel}/{e['name']}" if rel else e["name"]
        if e["is_dir"]:
            item_path.mkdir(parents=True, exist_ok=True)
            extract_tree(f, fs_base, e["start_sector"], e["size"], item_path, rel_path)
        else:
            item_path.parent.mkdir(parents=True, exist_ok=True)
            f.seek(fs_base + e["start_sector"] * SECTOR_SIZE)
            remaining = e["size"]
            with open(item_path, "wb") as out:
                while remaining > 0:
                    chunk = f.read(min(65536, remaining))
                    if not chunk:
                        break
                    out.write(chunk)
                    remaining -= len(chunk)
            print(f"  extracted  {rel_path}  ({e['size']:,} bytes)")


def main():
    parser = argparse.ArgumentParser(description="Extract Xbox 360 disc ISO (XDVDFS)")
    parser.add_argument("iso", nargs="?", help="Path to .iso file")
    parser.add_argument("--out", default="assets", help="Output directory (default: assets/)")
    args = parser.parse_args()

    # Auto-discover ISO if not specified
    if args.iso:
        iso_path = Path(args.iso)
    else:
        candidates = list(Path(".").glob("*.iso")) + list(Path("game").glob("*.iso"))
        if not candidates:
            sys.exit("No .iso file found. Pass path explicitly: python3 scripts/extract_iso.py game.iso")
        iso_path = candidates[0]
        print(f"Auto-detected ISO: {iso_path}")

    if not iso_path.exists():
        sys.exit(f"ISO not found: {iso_path}")

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Opening {iso_path} ({iso_path.stat().st_size / 1e9:.2f} GB)")

    with open(iso_path, "rb") as f:
        result = find_volume_descriptor(f)
        if result is None:
            sys.exit(
                "Could not find XDVDFS volume descriptor.\n"
                "The ISO may be encrypted, split, or use an unsupported format.\n"
                "Try extracting with extract-xiso first: https://github.com/XboxDev/extract-xiso"
            )

        fs_base, root_sector, root_size = result
        print(f"  Root dir: sector {root_sector}, size {root_size} bytes")
        print(f"Extracting to {out_dir}/")
        extract_tree(f, fs_base, root_sector, root_size, out_dir)

    xex = out_dir / "default.xex"
    if not xex.exists():
        # Check one level deep
        for candidate in out_dir.rglob("default.xex"):
            xex = candidate
            break

    if xex.exists():
        # Copy to assets root for ReXGlue
        target = out_dir / "default.xex"
        if xex != target:
            import shutil
            shutil.copy2(xex, target)
        print(f"\ndefault.xex -> {target}")
        print("Extraction complete. Run next steps:")
        print("  python3 scripts/analyse_xex.py assets/default.xex")
    else:
        print("\nWARNING: default.xex not found in extracted files.")
        print("The disc may use a different executable name or structure.")


if __name__ == "__main__":
    main()
