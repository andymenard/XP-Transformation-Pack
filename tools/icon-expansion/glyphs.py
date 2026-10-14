"""XP control silhouettes and beveled derivatives of specialized KDE glyphs.

Control paths: GPL-3.0-or-later, original XPlasma artwork.
KDE source SVGs and their license notices remain alongside derived images.
"""
import io
import re
from PIL import Image, ImageChops


def control(name):
    n = re.sub(r'(-symbolic|-rtl)+$', '', name.lower())
    direction = None
    for word in ['left', 'right', 'up', 'down']:
        if n in {'arrow-' + word, 'arrow-' + word + '-double'}:
            direction = word
    if '-symbolic' in name and n in {'go-next', 'go-previous', 'go-up', 'go-down', 'go-first', 'go-last'}:
        direction = {'go-next': 'right', 'go-previous': 'left', 'go-up': 'up', 'go-down': 'down', 'go-first': 'left', 'go-last': 'right'}[n]
    if direction:
        if name.endswith('-rtl') and direction in {'left', 'right'}:
            direction = 'left' if direction == 'right' else 'right'
        paths = {'right': 'M6 3L11 8L6 13Z', 'left': 'M10 3L5 8L10 13Z',
                 'up': 'M3 10L8 5L13 10Z', 'down': 'M3 6L8 11L13 6Z'}
        path = paths[direction]
        if n in {'go-first', 'go-last'}:
            path += 'M2 3H4V13H2Z' if direction == 'left' else 'M12 3H14V13H12Z'
        if n.endswith('-double') and direction == 'down':
            path = 'M3 3L8 7L13 3Z M3 9L8 13L13 9Z'
        if n.endswith('-double') and direction == 'up':
            path = 'M3 7L8 3L13 7Z M3 13L8 9L13 13Z'
        return n + '-' + direction, path
    if n in {'window-close', 'dialog-close'}:
        return 'close', 'M3 4L4 3L8 7L12 3L13 4L9 8L13 12L12 13L8 9L4 13L3 12L7 8Z'
    if n in {'list-add', 'list-remove'} and '-symbolic' in name:
        return n, 'M3 7H13V9H3Z' + ('M7 3H9V13H7Z' if n == 'list-add' else '')
    if n in {'window-minimize', 'window-maximize', 'window-restore'} and '-symbolic' in name:
        return n, {'window-minimize': 'M3 11H13V13H3Z',
                   'window-maximize': 'M3 3H13V13H3ZM4 6V12H12V6Z',
                   'window-restore': 'M5 2H14V11H12V14H2V5H5ZM6 4V5H12V10H13V4ZM3 8V13H11V8Z'}[n]
    if n in {'expand', 'collapse', 'expand-all', 'collapse-all'}:
        return n, 'M2 2H14V14H2ZM3 3V13H13V3ZM4 7H12V9H4Z' + ('M7 4H9V12H7Z' if n.startswith('expand') else '')
    return None


def control_svg(spec):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">'
            '<path fill="#000000" fill-rule="evenodd" d="' + spec[1] + '"/></svg>').encode()


def render(svg, size, symbolic=False):
    try:
        from PySide6 import QtCore, QtGui, QtSvg
    except ImportError:
        from PyQt6 import QtCore, QtGui, QtSvg
    renderer = QtSvg.QSvgRenderer(QtCore.QByteArray(svg))
    if not renderer.isValid():
        raise ValueError('Invalid SVG artwork')
    canvas = QtGui.QImage(size, size, QtGui.QImage.Format.Format_ARGB32_Premultiplied)
    canvas.fill(QtCore.Qt.GlobalColor.transparent)
    painter = QtGui.QPainter(canvas)
    renderer.render(painter, QtCore.QRectF(0, 0, size, size))
    painter.end()
    buffer = QtCore.QBuffer()
    buffer.open(QtCore.QIODevice.OpenModeFlag.WriteOnly)
    canvas.save(buffer, 'PNG')
    image = Image.open(io.BytesIO(bytes(buffer.data()))).convert('RGBA')
    if image.getbbox() is None:
        raise ValueError('SVG renders no visible pixels')
    if symbolic:
        silhouette = Image.new('RGBA', image.size, '#000000')
        silhouette.putalpha(image.getchannel('A'))
        return silhouette
    return image


def xp_svg(svg):
    # Keep warning/positive colors; replace Breeze's neutral palette with XP blue.
    text = svg.decode()
    for color in ['#232629', '#31363b', '#4d4d4d', '#7f8c8d', '#eff0f1', '#fcfcfc']:
        text = re.sub(re.escape(color), '#316ac5', text, flags=re.I)
    return text.encode()


def bevel(image):
    alpha = image.getchannel('A')
    stroke = max(1, image.width // 32)
    # Shift without wrapping, so border pixels cannot appear on the other side.
    def shifted(dx, dy):
        result = Image.new('L', alpha.size)
        result.paste(alpha, (dx, dy))
        return result
    highlight = ImageChops.subtract(alpha, shifted(stroke, stroke))
    shadow = ImageChops.subtract(alpha, shifted(-stroke, -stroke))
    result = image.copy()
    result.paste('#cce2ff', mask=highlight.point(lambda x: x // 2))
    result.paste('#003c74', mask=shadow.point(lambda x: x // 2))
    result.putalpha(alpha)
    return result
