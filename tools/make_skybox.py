#!/usr/bin/env python3
"""Makes a sky for a level that eurochef builds from a glTF scene: a sphere around the level
with the sky painted into its vertex colours (zenith, horizon, ground, clouds), or with a
panorama on it. eurochef takes a node named sky... as the level's sky.

    tools/make_skybox.py sky.glb                              a sky of its own, to import in Blender
    tools/make_skybox.py --into level.glb                     put into the level's file (a copy is kept)
    tools/make_skybox.py --into level.glb -o level_sky.glb    or into a new file
    tools/make_skybox.py --gui [--into level.glb]             try it out in a window, then save
    tools/make_skybox.py --preview sky.png --preset dusk      a picture of it, nothing else
    tools/make_skybox.py --list                               the presets

    --preset night --clouds 0.5 --seed 7 --horizon '#402030' --texture pano.png ...

The game draws the sky where it stands and it hides what is behind it, so the sphere is put
around the whole level: with --into its middle and size come from the level, else from
--centre and --radius. Colours are given the way they look (sRGB) and stored linear as glTF
wants them. The node is named sky_made: making it again replaces the one made before.
"""
import argparse
import copy
import json
import math
import os
import random
import shutil
import struct
import sys

SKY_NODE = 'sky_made'

# zenith, horizon, ground, clouds, how much of the sky they cover
PRESETS = {
    'day': ('#2f6fd0', '#a9cdf2', '#6f7f8f', '#ffffff', 0.35),
    'overcast': ('#7d8791', '#a7afb5', '#5f6468', '#c9ced2', 0.8),
    'dusk': ('#1c2550', '#f08a4b', '#2a2030', '#f6b08a', 0.4),
    'night': ('#03050c', '#101a33', '#05070c', '#232c44', 0.3),
    'space': ('#000000', '#020308', '#000000', '#000000', 0.0),
}


def colour_of(text):
    text = text.lstrip('#')
    if len(text) != 6:
        raise argparse.ArgumentTypeError('a colour is #RRGGBB')
    return [int(text[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]


def vector_of(text):
    parts = [float(v) for v in text.split(',')]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError('a place is x,y,z')
    return parts


def to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def mix(a, b, t):
    return [a[i] + (b[i] - a[i]) * t for i in range(3)]


def smooth(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


class Noise:
    """Value noise in space, a few octaves: the same for the same seed"""

    def __init__(self, seed):
        rng = random.Random(seed)
        self.table = [rng.random() for _ in range(4096)]
        self.offset = [rng.uniform(0, 100) for _ in range(3)]

    def cell(self, x, y, z):
        return self.table[(x * 73856093 ^ y * 19349663 ^ z * 83492791) & 4095]

    def at(self, p):
        base = [math.floor(v) for v in p]
        f = [smooth(p[i] - base[i]) for i in range(3)]
        x, y, z = base
        value = 0.0
        for dx in (0, 1):
            for dy in (0, 1):
                for dz in (0, 1):
                    weight = (f[0] if dx else 1 - f[0]) * (f[1] if dy else 1 - f[1]) * (f[2] if dz else 1 - f[2])
                    value += weight * self.cell(x + dx, y + dy, z + dz)
        return value

    def clouds(self, direction, scale):
        # looked at from below a flat layer, so they get small and dense towards the horizon
        height = max(direction[1], 0.05)
        p = [direction[0] / height * scale * 0.35 + self.offset[0], self.offset[1],
             direction[2] / height * scale * 0.35 + self.offset[2]]
        value, size, total = 0.0, 1.0, 0.0
        for _ in range(4):
            value += self.at([v / size for v in p]) * size
            total += size
            size *= 0.5
        return value / total


def sky_colour(direction, options, noise):
    up = direction[1]
    if up >= 0:
        colour = mix(options.horizon, options.zenith, up ** options.falloff)
    else:
        colour = mix(options.horizon, options.ground, smooth(-up * 6))
    if options.clouds > 0 and up > 0:
        cover = noise.clouds(direction, options.cloud_scale)
        # from the wanted share of the sky to how high the noise has to be
        edge = 1.0 - options.clouds
        amount = smooth((cover - edge * 0.75) / max(options.cloud_softness, 0.01))
        amount *= smooth(up * 8)   # none in the haze right at the horizon
        colour = mix(colour, options.cloud_colour, amount * options.cloud_opacity)
    return colour


def sphere_direction(row, column, across, down):
    """Where the vertex in a row (0: the top) and a column of the sphere is from its middle"""
    latitude = math.pi / 2 - math.pi * row / down
    longitude = 2 * math.pi * column / across
    return [math.cos(latitude) * math.cos(longitude), math.sin(latitude), math.cos(latitude) * math.sin(longitude)]


def make_sphere(options):
    """Positions, colours, texture coordinates and indices of a sphere seen from inside"""
    noise = Noise(options.seed)
    across, down = options.segments, max(options.segments // 2, 4)
    positions, colours, uvs, indices = [], [], [], []
    for row in range(down + 1):
        for column in range(across + 1):
            direction = sphere_direction(row, column, across, down)
            positions.append([options.centre[i] + direction[i] * options.radius for i in range(3)])
            colour = [1.0, 1.0, 1.0] if options.texture else sky_colour(direction, options, noise)
            colours.append([to_linear(min(1.0, max(0.0, c))) for c in colour] + [1.0])
            uvs.append([column / across, row / down])
    for row in range(down):
        for column in range(across):
            a = row * (across + 1) + column
            b, c, d = a + 1, a + across + 1, a + across + 2
            # counter clockwise from inside: the game draws a triangle from its front only
            if row > 0:
                indices += [a, c, b]
            if row < down - 1:
                indices += [b, c, d]
    return positions, colours, uvs, indices


def multiply(a, b):
    return [[sum(a[k][r] * b[c][k] for k in range(4)) for r in range(4)] for c in range(4)]


def node_matrix(node):
    if 'matrix' in node:
        m = node['matrix']
        return [m[0:4], m[4:8], m[8:12], m[12:16]]
    t = node.get('translation', [0, 0, 0])
    x, y, z, w = node.get('rotation', [0, 0, 0, 1])
    s = node.get('scale', [1, 1, 1])
    return [
        [(1 - 2 * (y * y + z * z)) * s[0], 2 * (x * y + z * w) * s[0], 2 * (x * z - y * w) * s[0], 0],
        [2 * (x * y - z * w) * s[1], (1 - 2 * (x * x + z * z)) * s[1], 2 * (y * z + x * w) * s[1], 0],
        [2 * (x * z + y * w) * s[2], 2 * (y * z - x * w) * s[2], (1 - 2 * (x * x + y * y)) * s[2], 0],
        [t[0], t[1], t[2], 1],
    ]


def is_left_out(name):
    name = (name or '').lower()
    return name.startswith(('sky', 'reference', 'ref_'))


def scene_bounds(doc):
    """The box of everything the scene has but skies and references, from the accessors' own"""
    low, high = [math.inf] * 3, [-math.inf] * 3

    def walk(index, parent):
        node = doc['nodes'][index]
        if is_left_out(node.get('name')):
            return
        matrix = multiply(parent, node_matrix(node))
        if 'mesh' in node:
            for primitive in doc['meshes'][node['mesh']]['primitives']:
                accessor = doc['accessors'][primitive['attributes']['POSITION']]
                if 'min' not in accessor:
                    continue
                for corner in range(8):
                    p = [accessor['max' if corner >> k & 1 else 'min'][k] for k in range(3)]
                    for k in range(3):
                        v = matrix[0][k] * p[0] + matrix[1][k] * p[1] + matrix[2][k] * p[2] + matrix[3][k]
                        low[k], high[k] = min(low[k], v), max(high[k], v)
        for child in node.get('children', []):
            walk(child, matrix)

    identity = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    scenes = doc.get('scenes', [])
    for root in scenes[doc.get('scene', 0)]['nodes'] if scenes else []:
        walk(root, identity)
    return (low, high) if low[0] != math.inf else None


def read_glb(path):
    data = open(path, 'rb').read()
    magic, version, _ = struct.unpack_from('<4sII', data, 0)
    if magic != b'glTF' or version != 2:
        sys.exit(f'{path} is not a binary glTF 2 file (.glb)')
    doc, binary, at = None, b'', 12
    while at < len(data):
        size, kind = struct.unpack_from('<II', data, at)
        chunk = data[at + 8:at + 8 + size]
        if kind == 0x4E4F534A:
            doc = json.loads(chunk)
        elif kind == 0x004E4942:
            binary = chunk
        at += 8 + size
    return doc, bytearray(binary)


def write_glb(path, doc, binary):
    binary = bytes(binary) + b'\0' * (-len(binary) % 4)
    doc.setdefault('buffers', [{}])[0]['byteLength'] = len(binary)
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    with open(path, 'wb') as out:
        out.write(struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(text) + 8 + len(binary)))
        out.write(struct.pack('<II', len(text), 0x4E4F534A) + text)
        out.write(struct.pack('<II', len(binary), 0x004E4942) + binary)


def add_sky(doc, binary, options):
    """Puts the sky's mesh, material and node into a document whose first buffer is `binary`"""
    positions, colours, uvs, indices = make_sphere(options)
    for key in ('bufferViews', 'accessors', 'meshes', 'materials', 'nodes'):
        doc.setdefault(key, [])
    doc.setdefault('buffers', [{'byteLength': 0}])
    if 'uri' in doc['buffers'][0]:
        sys.exit('the file keeps its data outside itself: save it as .glb')

    def view(data, target=None):
        binary.extend(b'\0' * (-len(binary) % 4))
        entry = {'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(data)}
        if target:
            entry['target'] = target
        binary.extend(data)
        doc['bufferViews'].append(entry)
        return len(doc['bufferViews']) - 1

    def accessor(data, target, component, kind, count, bounds=None):
        entry = {'bufferView': view(data, target), 'componentType': component, 'type': kind, 'count': count}
        if bounds:
            entry['min'], entry['max'] = bounds
        doc['accessors'].append(entry)
        return len(doc['accessors']) - 1

    bounds = ([min(p[k] for p in positions) for k in range(3)], [max(p[k] for p in positions) for k in range(3)])
    attributes = {
        'POSITION': accessor(b''.join(struct.pack('<3f', *p) for p in positions), 34962, 5126, 'VEC3',
                             len(positions), bounds),
        'COLOR_0': accessor(b''.join(struct.pack('<4f', *c) for c in colours), 34962, 5126, 'VEC4', len(colours)),
        'TEXCOORD_0': accessor(b''.join(struct.pack('<2f', *t) for t in uvs), 34962, 5126, 'VEC2', len(uvs)),
    }
    wide = len(positions) > 65535
    index_data = struct.pack('<%d%s' % (len(indices), 'I' if wide else 'H'), *indices)
    index_accessor = accessor(index_data, 34963, 5125 if wide else 5123, 'SCALAR', len(indices))

    material = {'name': 'sky_nocollide', 'doubleSided': False,
                'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0, 'roughnessFactor': 1}}
    if options.texture:
        kind = {'.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg'}.get(
            os.path.splitext(options.texture)[1].lower())
        if not kind:
            sys.exit('the panorama has to be a .png or a .jpg')
        for key in ('images', 'textures', 'samplers'):
            doc.setdefault(key, [])
        doc['images'].append({'name': 'sky_panorama', 'mimeType': kind,
                              'bufferView': view(open(options.texture, 'rb').read())})
        doc['samplers'].append({'wrapS': 10497, 'wrapT': 33071})
        doc['textures'].append({'source': len(doc['images']) - 1, 'sampler': len(doc['samplers']) - 1})
        material['pbrMetallicRoughness']['baseColorTexture'] = {'index': len(doc['textures']) - 1}
    doc['materials'].append(material)
    doc['meshes'].append({'name': SKY_NODE, 'primitives': [
        {'attributes': attributes, 'indices': index_accessor, 'material': len(doc['materials']) - 1}]})
    doc['nodes'].append({'name': SKY_NODE, 'mesh': len(doc['meshes']) - 1})

    if not doc.get('scenes'):
        doc['scenes'], doc['scene'] = [{'nodes': []}], 0
    roots = doc['scenes'][doc.get('scene', 0)].setdefault('nodes', [])
    # a sky made before goes out of the scene (its data stays in the file, unused)
    roots[:] = [n for n in roots if doc['nodes'][n].get('name') != SKY_NODE]
    roots.append(len(doc['nodes']) - 1)
    return len(indices) // 3


def hex_of(colour):
    return '#%02x%02x%02x' % tuple(round(min(1.0, max(0.0, c)) * 255) for c in colour)


def apply_preset(options, name, keep=()):
    """Sets the colours and the clouds of a preset, but for the ones named in `keep`"""
    zenith, horizon, ground, cloud, share = PRESETS[name]
    options.preset = name
    for key, value in (('zenith', colour_of(zenith)), ('horizon', colour_of(horizon)), ('ground', colour_of(ground)),
                       ('cloud_colour', colour_of(cloud)), ('clouds', share)):
        if key not in keep:
            setattr(options, key, value)


def command_for(options):
    """The command line that makes the sky the options describe"""
    words = ['tools/make_skybox.py']
    if options.into:
        words += ['--into', options.into]
        if options.into_output:
            words += ['-o', options.into_output]
    elif options.output:
        words.append(options.output)
    if options.texture:
        words += ['--texture', options.texture]
    else:
        words += ['--preset', options.preset, '--zenith', f"'{hex_of(options.zenith)}'",
                  '--horizon', f"'{hex_of(options.horizon)}'", '--ground', f"'{hex_of(options.ground)}'",
                  '--falloff', f'{options.falloff:.2f}', '--clouds', f'{options.clouds:.2f}']
        if options.clouds > 0:
            words += ['--cloud-colour', f"'{hex_of(options.cloud_colour)}'", '--cloud-scale',
                      f'{options.cloud_scale:.2f}', '--cloud-softness', f'{options.cloud_softness:.2f}',
                      '--cloud-opacity', f'{options.cloud_opacity:.2f}', '--seed', str(options.seed)]
    words += ['--segments', str(options.segments)]
    if options.radius:
        words += ['--radius', f'{options.radius:g}']
    if options.centre:
        words += ['--centre', ','.join(f'{v:g}' for v in options.centre)]
    return ' '.join(words)


def write_sky(options):
    """Writes the sky the options describe, into the level or on its own. Returns what to say"""
    options = copy.copy(options)
    said = []
    if options.into:
        doc, binary = read_glb(options.into)
        bounds = scene_bounds(doc)
        if bounds and not options.centre:
            options.centre = [(bounds[0][k] + bounds[1][k]) / 2 for k in range(3)]
        if bounds and not options.radius:
            options.radius = max(math.dist(bounds[0], bounds[1]) / 2 * options.margin, 50.0)
        target = options.into_output or options.output or options.into
    else:
        doc, binary, target = {'asset': {'version': '2.0', 'generator': 'make_skybox.py'}}, bytearray(), options.output
    options.centre = options.centre or [0.0, 0.0, 0.0]
    options.radius = options.radius or 300.0

    triangles = add_sky(doc, binary, options)
    if options.into and os.path.abspath(target) == os.path.abspath(options.into):
        kept = os.path.splitext(options.into)[0] + '.before_sky.glb'
        if not os.path.exists(kept):
            shutil.copyfile(options.into, kept)
            said.append(f'the level as it was: {kept}')
    write_glb(target, doc, binary)
    said.append(f'{target}: a sky of {triangles} triangles, radius {options.radius:.0f} around '
                f'{options.centre[0]:.1f} {options.centre[1]:.1f} {options.centre[2]:.1f}')
    return '\n'.join(said)


PANORAMA_SIZE = (1024, 512)


def panorama_of(options):
    """The sky unrolled, 2:1: what the sphere's vertices are painted with, blended between them
    as the game does between a triangle's corners"""
    from PIL import Image
    if options.texture:
        return Image.open(options.texture).convert('RGB').resize(PANORAMA_SIZE, Image.BILINEAR)
    noise = Noise(options.seed)
    across, down = options.segments, max(options.segments // 2, 4)
    pixels = []
    for row in range(down + 1):
        for column in range(across + 1):
            colour = sky_colour(sphere_direction(row, column, across, down), options, noise)
            pixels.append(tuple(round(min(1.0, max(0.0, c)) * 255) for c in colour))
    grid = Image.new('RGB', (across + 1, down + 1))
    grid.putdata(pixels)
    return grid.resize(PANORAMA_SIZE, Image.BILINEAR)


def view_of(panorama, size, yaw, pitch, fov=75.0):
    """What a player sees of the sky looking `yaw` around and `pitch` up (degrees): the game's
    picture, which is the mirror image of the scene's"""
    from PIL import Image
    width, height = panorama.size
    wide = Image.new('RGB', (width * 3, height))
    for k in range(3):
        wide.paste(panorama, (width * k, 0))
    across, down = 48, 27
    reach = math.tan(math.radians(fov) / 2)
    cos_p, sin_p = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    cos_y, sin_y = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))

    def source(i, j):
        x = (i / across * 2 - 1) * reach
        y = (1 - j / down * 2) * reach * size[1] / size[0]
        y, z = y * cos_p + sin_p, cos_p - y * sin_p
        x, z = x * cos_y + z * sin_y, z * cos_y - x * sin_y
        length = math.sqrt(x * x + y * y + z * z)
        # the game's x is the scene's turned around
        longitude = math.atan2(z, -x) % (2 * math.pi)
        return longitude / (2 * math.pi), (math.pi / 2 - math.asin(y / length)) / math.pi

    corners = [[source(i, j) for i in range(across + 1)] for j in range(down + 1)]
    mesh = []
    for j in range(down):
        for i in range(across):
            quad = [corners[j][i], corners[j + 1][i], corners[j + 1][i + 1], corners[j][i + 1]]
            # the four corners on the same turn of the panorama as the first
            first = quad[0][0]
            flat = []
            for u, v in quad:
                u += round(first - u)
                flat += [(u + 1) * width, v * (height - 1)]
            box = (i * size[0] // across, j * size[1] // down, (i + 1) * size[0] // across, (j + 1) * size[1] // down)
            mesh.append((box, flat))
    return wide.transform(size, Image.MESH, mesh, Image.BILINEAR)


class SkyWindow:
    """A window to try a sky out in: the picture a player has of it, dragged to look around,
    and everything that can be set. `shown` False builds it without putting it on the screen"""

    SLIDERS = [
        ('clouds', 'Clouds', 0.0, 1.0, 0.01),
        ('cloud_scale', 'Cloud size (larger: smaller)', 0.5, 10.0, 0.1),
        ('cloud_softness', 'Cloud softness', 0.02, 0.6, 0.01),
        ('cloud_opacity', 'Cloud opacity', 0.0, 1.0, 0.01),
        ('falloff', 'Horizon falloff', 0.2, 3.0, 0.05),
        ('seed', 'Seed', 1, 500, 1),
        ('segments', 'Segments', 16, 128, 4),
    ]
    COLOURS = [('zenith', 'Zenith'), ('horizon', 'Horizon'), ('ground', 'Ground'), ('cloud_colour', 'Clouds')]
    VIEW = (640, 360)

    def __init__(self, options, shown=True):
        import tkinter as tk
        from tkinter import ttk
        self.tk, self.options = tk, options
        self.yaw, self.pitch = 0.0, 20.0
        self.panorama = self.picture = self.pending = self.drag = None
        self.root = tk.Tk()
        self.root.title('make_skybox')
        if not shown:
            self.root.withdraw()

        left = ttk.Frame(self.root, padding=6)
        left.grid(row=0, column=0, sticky='n')
        self.view = tk.Label(left, cursor='fleur')
        self.view.grid(row=0, column=0)
        self.view.bind('<ButtonPress-1>', self.drag_start)
        self.view.bind('<B1-Motion>', self.drag_move)
        ttk.Label(left, text='Drag to look around. Below: the whole sky, unrolled.').grid(row=1, column=0, sticky='w')
        self.strip = tk.Label(left)
        self.strip.grid(row=2, column=0)

        right = ttk.Frame(self.root, padding=6)
        right.grid(row=0, column=1, sticky='n')
        row = 0
        ttk.Label(right, text='Preset').grid(row=row, column=0, sticky='w')
        self.preset = tk.StringVar(value=options.preset)
        ttk.OptionMenu(right, self.preset, options.preset, *sorted(PRESETS), command=self.preset_chosen).grid(
            row=row, column=1, sticky='ew')
        self.swatches = {}
        for key, label in self.COLOURS:
            row += 1
            ttk.Label(right, text=label).grid(row=row, column=0, sticky='w')
            swatch = tk.Button(right, width=12, relief='groove', command=lambda key=key: self.colour_chosen(key))
            swatch.grid(row=row, column=1, sticky='ew', pady=1)
            self.swatches[key] = swatch
        self.scales = {}
        for key, label, low, high, step in self.SLIDERS:
            row += 1
            ttk.Label(right, text=label).grid(row=row, column=0, sticky='w')
            scale = tk.Scale(right, from_=low, to=high, resolution=step, orient='horizontal', length=220,
                             command=lambda value, key=key: self.slider_moved(key, value))
            scale.set(getattr(options, key))
            scale.grid(row=row, column=1, sticky='ew')
            self.scales[key] = scale
        row += 1
        into = f'Put into {os.path.basename(options.into)}' if options.into else 'Put into a level...'
        ttk.Button(right, text=into, command=self.save_into).grid(row=row, column=0, columnspan=2, sticky='ew', pady=(8, 1))
        row += 1
        ttk.Button(right, text='Save as a sky of its own...', command=self.save_alone).grid(
            row=row, column=0, columnspan=2, sticky='ew')
        row += 1
        ttk.Label(right, text='The same from the command line:').grid(row=row, column=0, columnspan=2, sticky='w', pady=(8, 0))
        row += 1
        self.command = tk.Text(right, width=44, height=6, wrap='word')
        self.command.grid(row=row, column=0, columnspan=2)
        row += 1
        self.status = ttk.Label(right, text='', wraplength=330, justify='left')
        self.status.grid(row=row, column=0, columnspan=2, sticky='w')
        if options.texture:
            self.status.config(text=f'The sky is the panorama {options.texture}: the colours and the clouds do nothing.')
        self.show_colours()
        self.repaint()

    def show_colours(self):
        for key, swatch in self.swatches.items():
            colour = getattr(self.options, key)
            dark = sum(colour) < 1.5
            swatch.config(bg=hex_of(colour), activebackground=hex_of(colour), text=hex_of(colour),
                          fg='white' if dark else 'black')

    def preset_chosen(self, name):
        apply_preset(self.options, name)
        self.scales['clouds'].set(self.options.clouds)
        self.show_colours()
        self.repaint_soon()

    def colour_chosen(self, key):
        from tkinter import colorchooser
        picked = colorchooser.askcolor(color=hex_of(getattr(self.options, key)), parent=self.root)[1]
        if picked:
            setattr(self.options, key, colour_of(picked))
            self.show_colours()
            self.repaint_soon()

    def slider_moved(self, key, value):
        value = int(float(value)) if key in ('seed', 'segments') else float(value)
        if getattr(self.options, key) != value:
            setattr(self.options, key, value)
            self.repaint_soon()

    def repaint_soon(self):
        # not on every step of a slider that is being dragged
        if self.pending:
            self.root.after_cancel(self.pending)
        self.pending = self.root.after(80, self.repaint)

    def repaint(self):
        self.pending = None
        self.panorama = panorama_of(self.options)
        self.show_view()
        from PIL import ImageTk
        self.strip_picture = ImageTk.PhotoImage(self.panorama.resize((self.VIEW[0], self.VIEW[0] // 4)))
        self.strip.config(image=self.strip_picture)
        self.command.delete('1.0', 'end')
        self.command.insert('1.0', command_for(self.options))

    def show_view(self):
        from PIL import ImageTk
        self.picture = ImageTk.PhotoImage(view_of(self.panorama, self.VIEW, self.yaw, self.pitch))
        self.view.config(image=self.picture)

    def drag_start(self, event):
        self.drag = (event.x, event.y, self.yaw, self.pitch)

    def drag_move(self, event):
        if self.drag:
            x, y, yaw, pitch = self.drag
            self.yaw = (yaw - (event.x - x) * 0.25) % 360
            self.pitch = min(89.0, max(-89.0, pitch + (event.y - y) * 0.25))
            self.show_view()

    def save(self, options):
        try:
            said = write_sky(options)
        except (OSError, SystemExit, ValueError, KeyError) as error:
            said = f'Not written: {error}'
        print(said)
        self.status.config(text=said)

    def save_into(self):
        from tkinter import filedialog
        options = copy.copy(self.options)
        if not options.into:
            options.into = filedialog.askopenfilename(parent=self.root, title='The level to put the sky into',
                                                      filetypes=[('binary glTF', '*.glb')])
            if not options.into:
                return
            options.into_output = options.output = None
        self.save(options)

    def save_alone(self):
        from tkinter import filedialog
        options = copy.copy(self.options)
        options.into = options.into_output = None
        options.output = filedialog.asksaveasfilename(parent=self.root, title='Save the sky as', defaultextension='.glb',
                                                      filetypes=[('binary glTF', '*.glb')])
        if options.output:
            self.save(options)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('output', nargs='?', help='the .glb to write (with --into: the level with the sky in it)')
    parser.add_argument('--into', metavar='LEVEL.glb', help='a level to put the sky into and around')
    parser.add_argument('-o', dest='into_output', metavar='OUT.glb', help='with --into: write here, not over the level')
    parser.add_argument('--list', action='store_true', help='list the presets')
    parser.add_argument('--gui', action='store_true', help='a window to try the sky out in before it is saved')
    parser.add_argument('--preview', metavar='PICTURE.png', help='write a picture of the sky, and no sky')
    parser.add_argument('--look', type=float, nargs=2, default=[0.0, 20.0], metavar=('AROUND', 'UP'),
                        help='with --preview: where the picture looks, in degrees (0 20)')
    parser.add_argument('--preset', choices=sorted(PRESETS), default='day')
    parser.add_argument('--zenith', type=colour_of, help='the colour straight up, #RRGGBB')
    parser.add_argument('--horizon', type=colour_of, help='the colour at the horizon')
    parser.add_argument('--ground', type=colour_of, help='the colour below the horizon')
    parser.add_argument('--falloff', type=float, default=0.6,
                        help='how soon the horizon gives way to the zenith: below 1 soon, above 1 late (0.6)')
    parser.add_argument('--clouds', type=float, help='how much of the sky the clouds take, 0 to 1 (0: none)')
    parser.add_argument('--cloud-colour', type=colour_of)
    parser.add_argument('--cloud-scale', type=float, default=3.0, help='larger: smaller clouds (3)')
    parser.add_argument('--cloud-softness', type=float, default=0.18, help='how soft their edges are (0.18)')
    parser.add_argument('--cloud-opacity', type=float, default=0.9, help='how much of the sky they hide (0.9)')
    parser.add_argument('--seed', type=int, default=1, help='another number, other clouds')
    parser.add_argument('--texture', metavar='PANORAMA', help='a .png or .jpg, 2:1, in place of the painted sky')
    parser.add_argument('--radius', type=float, help='of the sphere (with --into: from the level, else 300)')
    parser.add_argument('--centre', type=vector_of, help='of the sphere, x,y,z in the scene (with --into: the level\'s)')
    parser.add_argument('--margin', type=float, default=2.5, help='with --into: the level\'s size times this (2.5)')
    parser.add_argument('--segments', type=int, default=64, help='around the sphere, half of it down (64)')
    options = parser.parse_args()

    if options.list:
        for name, (zenith, horizon, ground, cloud, share) in PRESETS.items():
            print(f'{name:9} zenith {zenith} horizon {horizon} ground {ground} clouds {cloud} over {share}')
        return
    given = [key for key in ('zenith', 'horizon', 'ground', 'cloud_colour', 'clouds') if getattr(options, key) is not None]
    apply_preset(options, options.preset, keep=given)
    options.clouds = min(1.0, max(0.0, options.clouds))
    options.segments = min(max(options.segments, 8), 250)

    if options.gui or options.preview:
        try:
            import PIL  # noqa: F401
        except ImportError:
            sys.exit('the preview needs Pillow (pip install pillow)')
    if options.preview:
        view_of(panorama_of(options), (960, 540), options.look[0], options.look[1]).save(options.preview)
        print(f'{options.preview}: the sky as a player sees it')
        return
    if options.gui:
        SkyWindow(options).root.mainloop()
        return
    if not options.into and not options.output:
        parser.error('name the .glb to write, or a level with --into, or try --gui')
    print(write_sky(options))


if __name__ == '__main__':
    main()
