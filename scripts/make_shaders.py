#!/usr/bin/env python3
"""
Makes vulkan_spirv.bin, the game's shaders for the Renut renderer, from YOUR copy of the game.

The public build of futari.exe carries no game shader (they are game data, like the executables they are compiled
into). Renut draws with the game's own shaders translated to SPIR-V, so after the assets are extracted this script
translates them on your machine, once, into vulkan_spirv.bin beside futari.exe. extract_assets.bat runs it for you
at the end of the extraction. After updating the port (the file must match the build), double-click
extract_assets.bat with nothing dragged on, which runs only this script, or run it by hand:

    python make_shaders.py                      # next to futari.exe (the release layout)
    py make_shaders.py                          # the same, where python is not on the PATH
    python scripts/make_shaders.py --exe out/build/win-amd64-release/futari.exe --assets assets \\
           --tools out/build/win-amd64-release-shaders/renut_tools      # a developer tree

Steps (every program is futari.exe itself; renut_tools/ beside it holds the DirectX Shader Compiler DLL, the shader
header and the early-alpha pass, no executable):
  1. futari.exe --futari_renut_dump_shaders=<work>/dump: the game loads its executables as it would to run and writes
     their shader containers (42 for Ver 1.5) to the folder, then exits without starting the game.
  2. futari.exe --futari_renut_translate=<work>/dump, twice around Renut's early-alpha pass as the port's own build
     does: XenosRecomp (hedge-dev's Xbox 360 shader translator, Renut's build of it) is built into the exe, and writes
     the shaders as the SPIR-V package the renderer reads.
  3. The work folder (game data) is deleted.
Needs nothing but Python 3.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

PACKAGE_NAME = "vulkan_spirv.bin"
EXPECTED_SHADERS = 42  # Mushihimesama Futari Ver 1.5: 22 + 22 in default.xex, 2 + 2 in each of the eight cores


def fail(msg):
    print(f"[FAILED] {msg}")
    return 1


def run(cmd, env=None, timeout=600, log=None):
    """Runs a tool, prints its output on failure (and always into log when given)."""
    merged = dict(os.environ)
    if env:
        merged.update(env)
    try:
        proc = subprocess.run([str(c) for c in cmd], env=merged, capture_output=True, text=True, timeout=timeout,
                              errors="replace")
    except subprocess.TimeoutExpired:
        print(f"  timed out after {timeout} s: {cmd[0]}")
        return False
    except OSError as e:
        # WinError 225 is Windows refusing to start a file its antivirus quarantined or blocked.
        print(f"  Windows would not start {Path(str(cmd[0])).name}: {e}")
        print("  If your antivirus quarantined it, restore the file and allow this folder, then run this again.")
        if log is not None:
            log.write(f"$ {' '.join(str(c) for c in cmd)}\nOSError: {e}\n")
        return False
    if log is not None:
        log.write(f"$ {' '.join(str(c) for c in cmd)}\n{proc.stdout}{proc.stderr}\n(exit {proc.returncode})\n")
    if proc.returncode != 0:
        print(f"  {Path(str(cmd[0])).name} exited {proc.returncode}")
        tail = (proc.stdout + proc.stderr).strip().splitlines()[-15:]
        for line in tail:
            print("  | " + line)
        return False
    return True


def check_package(path: Path):
    """The header and table of a BVKS package (render/shader_library.cpp): count, or None with a reason."""
    data = path.read_bytes()
    if len(data) < 32:
        return None, "too short"
    magic, version, count, _r0, table, data_off = struct.unpack_from("<4sIIIII", data, 0)
    if magic != b"BVKS" or version != 1:
        return None, "not a SPIR-V package"
    if table + count * 56 > len(data) or data_off > len(data):
        return None, "truncated table"
    last = -1
    for i in range(count):
        h, off, csize, _size, stage = struct.unpack_from("<QIIII", data, table + 56 * i)
        if h <= last or data_off + off + csize > len(data) or stage not in (0, 4):
            return None, f"entry {i} is out of order, out of bounds or of an unknown stage"
        last = h
    return count, None


def main():
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description="Translate the game's shaders for the Renut renderer (vulkan_spirv.bin).")
    ap.add_argument("--exe", default=None, help="futari.exe (default: next to this script, or ../out/build/...)")
    ap.add_argument("--assets", default=None, help="the game data folder (default: assets/ next to the exe)")
    ap.add_argument("--tools", default=None, help="renut_tools/ folder (default: next to the exe)")
    ap.add_argument("--out", default=None, help=f"where to write {PACKAGE_NAME} (default: next to the exe)")
    ap.add_argument("--keep", action="store_true", help="keep the work folder (game data; for diagnosis)")
    args = ap.parse_args()

    exe = Path(args.exe) if args.exe else here / "futari.exe"
    if not exe.is_file():
        return fail(f"no {exe}: run this next to futari.exe, or pass --exe")
    exe = exe.resolve()
    exe_dir = exe.parent
    assets = Path(args.assets).resolve() if args.assets else exe_dir / "assets"
    if not (assets / "default.xex").is_file():
        return fail(f"no {assets / 'default.xex'}: extract the game data first (extract_assets.bat)")
    tools = Path(args.tools).resolve() if args.tools else exe_dir / "renut_tools"
    dxcompiler = tools / "dxcompiler.dll"
    common = tools / "shader_common.h"
    early_alpha = tools / "renut_early_alpha.py"
    for needed in (dxcompiler, common, early_alpha, tools / "early_alpha.py"):
        if not needed.is_file():
            return fail(f"no {needed}: the renut_tools folder that ships with the port is incomplete")
    def translate(dump_dir, out_cpp, hlsl_dir=None):
        """The translator built into the exe (--futari_renut_translate), with the arguments its standalone build took."""
        cmd = [exe, f"--game_data_root={assets}", f"--futari_renut_translate={dump_dir}",
               f"--futari_renut_translate_out={out_cpp}", f"--futari_renut_translate_common={common}",
               f"--futari_renut_dxcompiler={dxcompiler}"]
        if hlsl_dir is not None:
            cmd.append(f"--futari_renut_translate_hlsl={hlsl_dir}")
        return cmd
    out_dir = Path(args.out).resolve() if args.out else exe_dir
    package = out_dir / PACKAGE_NAME

    work = exe_dir / "shader_work"
    if work.exists():
        shutil.rmtree(work)
    dump = work / "dump"
    hlsl = work / "early_alpha_hlsl"
    for d in (dump, hlsl):
        d.mkdir(parents=True)
    log = open(work / "make_shaders.log", "w", encoding="utf-8")
    started = time.time()
    ok = False
    try:
        print("1/3  Reading the shaders out of your game's executables (the game opens and closes by itself)...")
        if not run([exe, f"--game_data_root={assets}", f"--futari_renut_dump_shaders={dump}"], timeout=180, log=log):
            return fail("futari.exe could not dump the shaders (see shader_work/make_shaders.log and the newest logs/*.log)")
        bins = sorted(dump.glob("*.bin"))
        if not (dump / "manifest.csv").is_file() or not bins:
            return fail("the dump is empty: is assets/ a complete Ver 1.5 extraction?")
        print(f"     {len(bins)} shaders" + ("" if len(bins) == EXPECTED_SHADERS else
                                           f" (expected {EXPECTED_SHADERS}: a different game version?)"))

        print("2/3  Translating them (XenosRecomp built into futari.exe, DirectX Shader Compiler)...")
        # As the build (src/renut_engine/gpu/gpu.cmake): a dump-only pass, the early-alpha rewrite, the real pass.
        if not run(translate(dump, work / "pass1.cpp", hlsl), env={"BANJO_HLSL_DUMP_ONLY": "1"}, timeout=300, log=log):
            return fail("the translator's first pass failed (see shader_work/make_shaders.log and the newest logs/*.log)")
        if not run([sys.executable, early_alpha, hlsl], log=log):
            return fail("the early-alpha pass failed")
        tmp_package = work / PACKAGE_NAME
        if not run(translate(dump, work / "cache.cpp"),
                   env={"BANJO_HLSL_OVERRIDE": str(hlsl), "XENOS_RECOMP_SPIRV_PACKAGE": str(tmp_package)},
                   timeout=300, log=log):
            return fail("the translator's second pass failed (see shader_work/make_shaders.log and the newest logs/*.log)")
        if not tmp_package.is_file():
            return fail("the translator wrote no package (is futari.exe this version's build?)")
        count, why = check_package(tmp_package)
        if count is None:
            return fail(f"the package is unusable: {why}")
        if count != len(bins):
            print(f"     note: {len(bins) - count} shader(s) did not translate; the game may show gaps")

        print(f"3/3  {PACKAGE_NAME}: {count} shaders")
        out_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(tmp_package, package)
        # The stamp beside the package names the build that made it; without it the launcher's setup flow would
        # treat this two-pass package as stale and redo it with its own one-pass translation.
        if not run([exe, f"--futari_renut_stamp={out_dir}"], timeout=60, log=log):
            print("     note: could not write vulkan_spirv.stamp (an older futari.exe?); the game will warn that the shaders are unstamped")
        ok = True
        print(f"[DONE] {package} ({package.stat().st_size // 1024} KB, {time.time() - started:.0f} s)")
        return 0
    finally:
        log.close()
        if ok and not args.keep:
            shutil.rmtree(work, ignore_errors=True)
        elif not ok:
            print(f"       (work folder kept for diagnosis: {work}; it holds game data, delete it when done)")


if __name__ == "__main__":
    sys.exit(main())
