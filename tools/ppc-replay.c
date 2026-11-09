/*
 * Replay recorded R300 draws (PPCGPU_RECORD, format R300REC2) through the real
 * Vulkan backend, with no guest.  Starts from a cleared colour and depth
 * buffer, draws [lo, hi), flushes, and writes the last draw's colour buffer.
 *
 *   ppc-replay <recdir> <lo> <hi> <out.ppm>
 *
 * lo and hi are draw numbers within the recording (0-based).  Environment:
 *   REPLAY_NOZ=1       draw without the depth test
 *   REPLAY_SPLIT=1     one draw per triangle (a barrier between triangles)
 *   REPLAY_SHADE=tex|v1|v2   reduce a 'tex * v1 + v2' program to one term
 * plus the backend's own PPCGPU_* switches.  Build: tools/build-replay.sh
 *
 * The recorded GLSL is replayed as it is: recordings made before
 * R300_GLSL_FB_IMAGE need tools/conv_glsl.py first.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ppc_mac_gpu_renderer.h"
#include "r300/r300_draw.h"

void *ppc_mac_gpu_vulkan_alloc_vram(uint64_t size, void **opaque_out);
const char *ppc_mac_gpu_vulkan_error(void);
PPCMacGPURenderer *ppc_mac_gpu_renderer_vulkan(void);

#define NTEX R300_NUM_TEX_UNITS

static uint8_t *mapf(const char *path, size_t *len)
{
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) {
        return NULL;
    }
    *len = st.st_size;
    void *p = mmap(NULL, st.st_size ? st.st_size : 1, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return p == MAP_FAILED ? NULL : p;
}

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

typedef struct Draw {
    uint64_t num;
    const uint8_t *glsl; uint32_t glsl_len; uint64_t glsl_id;
    const uint8_t *pkt;
    const uint8_t *verts; uint32_t nv;
    uint64_t tex_id[NTEX]; uint32_t tex_len[NTEX];
} Draw;

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: ppc-replay dir lo hi out.ppm\n"); return 2; }
    const char *dir = argv[1];
    long lo = atol(argv[2]), hi = atol(argv[3]);
    char path[1024];
    size_t len;
    snprintf(path, sizeof path, "%s/draws.bin", dir);
    const uint8_t *b = mapf(path, &len);
    if (!b || memcmp(b, "R300REC2", 8)) { fprintf(stderr, "bad recording\n"); return 1; }
    uint32_t szp = rd32(b + 8), szv = rd32(b + 12), ntex = rd32(b + 20);
    if (szp != sizeof(R300DrawPacket) || szv != sizeof(R300Vertex) || ntex != NTEX) {
        fprintf(stderr, "struct size mismatch: %u/%zu %u/%zu %u/%d\n", szp, sizeof(R300DrawPacket), szv, sizeof(R300Vertex), ntex, NTEX);
        return 1;
    }

    uint64_t vram_size = 128ull << 20;
    void *opaque = NULL;
    uint8_t *vram = ppc_mac_gpu_vulkan_alloc_vram(vram_size, &opaque);
    if (!vram) { fprintf(stderr, "alloc_vram: %s\n", ppc_mac_gpu_vulkan_error()); return 1; }
    PPCMacGPURenderer *r = ppc_mac_gpu_renderer_vulkan();
    void *ctx = r->init(vram, vram_size);
    if (!ctx) { fprintf(stderr, "init: %s\n", ppc_mac_gpu_vulkan_error()); return 1; }

    const uint8_t *o = b + 24, *end = b + len;
    long k = 0, drawn = 0;
    R300DrawPacket last = { 0 };
    uint64_t last_cleared_rt = ~0ull;
    while (o < end) {
        Draw d;
        d.num = rd64(o + 4);
        o += 12;
        d.glsl_id = rd64(o); d.glsl_len = rd32(o + 8); o += 12;
        d.pkt = o; o += szp;
        d.nv = rd32(o); o += 4;
        d.verts = o; o += (size_t)d.nv * szv;
        for (int t = 0; t < NTEX; t++) { d.tex_id[t] = rd64(o); d.tex_len[t] = rd32(o + 8); o += 12; }
        if (k < lo || k >= hi) { k++; continue; }
        k++;

        R300DrawPacket pkt;
        memcpy(&pkt, d.pkt, sizeof pkt);

        /* glsl text */
        snprintf(path, sizeof path, "%s/blobs/%016llx", dir, (unsigned long long)d.glsl_id);
        size_t gl; uint8_t *g = mapf(path, &gl);
        char *glsl = malloc(gl + 1); memcpy(glsl, g, gl); glsl[gl] = 0; munmap(g, gl);
        pkt.glsl = glsl;
        {
            const char *m = getenv("REPLAY_SHADE");
            const char *needle = "vec3 rr = rA * rB + rC;";
            char *at = m ? strstr(glsl, needle) : NULL;
            if (at) {
                const char *rep = !strcmp(m, "tex") ? "vec3 rr = rA;          " :
                                  !strcmp(m, "v1")  ? "vec3 rr = rB;          " :
                                  !strcmp(m, "v2")  ? "vec3 rr = rC;          " : NULL;
                if (rep) memcpy(at, rep, strlen(needle) < strlen(rep) ? strlen(needle) : strlen(rep));
            }
        }
        pkt.verts = (R300Vertex *)d.verts;
        for (int t = 0; t < NTEX; t++) {
            R300TexDesc *td = &pkt.tex[t];
            uint8_t *host = NULL;
            if (td->bound && d.tex_id[t]) {
                snprintf(path, sizeof path, "%s/blobs/%016llx", dir, (unsigned long long)d.tex_id[t]);
                size_t tl; uint8_t *tb = mapf(path, &tl);
                bool had_host = td->host_data != NULL;   /* the recorded pointer says: GART copy */
                if (had_host) {
                    host = malloc(tl); memcpy(host, tb, tl);
                } else if ((uint64_t)td->gpu_addr + tl <= vram_size) {
                    memcpy(vram + td->gpu_addr, tb, tl);
                }
                munmap(tb, tl);
            }
            td->host_data = host;
        }
        /* clear colour and depth once per distinct render target */
        uint64_t key = ((uint64_t)pkt.rt_gpu_addr << 32) | pkt.rt_pitch;
        if (key != last_cleared_rt) {
            uint64_t n = (uint64_t)pkt.rt_pitch * pkt.rt_height * pkt.rt_bpp;
            if (pkt.rt_gpu_addr + n <= vram_size) {
                memset(vram + pkt.rt_gpu_addr, 0x40, n);
            }
            if (pkt.depth.attach && pkt.depth.gpu_addr + (uint64_t)pkt.depth.pitch * pkt.rt_height * pkt.depth.bpp <= vram_size) {
                uint32_t *z = (uint32_t *)(vram + pkt.depth.gpu_addr);
                /* Z24S8 far, stored with DEPTHENDIAN 2 (bytes swapped): 0xFFFFFF00 -> 00 FF FF FF in memory order */
                for (uint64_t i = 0; i < (uint64_t)pkt.depth.pitch * pkt.rt_height; i++) z[i] = 0x00FFFFFFu;
            }
            last_cleared_rt = key;
        }
        if (getenv("REPLAY_NOZ")) pkt.depth.attach = false;
        int rc;
        if (getenv("REPLAY_SPLIT") && pkt.prim_class == 0 && pkt.num_line_verts == 0 && pkt.num_verts > 3) {
            R300DrawPacket q = pkt;
            rc = 0;
            for (uint32_t t = 0; t + 3 <= pkt.num_verts; t += 3) {
                q.verts = (R300Vertex *)d.verts + t;
                q.num_verts = 3;
                rc |= r->draw_r300(ctx, vram, vram_size, &q);
            }
        } else {
            rc = r->draw_r300(ctx, vram, vram_size, &pkt);
        }

        if (rc) fprintf(stderr, "draw %ld (#%llu): rc=%d\n", k - 1, (unsigned long long)d.num, rc);
        drawn++;
        last = pkt;
        /* glsl/host memory is leaked on purpose: the backend may keep the pointers until flush */
    }
    r->flush_r200(ctx);
    fprintf(stderr, "replayed %ld draws; last RT %ux%u pitch %u at %#x bpp %u\n", drawn, last.rt_width, last.rt_height, last.rt_pitch, last.rt_gpu_addr, last.rt_bpp);

    FILE *f = fopen(argv[4], "wb");
    fprintf(f, "P6\n%u %u\n255\n", last.rt_width, last.rt_height);
    for (uint32_t y = 0; y < last.rt_height; y++) {
        for (uint32_t x = 0; x < last.rt_width; x++) {
            const uint8_t *p = vram + last.rt_gpu_addr + ((uint64_t)y * last.rt_pitch + x) * last.rt_bpp;
            uint8_t px[3] = { p[1], p[2], p[3] };   /* ARGB bytes: R G B after A */
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
    return 0;
}
