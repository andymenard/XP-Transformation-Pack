#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package XP's real waving IE flag from the user's own shell32.dll."""
import argparse
import io
from pathlib import Path
import struct
from PIL import Image
from xp_pe_resources import resources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shell32', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    pe = resources(args.shell32)

    def bitmap(number):
        matches = [value for key, value in pe.items() if key[:2] == (2, number)]
        if not matches:
            raise ValueError(f'XP bitmap {number} is missing')
        data = matches[0]
        header, width, height, planes, bits = struct.unpack_from('<IiiHH', data)
        if header != 40 or bits != 24 or planes != 1:
            raise ValueError('Unexpected bitmap format; requires XP 24-bit flag artwork')
        return Image.open(io.BytesIO(b'BM' + struct.pack('<IHHI', len(data)+14, 0, 0, 54) + data)).convert('RGBA')

    strip = bitmap(242)
    idle = bitmap(240)
    if strip.size != (38, 1900) or idle.size != (22, 1100):
        raise ValueError('Unexpected flag strip dimensions; refusing to cut incorrect frames')
    frames = [strip.crop((0, i*38, 38, (i+1)*38)) for i in range(50)]
    args.output.mkdir(parents=True, exist_ok=True)
    frames[0].save(args.output/'windows-loading.png', save_all=True,
                   append_images=frames[1:], duration=80, loop=0, disposal=0, blend=0)
    idle.crop((0, 0, 22, 22)).save(args.output/'windows.png')
    print('Packaged 50 original Microsoft flag frames; their original rights are unchanged.')


if __name__ == '__main__':
    main()
