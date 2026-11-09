"""Convert the GLSL of a PPCGPU_RECORD directory recorded before R300_GLSL_FB_IMAGE to the storage-image form:
   python3 tools/conv_glsl.py <recdir> <newdir>   (draws.bin is linked, textures too)"""
import os, re, sys
src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst + '/blobs', exist_ok=True)
if not os.path.exists(dst + '/draws.bin'): os.symlink(os.path.abspath(src + '/draws.bin'), dst + '/draws.bin')
FMT = {1: 'r8ui', 2: 'r16ui', 4: 'r32ui', 8: 'rg32ui', 16: 'rgba32ui'}
def conv(t):
    out = []
    bytes_by_k = {}
    for m in re.finditer(r'// colour buffer (\d+): format \d+, US_OUT_FMT \w+, endian \d+, (\d+) bytes', t):
        bytes_by_k[int(m.group(1))] = int(m.group(2))
    # colour buffers
    def att(m):
        idx, bind, u, name = m.group(1), m.group(2), m.group(3), m.group(4)
        if name == 'zin':
            return m.group(0)
        k = int(name[4:])
        if not u:
            return 'layout(set = 0, binding = %s, rgba8) coherent uniform image2D %s;' % (bind, name)
        return 'layout(set = 0, binding = %s, %s) coherent uniform uimage2D %s;' % (bind, FMT[bytes_by_k.get(k, 4)], name)
    t = re.sub(r'layout\(input_attachment_index = (\d+), set = 0, binding = (\d+)\) uniform (u?)subpassInput (\w+);', att, t)
    t = re.sub(r'#define R300_FB(\d+)_LOAD subpassLoad\(fbin\1\)', r'#define R300_FB\1_LOAD imageLoad(fbin\1, ivec2(gl_FragCoord.xy))', t)
    t = re.sub(r'layout\(location = \d+\) out (r300_cb\d+_o) (o_c\d+);', r'\1 \2;', t)
    nt = len(re.findall(r'layout\(set = 0, binding = \d+, \w+\) coherent uniform u?image2D fbin\d+;', t))
    zm = re.search(r'layout\(input_attachment_index = \d+, set = 0, binding = (\d+)\) uniform usubpassInput zin;\nlayout\(location = \d+\) out uvec4 o_z;\n', t)
    if zm:
        b = zm.group(1)
        t = t.replace(zm.group(0), '#ifdef R300_Z16\nlayout(set = 0, binding = %s, r16ui) coherent uniform uimage2D zin;\n#else\nlayout(set = 0, binding = %s, r32ui) coherent uniform uimage2D zin;\n#endif\nuvec4 o_z;\n' % (b, b))
        t = t.replace('uint zb = subpassLoad(zin).x;', 'uint zb = imageLoad(zin, ivec2(gl_FragCoord.xy)).x;')
    if '/*R300_END*/' in t and nt:
        st = '    if (!r300_dead) {\n' + ''.join('        imageStore(fbin%d, ivec2(gl_FragCoord.xy), o_c%d);\n' % (k, k) for k in range(nt))
        st += '#ifdef R300_FS_Z\n        imageStore(zin, ivec2(gl_FragCoord.xy), o_z);\n#endif\n    }\n'
        t = t.replace('    /*R300_END*/', st + '    /*R300_END*/')
    return t
n = 0
for f in os.listdir(src + '/blobs'):
    p = src + '/blobs/' + f
    if os.path.getsize(p) < 4000000:
        b = open(p, 'rb').read()
        if b.startswith(b'#version'):
            open(dst + '/blobs/' + f, 'w').write(conv(b.decode())); n += 1; continue
    if not os.path.exists(dst + '/blobs/' + f): os.symlink(p, dst + '/blobs/' + f)
print(n, 'programs converted')
