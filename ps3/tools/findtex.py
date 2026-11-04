#!/usr/bin/env python3
# findtex.py -- which textures a model asks for that its own TXD doesn't have,
# and which TXDs of the image do have them (res-PS3 patch 82).
#
#   python3 ps3/tools/findtex.py <USRDIR or the folder with models/ and data/> [model ...]
#
# Reads models/gta3.img + gta3.dir (the .dir is 32-byte entries: offset and
# size in 2048-byte sectors, little-endian, then the name), the IDE files
# under data/ (which TXD each model uses), the texture names each DFF asks
# for (RW Texture chunks) and the names each TXD holds. With model names,
# only those; without, every model of the image. Prints one line per model
# with a missing texture, then a summary of the TXDs that hold the missing
# ones. The game itself never reads this.
import os, struct, sys

def find_ci(root, *parts):
    """root/parts... with any letter case (the data comes from Windows)"""
    cur = root
    for p in parts:
        if not os.path.isdir(cur):
            return None
        hit = [e for e in os.listdir(cur) if e.lower() == p.lower()]
        if not hit:
            return None
        cur = os.path.join(cur, hit[0])
    return cur

def read_dir(dirpath):
    entries = {}
    with open(dirpath, 'rb') as f:
        data = f.read()
    for i in range(0, len(data) - 31, 32):
        off, size = struct.unpack_from('<II', data, i)
        name = data[i+8:i+32].split(b'\0')[0].decode('latin-1')
        entries[name.lower()] = (off * 2048, size * 2048)
    return entries

def read_ides(dataroot):
    """model name -> txd name, from every .ide under data/"""
    txdof = {}
    for dp, dn, fn in os.walk(dataroot):
        for n in fn:
            if not n.lower().endswith('.ide'):
                continue
            section = None
            with open(os.path.join(dp, n), 'r', encoding='latin-1') as f:
                for line in f:
                    line = line.split('#')[0].strip()
                    if not line:
                        continue
                    low = line.lower()
                    if low in ('objs', 'tobj', 'peds', 'cars', 'weap', 'hier', 'anim', 'path', '2dfx', 'txdp', 'mlo'):
                        section = low
                        continue
                    if low == 'end':
                        section = None
                        continue
                    if section in ('objs', 'tobj', 'peds', 'cars', 'weap', 'hier', 'anim'):
                        f2 = [x.strip() for x in line.split(',')]
                        if len(f2) >= 3:
                            txdof[f2[1].lower()] = f2[2].lower()
    return txdof

def dff_textures(buf):
    """texture names in a DFF: a Texture chunk (6) holds a Struct (1) of 4
    bytes and then a String (2) with the name"""
    names = set()
    i = 0
    n = len(buf)
    while True:
        i = buf.find(b'\x06\x00\x00\x00', i)
        if i < 0 or i + 36 > n:
            break
        t1, s1 = struct.unpack_from('<II', buf, i + 12)
        if t1 == 1 and s1 == 4:
            t2, s2 = struct.unpack_from('<II', buf, i + 28)
            if t2 == 2 and 0 < s2 < 64 and i + 40 + s2 <= n:
                name = buf[i+40:i+40+s2].split(b'\0')[0].decode('latin-1')
                if name:
                    names.add(name.lower())
        i += 4
    return names

def txd_names(buf):
    """texture names in a TXD: each Texture Native (0x15) has a Struct whose
    D3D8/D3D9 layout puts the 32-byte name after platform id and filter"""
    names = set()
    i = 0
    n = len(buf)
    while True:
        i = buf.find(b'\x15\x00\x00\x00', i)
        if i < 0 or i + 12 + 12 + 8 + 32 > n:
            break
        t1 = struct.unpack_from('<I', buf, i + 12)[0]
        if t1 == 1:
            name = buf[i+32:i+64].split(b'\0')[0].decode('latin-1')
            if name and all(32 < ord(c) < 127 for c in name):
                names.add(name.lower())
        i += 4
    return names

def main():
    if len(sys.argv) < 2:
        print(__doc__.strip().split('\n')[2])
        sys.exit(1)
    root = sys.argv[1]
    want = [m.lower() for m in sys.argv[2:]]
    img = find_ci(root, 'models', 'gta3.img')
    dr = find_ci(root, 'models', 'gta3.dir')
    data = find_ci(root, 'data')
    if not img or not dr or not data:
        print('need models/gta3.img, models/gta3.dir and data/ under', root)
        sys.exit(1)
    entries = read_dir(dr)
    txdof = read_ides(data)
    with open(img, 'rb') as f:
        def get(name):
            off, size = entries[name]
            f.seek(off)
            return f.read(size)
        txds = {}
        for name in entries:
            if name.endswith('.txd'):
                txds[name[:-4]] = txd_names(get(name))
        holders = {}
        for t, ns in txds.items():
            for x in ns:
                holders.setdefault(x, []).append(t)
        models = want or sorted(n[:-4] for n in entries if n.endswith('.dff'))
        missing_total = 0
        where = {}
        for m in models:
            if m + '.dff' not in entries:
                print(f'{m}: not in the image')
                continue
            txd = txdof.get(m)
            if txd is None:
                print(f'{m}: not in any IDE')
                continue
            own = txds.get(txd)
            if own is None:
                print(f'{m}: its TXD "{txd}" is not in the image')
                continue
            miss = sorted(dff_textures(get(m + '.dff')) - own)
            if not miss:
                if want:
                    print(f'{m}: all its textures are in "{txd}"')
                continue
            missing_total += 1
            parts = []
            for x in miss:
                h = holders.get(x, [])
                for t in h:
                    where[t] = where.get(t, 0) + 1
                parts.append(f'{x} -> ' + (', '.join(h[:4]) + (' ...' if len(h) > 4 else '') if h else 'in no TXD'))
            print(f'{m} (TXD "{txd}"): ' + '; '.join(parts))
        print(f'-- {missing_total} models with textures not in their own TXD')
        if where:
            top = sorted(where.items(), key=lambda kv: -kv[1])[:15]
            print('-- TXDs that hold them (how many missing textures each): ' + ', '.join(f'{t} {c}' for t, c in top))

main()
