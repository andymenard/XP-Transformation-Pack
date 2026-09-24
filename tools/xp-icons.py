#!/usr/bin/python3
"""xp-icons.py FILE OUT.png [ID...] [--size N] [--cols N]
   xp-icons.py --audit OUT.png      every row of src/app/icons.cpp's map

A contact sheet of the icon groups in an XP DLL, EXE or CPL (from the
extracted media, ~/.local/share/xplasma/extracted), each labelled with its
resource id -- the ids icons.cpp's map names -- to check or pick them by eye.
With IDs (or ranges, 270-280), only those. --audit draws each row of the
icon map instead, its icon over the first name it's given as, to check the
map by eye. A developer's tool: needs Python's pefile and Pillow.
"""
import argparse, io, os, re, struct
import pefile
from PIL import Image, ImageDraw

p = argparse.ArgumentParser()
p.add_argument('file', nargs='?')
p.add_argument('out', nargs='?')
p.add_argument('ids', nargs='*')
p.add_argument('--audit', metavar='OUT')
p.add_argument('--media', default=os.path.expanduser('~/.local/share/xplasma/extracted'))
p.add_argument('--size', type=int, default=32)
p.add_argument('--cols', type=int, default=12)
a = p.parse_args()


def load(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
    return pe


def resources(pe, type_id):
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if t.id == type_id:
            for name in t.directory.entries:
                lang = name.directory.entries[0]
                yield name.id if name.id is not None else str(name.name), \
                    pe.get_data(lang.data.struct.OffsetToData, lang.data.struct.Size)


RT_ICON, RT_GROUP_ICON = 3, 14


def image(icons, group):
    """The group's image nearest the size asked, as an .ico Pillow reads."""
    _, _, count = struct.unpack_from('<HHH', group)
    entries = [struct.unpack_from('<BBBBHHIH', group, 6 + 14 * i) for i in range(count)]
    best = min(entries, key=lambda e: (abs((e[0] or 256) - a.size), -e[5]))   # nearest size, deepest colour
    data = icons[best[7]]
    header = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', *best[:6], len(data), 22)
    im = Image.open(io.BytesIO(header + data))
    return im.convert('RGBA').resize((a.size, a.size), Image.NEAREST)


def sheet(cells, out, width):
    """cells: [(label, image)] in a grid, each label under its icon."""
    lines = max(label.count('\n') + 1 for label, _ in cells)
    cw, ch = max(a.size, width) + 8, a.size + 8 + 12 * lines
    rows = (len(cells) + a.cols - 1) // a.cols
    sheet = Image.new('RGBA', (a.cols * cw, rows * ch), 'white')
    draw = ImageDraw.Draw(sheet)
    for i, (label, im) in enumerate(cells):
        x, y = (i % a.cols) * cw, (i // a.cols) * ch
        if im is not None:
            sheet.alpha_composite(im, (x + (cw - a.size) // 2, y + 2))
        draw.text((x + 4, y + a.size + 5), label, fill='black' if im is not None else 'red')
    sheet.save(out)
    print(out, len(cells), 'icons')


if a.audit:
    source = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'app', 'icons.cpp')).read()
    rows = re.findall(r'\{"([\w.]+)", (\d+), "\w+", "([^"]+)"\}', source)
    files = {}
    cells = []
    for file, gid, names in rows:
        if file not in files:
            pe = load(os.path.join(a.media, file))
            files[file] = (dict(resources(pe, RT_ICON)), dict(resources(pe, RT_GROUP_ICON)))
        icons, groups = files[file]
        group = groups.get(int(gid))
        label = f'{file.split(".")[0]} {gid}\n{names.split()[0][:22]}'
        cells.append((label, image(icons, group) if group else None))
    a.size = max(a.size, 32)
    sheet(cells, a.audit, 150)
    raise SystemExit

pe = load(a.file)
icons = dict(resources(pe, RT_ICON))
wanted = set()
for spec in a.ids:
    lo, _, hi = spec.partition('-')
    wanted.update(range(int(lo), int(hi or lo) + 1))
sheet([(str(gid), image(icons, g)) for gid, g in resources(pe, RT_GROUP_ICON)
       if not wanted or gid in wanted], a.out, 40)
