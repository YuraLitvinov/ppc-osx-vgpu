"""Check a PPCGPU_RECORD directory: python3 tools/rec_info.py <dir>"""
import os, struct, sys, collections
d = sys.argv[1]
b = open(d + '/draws.bin', 'rb').read()
assert b[:8] == b'R300REC2', b[:8]
szp, szv, szu, ntex = struct.unpack_from('<4I', b, 8)
print('sizeof packet', szp, 'vertex', szv, 'uniforms', szu, 'ntex', ntex)
o = 24
n = 0
verts = 0
progs = collections.Counter()
missing = 0
while o < len(b):
    tag, num = struct.unpack_from('<IQ', b, o); o += 12
    assert tag == 0xD4A70001, (hex(tag), o)
    gid, gl = struct.unpack_from('<QI', b, o); o += 12
    progs[gid] += 1
    if not os.path.exists('%s/blobs/%016x' % (d, gid)):
        missing += 1
    o += szp
    nv, = struct.unpack_from('<I', b, o); o += 4
    verts += nv; o += nv * szv
    for t in range(ntex):
        bid, ln = struct.unpack_from('<QI', b, o); o += 12
        if bid and not os.path.exists('%s/blobs/%016x' % (d, bid)):
            missing += 1
    n += 1
print('draws', n, 'verts', verts, 'distinct programs', len(progs), 'missing blobs', missing,
      'blobs', len(os.listdir(d + '/blobs')), 'bytes parsed', o, 'of', len(b))
