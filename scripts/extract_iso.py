#!/usr/bin/env python3
"""
Extracts default.xex and game assets from a Mushihimesama Futari Ver. 1.5
Xbox 360 dump, and copies marketplace DLC packages into place.

Accepts any of:
  - a disc ISO (XDVDFS filesystem)
  - a console disc-install ("Games on Demand" / SVOD container) — either the
    `00007000` folder copied off console storage, or any folder above it
  - a folder containing the marketplace DLC packages (`00000002`)
  - a copied `Content\\...\\435A07D7` folder holding both of the above, which
    extracts the game AND installs the DLC in one go

Usage:
    python3 scripts/extract_iso.py [path] [--out assets/] [--dlc-out dlc/]
"""

import hashlib
import struct
import os
import sys
import argparse
import shutil
from pathlib import Path

XDVDFS_MAGIC = b"MICROSOFT*XBOX*MEDIA"
SECTOR_SIZE = 2048

TITLE_ID = 0x435A07D7  # Mushihimesama Futari Ver. 1.5
XCONTENT_MAGICS = (b"LIVE", b"CON ", b"PIRS")
CONTENT_TYPE_DLC = 0x2
CONTENT_TYPE_INSTALLED_DISC = 0x7000

# Known offsets where the XDVDFS volume descriptor can appear in an ISO
CANDIDATE_OFFSETS = [
    0x10000,       # Standard XGD / most disc rips; also GoD installs
    0x2080000,     # XGD2 backup partition
    0x0FDA0000,    # XGD2 with video partition (Mushi Futari RF rip)
    0x1FB20000,    # XGD3
]


# ---------------------------------------------------------------------------
# XContent packages (console-created files: disc installs and DLC)
# ---------------------------------------------------------------------------

def read_xcontent_header(path: Path):
    """Return {magic, content_type, title_id} if the file is an XContent
    package (LIVE/CON/PIRS), else None."""
    try:
        with open(path, "rb") as f:
            head = f.read(0x400)
    except OSError:
        return None
    if len(head) < 0x364 or head[:4] not in XCONTENT_MAGICS:
        return None
    return {
        "magic": head[:4],
        "content_type": struct.unpack_from(">I", head, 0x344)[0],
        "title_id": struct.unpack_from(">I", head, 0x360)[0],
    }


class GodReader:
    """A console disc install ("Games on Demand" / SVOD container), presented
    as one flat disc image with seek()/read(), so the XDVDFS code below reads
    it exactly like an ISO.

    Layout: beside the header file sits `<name>.data/` holding Data0000,
    Data0001, ... Each file is 0x1000-byte blocks: one level-1 hash-table
    block, then groups of [one level-0 hash-table block + 0xCC data blocks].
    A full file carries 0xCB groups = 0xA1C4 data blocks (0xA290000 bytes on
    disk). The level-0 tables hash each data block, the level-1 table hashes
    the level-0 tables plus the NEXT file's level-1 table, and the header's
    root hash covers the first level-1 table — so verify() can prove the
    whole copy good before extraction starts.
    """

    BLOCK = 0x1000
    DATA_PER_L0 = 0xCC             # data blocks covered by one level-0 table
    DATA_PER_FILE = 0xCB * 0xCC    # data blocks in a full data file (0xA1C4)
    FULL_FILE_BLOCKS = 1 + 0xCB + 0xCB * 0xCC  # = 0xA290 blocks on disk

    def __init__(self, header_path: Path):
        self.header_path = header_path
        header = header_path.read_bytes()
        if header[:4] not in XCONTENT_MAGICS:
            raise ValueError(f"{header_path.name}: not an XContent package")
        self.content_type = struct.unpack_from(">I", header, 0x344)[0]
        self.title_id = struct.unpack_from(">I", header, 0x360)[0]

        # SVOD device descriptor
        desc = header[0x379:0x379 + 0x24]
        self.root_hash = desc[0x04:0x18]
        self.flags = desc[0x18]
        self.data_block_count = int.from_bytes(desc[0x19:0x1C], "big")
        data_block_offset = int.from_bytes(desc[0x1C:0x1F], "big")
        if self.flags or data_block_offset:
            # Console disc installs have both zero. A shifted layout would be
            # absorbed anyway — the extractor locates the filesystem by
            # scanning for its volume descriptor — but say so, since hash
            # verification against this layout is untested.
            print(f"  NOTE: unusual container layout (flags={self.flags:#x}, "
                  f"dataBlockOffset={data_block_offset:#x}) — untested, "
                  "attempting anyway")

        self.data_dir = header_path.parent / (header_path.name + ".data")
        if not self.data_dir.is_dir():
            raise ValueError(f"{self.data_dir.name}: data folder is missing "
                             f"beside {header_path.name}")

        n_files = -(-self.data_block_count // self.DATA_PER_FILE)
        self.data_files = [self.data_dir / f"Data{i:04d}" for i in range(n_files)]
        self._handles = [None] * n_files
        self.size = self.data_block_count * self.BLOCK
        self.pos = 0

    # -- integrity ---------------------------------------------------------

    def _file_shape(self, index):
        """(data blocks, expected byte size) of data file `index`."""
        n = min(self.DATA_PER_FILE,
                self.data_block_count - index * self.DATA_PER_FILE)
        groups = -(-n // self.DATA_PER_L0)
        return n, (1 + groups + n) * self.BLOCK

    def check_files(self):
        """Cheap structural check: every data file present and full size."""
        problems = []
        for i, path in enumerate(self.data_files):
            _, expected = self._file_shape(i)
            if not path.exists():
                problems.append(f"{path.name} is missing "
                                f"(the install has {len(self.data_files)} data files)")
            elif path.stat().st_size != expected:
                problems.append(
                    f"{path.name} is {path.stat().st_size:,} bytes, "
                    f"expected {expected:,} — the copy is incomplete")
        return problems

    def verify(self):
        """Walk the container's own hash tree: root hash -> level-1 tables ->
        level-0 tables -> every data block. Catches a corrupt or truncated
        copy up front, with the exact file named, instead of hours later as a
        codegen failure. Returns a list of problems (empty = proven good)."""
        problems = []
        expected_l1 = self.root_hash
        for i, path in enumerate(self.data_files):
            n_blocks, _ = self._file_shape(i)
            print(f"  verifying {path.name} ...", end="", flush=True)
            bad = None
            with open(path, "rb") as f:
                l1 = f.read(self.BLOCK)
                if hashlib.sha1(l1).digest() != expected_l1:
                    bad = f"{path.name}: level-1 hash table does not match"
                else:
                    done = 0
                    group = 0
                    while done < n_blocks and not bad:
                        take = min(self.DATA_PER_L0, n_blocks - done)
                        l0 = f.read(self.BLOCK)
                        if l1[group * 0x14:(group + 1) * 0x14] != hashlib.sha1(l0).digest():
                            bad = f"{path.name}: hash table for blocks {done}-{done + take - 1} does not match"
                            break
                        for j in range(take):
                            block = f.read(self.BLOCK)
                            if l0[j * 0x14:(j + 1) * 0x14] != hashlib.sha1(block).digest():
                                bad = f"{path.name}: data block {done + j} fails its hash"
                                break
                        done += take
                        group += 1
            if bad:
                print(" CORRUPT")
                problems.append(bad + " — the copy is corrupt")
            else:
                print(" ok")
            # entry 0xCB of each level-1 table hashes the next file's level-1
            expected_l1 = l1[0xCB * 0x14:0xCC * 0x14]
        return problems

    # -- file-like interface used by the XDVDFS reader ---------------------

    def _open(self, index):
        if self._handles[index] is None:
            self._handles[index] = open(self.data_files[index], "rb")
        return self._handles[index]

    def seek(self, pos):
        self.pos = pos

    def read(self, n=-1):
        if n < 0:
            n = self.size - self.pos
        out = bytearray()
        while n > 0 and 0 <= self.pos < self.size:
            block, within = divmod(self.pos, self.BLOCK)
            index, fb = divmod(block, self.DATA_PER_FILE)
            # skip the level-1 table and one level-0 table per 0xCC blocks
            offset = (fb + fb // self.DATA_PER_L0 + 2) * self.BLOCK + within
            take = min(n, self.BLOCK - within)
            f = self._open(index)
            f.seek(offset)
            chunk = f.read(take)
            if not chunk:
                break
            out += chunk
            self.pos += len(chunk)
            n -= len(chunk)
        return bytes(out)

    def close(self):
        for h in self._handles:
            if h:
                h.close()


def find_god_installs(root: Path, max_depth=4):
    """Find disc-install containers (header file + <name>.data folder) for
    this game under `root`."""
    found = []
    root = root.resolve()
    for dirpath, dirnames, filenames in os.walk(root):
        depth = len(Path(dirpath).resolve().parts) - len(root.parts)
        if depth >= max_depth:
            dirnames[:] = []
            continue
        for name in filenames:
            if name + ".data" not in dirnames:
                continue
            path = Path(dirpath) / name
            info = read_xcontent_header(path)
            if (info and info["content_type"] == CONTENT_TYPE_INSTALLED_DISC
                    and info["title_id"] == TITLE_ID):
                found.append(path)
    return found


def find_dlc_packages(root: Path, max_depth=4):
    """Find marketplace DLC packages for this game under `root`."""
    found = []
    root = root.resolve()
    for dirpath, dirnames, filenames in os.walk(root):
        depth = len(Path(dirpath).resolve().parts) - len(root.parts)
        if depth >= max_depth:
            dirnames[:] = []
            continue
        for name in filenames:
            path = Path(dirpath) / name
            info = read_xcontent_header(path)
            if (info and info["content_type"] == CONTENT_TYPE_DLC
                    and info["title_id"] == TITLE_ID):
                found.append(path)
    return found


def copy_dlc(packages, dlc_out: Path):
    """Copy DLC packages into dlc/, filenames unchanged (the game sniffs the
    XContent magic, so the console's hex names are exactly right)."""
    dlc_out.mkdir(parents=True, exist_ok=True)
    for src in packages:
        dest = dlc_out / src.name
        if dest.exists() and dest.stat().st_size == src.stat().st_size:
            print(f"  already present  {src.name}  ({src.stat().st_size:,} bytes)")
            continue
        shutil.copy2(src, dest)
        if dest.stat().st_size != src.stat().st_size:
            PROBLEMS.append(f"dlc/{src.name}: copy came out short")
            print(f"  FAILED     {src.name}")
        else:
            print(f"  installed  {src.name}  ({src.stat().st_size:,} bytes)")


# ---------------------------------------------------------------------------
# XDVDFS (the disc filesystem — inside an ISO or a disc install)
# ---------------------------------------------------------------------------

def find_volume_descriptor(f):
    """Locate the XDVDFS volume descriptor and return (fs_base, root_sector,
    root_size). The descriptor sits at sector 32 of its partition and every
    sector number in the filesystem is partition-relative, so the base for
    all reads is the descriptor's offset MINUS 0x10000 — verified against
    both a full XGD2 rip (partition at 0xFD90000) and a console disc install
    (partition at 0). Earlier versions returned the descriptor offset itself,
    which shifted every read 32 sectors late and made every extracted file
    garbage."""
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
            return base - 0x10000, root_sector, root_size

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
                return base - 0x10000, root_sector, root_size
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


# Files that did not come out whole, or names the ISO should not have contained.
# A split, truncated or corrupt rip is by far the most common problem here, and
# it used to be reported as a success: the loop below broke on a short read, then
# printed the size from the directory table rather than the bytes written, so
# every file could land empty and the script still exited 0. The failure then
# surfaced much later in codegen, with a green extraction log as evidence.
PROBLEMS = []


def safe_name(name: str) -> bool:
    """Reject entry names that would write outside the output directory."""
    return not (
        name in ("", ".", "..")
        or "/" in name
        or "\\" in name
        or ":" in name
    )


def extract_tree(f, fs_base, sector, size, dest: Path, rel=""):
    entries = read_directory(f, fs_base, sector, size)
    for e in entries:
        rel_path  = f"{rel}/{e['name']}" if rel else e["name"]
        if not safe_name(e["name"]):
            PROBLEMS.append(f"{rel_path}: unsafe entry name in the directory table, skipped")
            print(f"  SKIPPED    {rel_path}  (unsafe name -- corrupt directory table?)")
            continue
        item_path = dest / e["name"]
        if e["is_dir"]:
            item_path.mkdir(parents=True, exist_ok=True)
            extract_tree(f, fs_base, e["start_sector"], e["size"], item_path, rel_path)
        else:
            item_path.parent.mkdir(parents=True, exist_ok=True)
            f.seek(fs_base + e["start_sector"] * SECTOR_SIZE)
            remaining = e["size"]
            written = 0
            with open(item_path, "wb") as out:
                while remaining > 0:
                    chunk = f.read(min(65536, remaining))
                    if not chunk:
                        break
                    out.write(chunk)
                    written += len(chunk)
                    remaining -= len(chunk)
            if written != e["size"]:
                PROBLEMS.append(
                    f"{rel_path}: wrote {written:,} of {e['size']:,} bytes (truncated)"
                )
                print(f"  TRUNCATED  {rel_path}  ({written:,} of {e['size']:,} bytes)")
            else:
                print(f"  extracted  {rel_path}  ({written:,} bytes)")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def classify_source(path: Path):
    """Work out what the user handed us. Returns (god_header, iso, dlc_list),
    any of which may be None/empty."""
    if path.is_file():
        info = read_xcontent_header(path)
        if info is None:
            return None, path, []          # presumably an ISO
        if info["title_id"] != TITLE_ID:
            sys.exit(f"{path.name} is an Xbox 360 package, but for title "
                     f"{info['title_id']:08X}, not this game ({TITLE_ID:08X}).")
        if info["content_type"] == CONTENT_TYPE_INSTALLED_DISC:
            return path, None, []
        if info["content_type"] == CONTENT_TYPE_DLC:
            return None, None, [path]
        sys.exit(f"{path.name}: unsupported package type "
                 f"{info['content_type']:#x} (expected a disc install or DLC).")

    gods = find_god_installs(path)
    dlc = find_dlc_packages(path)
    if not gods and not dlc:
        sys.exit(
            f"No disc install or DLC for this game found under {path}.\n"
            "Expected the console's Content layout: a 00007000 folder (the\n"
            "installed disc) and/or a 00000002 folder (the DLC packages) —\n"
            "see docs/DUMPING.md."
        )
    if len(gods) > 1:
        print(f"  NOTE: {len(gods)} disc installs found; using {gods[0]}")
    return (gods[0] if gods else None), None, dlc


def main():
    parser = argparse.ArgumentParser(
        description="Extract a Mushihimesama Futari Ver. 1.5 dump: a disc ISO, "
                    "a console disc-install folder, and/or its DLC packages")
    parser.add_argument("source", nargs="?",
                        help="Path to an .iso, a console-install folder "
                             "(00007000 or any folder above it), or a DLC "
                             "folder (00000002)")
    parser.add_argument("--out", default="assets",
                        help="Assets output directory (default: assets/)")
    parser.add_argument("--dlc-out", default="dlc",
                        help="DLC output directory (default: dlc/)")
    parser.add_argument("--no-verify", action="store_true",
                        help="Skip the disc install's hash-tree verification")
    args = parser.parse_args()

    # Auto-discover an ISO if nothing was specified
    if args.source:
        src = Path(args.source)
        if not src.exists():
            sys.exit(f"Not found: {src}")
        god_header, iso_path, dlc = classify_source(src)
    else:
        candidates = list(Path(".").glob("*.iso")) + list(Path("game").glob("*.iso"))
        if not candidates:
            sys.exit("No .iso file found. Pass a path explicitly: an .iso, or a "
                     "folder copied off your console (see docs/DUMPING.md).")
        if len(candidates) > 1:
            names = ", ".join(str(c) for c in candidates)
            sys.exit(f"Several .iso files found ({names}); pass the right one explicitly.")
        god_header, iso_path, dlc = None, candidates[0], []
        print(f"Auto-detected ISO: {iso_path}")

    out_dir = Path(args.out)
    disc = None

    if god_header is not None:
        print(f"Opening console disc install: {god_header}")
        disc = GodReader(god_header)
        print(f"  {len(disc.data_files)} data files, "
              f"{disc.size / 1e9:.2f} GB of game data")
        structural = disc.check_files()
        if not structural and not args.no_verify:
            print("Verifying the copy against the container's own hash tree...")
            structural = disc.verify()
        if structural:
            print(f"\nEXTRACTION FAILED -- {len(structural)} problem(s):",
                  file=sys.stderr)
            for p in structural:
                print(f"  {p}", file=sys.stderr)
            print("\nRe-copy the install from your console's storage and try "
                  "again;\ndo not build from a copy that fails these checks.",
                  file=sys.stderr)
            sys.exit(1)
    elif iso_path is not None:
        print(f"Opening {iso_path} ({iso_path.stat().st_size / 1e9:.2f} GB)")
        disc = open(iso_path, "rb")

    if disc is not None:
        out_dir.mkdir(parents=True, exist_ok=True)
        result = find_volume_descriptor(disc)
        if result is None:
            sys.exit(
                "Could not find XDVDFS volume descriptor.\n"
                "The ISO may be encrypted, split, or use an unsupported format.\n"
                "Try extracting with extract-xiso first: https://github.com/XboxDev/extract-xiso"
            )
        fs_base, root_sector, root_size = result
        print(f"  Root dir: sector {root_sector}, size {root_size} bytes")
        print(f"Extracting to {out_dir}/")
        extract_tree(disc, fs_base, root_sector, root_size, out_dir)
        disc.close()

    if dlc:
        print(f"Installing {len(dlc)} DLC package(s) to {args.dlc_out}/")
        copy_dlc(dlc, Path(args.dlc_out))

    # Fail loudly on an incomplete extraction rather than reporting success and
    # letting it resurface as a confusing codegen error hours later.
    if PROBLEMS:
        print(f"\nEXTRACTION FAILED -- {len(PROBLEMS)} problem(s):", file=sys.stderr)
        for p in PROBLEMS:
            print(f"  {p}", file=sys.stderr)
        print(
            "\nThe dump is most likely truncated, split across files, or corrupt.\n"
            "Verify it against the checksums in docs/DUMPING.md and re-dump\n"
            "it; do not build from what was extracted here.",
            file=sys.stderr,
        )
        sys.exit(1)

    if disc is None:
        print("\nDLC installed. (No game install was found at that path — "
              "assets/ was left alone.)")
        return

    xex = out_dir / "default.xex"
    if not xex.exists():
        # Search the whole extracted tree for it
        for candidate in out_dir.rglob("default.xex"):
            xex = candidate
            break

    if xex.exists():
        # Copy to assets root for ReXGlue
        target = out_dir / "default.xex"
        if xex != target:
            shutil.copy2(xex, target)
        print(f"\ndefault.xex -> {target}")
        print("Extraction complete. Next: generate the recompiled C++ and build --")
        print("  bash scripts/regen.sh      (or build.ps1 on Windows, which runs it for you)")
    else:
        print("\nWARNING: default.xex not found in extracted files.")
        sys.exit(1)


if __name__ == "__main__":
    main()
