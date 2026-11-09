"""Index a PPCGPU_RECORD directory (writes index.pkl): python3 tools/rec_load.py <dir>"""
import numpy as np, struct, os, pickle, sys
d = sys.argv[1]
mm = np.memmap(d + '/draws.bin', dtype=np.uint8, mode='r')
szp, szv, szu, ntex = struct.unpack_from('<4I', mm, 8)
o = 24; N = len(mm); rows = []
while o < N:
    tag, num = struct.unpack_from('<IQ', mm, o); o += 12
    gid, gl = struct.unpack_from('<QI', mm, o); o += 12
    p = o; o += szp
    g = lambda off, f='<I': struct.unpack_from(f, mm, p + off)[0]
    nv_tot, = struct.unpack_from('<I', mm, o); o += 4
    voff = o; o += nv_tot * szv
    tx = []
    for t in range(ntex):
        bid, ln = struct.unpack_from('<QI', mm, o); o += 12; tx.append(bid)
    rows.append(dict(n=num, gid=gid, voff=voff, nv=g(5936), nl=g(5940), prim=g(5944), cull=g(148),
                     zatt=g(128), zinfo=struct.unpack_from('<4I', mm, p + 1368), rtw=g(8), rth=g(12),
                     cblend=g(1256), alpha=g(1268), tex=tx, warn=g(5948), ncb=g(44),
                     zbpp=g(128 + 12), tex0=[(g(2472 + 216 * t + 4), g(2472 + 216 * t + 8)) for t in range(2)]))
pickle.dump(rows, open(d + '/index.pkl', 'wb'))
print(len(rows), 'draws indexed')
