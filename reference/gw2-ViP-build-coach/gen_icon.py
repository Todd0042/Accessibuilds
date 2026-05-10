#!/usr/bin/env python3
"""
Embed a PNG icon as a C byte array for Textures_GetOrCreateFromMemory.
Usage: python3 gen_icon.py [icon.png]
Output: src/icon_png.h

See lessons_learned.txt §3 for context on why this approach is used
(Textures_LoadFromResource is broken under MinGW/Proton).
"""
import sys
import os

def embed(src_path: str, out_path: str, array_name: str) -> None:
    data = open(src_path, 'rb').read()
    with open(out_path, 'w') as f:
        f.write('#pragma once\n\n')
        f.write(f'static const unsigned char {array_name}[] = {{\n    ')
        hex_bytes = [f'0x{b:02x}' for b in data]
        for i, h in enumerate(hex_bytes):
            f.write(h)
            if i < len(hex_bytes) - 1:
                f.write(', ')
            if (i + 1) % 16 == 0:
                f.write('\n    ')
        f.write(f'\n}};\n\n')
        f.write(f'static const unsigned int {array_name}_len = {len(data)};\n')
    print(f'Success: wrote {len(data)} bytes -> {out_path}')

if __name__ == '__main__':
    src = sys.argv[1] if len(sys.argv) > 1 else 'icon.png'
    if not os.path.exists(src):
        print(f'ERROR: {src} not found.')
        print('Place your icon PNG in the project root and re-run:')
        print('  python3 gen_icon.py icon.png')
        sys.exit(1)

    out_dir = os.path.join(os.path.dirname(__file__), 'src')
    os.makedirs(out_dir, exist_ok=True)
    embed(src, os.path.join(out_dir, 'icon_png.h'), 'icon_png')
    print()
    print('Then in src/addon.cpp:')
    print('  1. Uncomment #include "icon_png.h"')
    print('  2. Set HAS_ICON = true')
    print('  3. Rebuild')
