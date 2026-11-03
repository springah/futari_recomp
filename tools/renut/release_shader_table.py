#!/usr/bin/env python3
"""futari-linux: Renut's shader table for a Linux build that can be distributed, without any translated game shader.

The Linux build compiles Renut's shader cache (renut_gpu/shader_cache.cpp, written by Renut's XenosRecomp at build
time) into the executable: the game shaders' SPIR-V and a table of numbers about each shader (hashes, fetch masks,
vertex input formats). This writes a copy with the table only, the way futari-nx's tools/renut_release_shaders.py does
for the Switch NRO: the SPIR-V array becomes empty, its sizes 0, and every entry's DXIL and SPIR-V offset and size 0.
The release build then reads the game shaders from vulkan_spirv.bin beside the executable, which the web installer
makes from the player's own executables (shader_library.cpp).

The build runs it when FUTARI_LINUX_RELEASE_SHADERS is ON (app/src/renut_engine/gpu/gpu.cmake).

Usage: python3 tools/linux/release_shader_table.py <shader_cache.cpp> <output .cpp>
"""
import os
import re
import sys


def strip(text):
    for name in ('Spirv', 'Dxil'):
        text, n = re.subn(r'(const uint8_t g_compressed' + name + r'Cache\[\]\s*=\s*\{)(.*?)(\};)',
                          lambda m: m.group(1) + '0' + m.group(3), text, flags=re.S)
        if name == 'Spirv' and n != 1:
            sys.exit('release_shader_table: no g_compressedSpirvCache array in the shader cache')
        text = re.sub(r'(const size_t g_' + name.lower() + r'Cache(?:Compressed|Decompressed)Size\s*=\s*)\d+',
                      r'\g<1>0', text)

    def entry(m):
        fields = m.group(2).split(', ')
        fields[1] = fields[2] = fields[3] = fields[4] = '0'
        return m.group(1) + ', '.join(fields)
    text, n = re.subn(r'^(\t\{ )(0x[0-9A-F]+, \d+, \d+, \d+, \d+)', entry, text, flags=re.M)
    if not n:
        sys.exit('release_shader_table: no entry of g_shaderCacheEntries found')
    return text, n


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    text, count = strip(open(src, encoding='utf-8').read())
    if re.search(r'g_compressedSpirvCache\[\]\s*=\s*\{0\}', text) is None:
        sys.exit('release_shader_table: the SPIR-V array was not emptied')
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out + '.tmp', 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    os.replace(out + '.tmp', out)
    print(f'release_shader_table: {count} shaders in the table, no SPIR-V built in -> {out}')


if __name__ == '__main__':
    main()
