/*
 * Read a PPCGPU_PVSREC file: disassemble capture N's vertex program and its
 * constants and re-run the interpreter (r300_pvs.c) on the recorded inputs,
 * comparing with the recorded outputs.
 *   pvs-rerun <file> <capture> [dis]
 * Build: cc -I qemu/hw/display/r300 tools/pvs-rerun.c qemu/hw/display/r300/r300_pvs.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "r300_pvs.h"
typedef struct { uint32_t n, first, last, maxc, fcopc, fca[16], fcl[16], code[1024]; float consts[256][4]; } Hdr;
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); int want = atoi(argv[2]); int dis = argc > 3;
    for (int k = 0;; k++) {
        char m[4]; Hdr h;
        if (fread(m, 1, 4, f) != 4) break;
        fread(&h.n, 4, 5, f); fread(h.fca, 4, 16, f); fread(h.fcl, 4, 16, f);
        fread(h.code, 4, 1024, f); fread(h.consts, 16, 256, f);
        size_t sz = (size_t)h.n * 32 * 4 * 4;
        float (*in)[32][4] = malloc(sz), (*out)[32][4] = malloc(sz);
        fread(in, 1, sz, f); fread(out, 1, sz, f);
        if (k != want) { free(in); free(out); continue; }
        printf("capture %d: n=%u insts %u..%u maxconst=%u fc_opc=%#x\n", k, h.n, h.first, h.last, h.maxc, h.fcopc);
        if (dis) for (unsigned i = h.first; i <= h.last; i++) { char b[200]; r300_pvs_disasm_inst(&h.code[i*4], b, sizeof b);
            printf("  %3u: %08x %08x %08x %08x  %s\n", i, h.code[i*4], h.code[i*4+1], h.code[i*4+2], h.code[i*4+3], b); }
        R300PVSProgram p = { .code = h.code, .first_inst = h.first, .last_inst = h.last, .consts = (const float (*)[4])h.consts,
                             .max_const = h.maxc, .fc_opc = h.fcopc };
        memcpy(p.fc_addrs, h.fca, 64); memcpy(p.fc_loop, h.fcl, 64);
        unsigned diff = 0, unsup = 0; float maxd = 0;
        for (unsigned v = 0; v < h.n; v++) {
            float o[32][4] = {{0}};
            unsup |= r300_pvs_run(&p, (const float (*)[4])in[v], o);
            for (int a = 0; a < 32; a++) for (int c = 0; c < 4; c++) { float d = o[a][c] - out[v][a][c];
                if (d != d || d > 1e-3f || d < -1e-3f) { diff++; if (d > maxd) maxd = d; } }
        }
        printf("rerun vs recorded: %u differing components, unsup flags %#x\n", diff, unsup);
        if (dis) { printf("consts (nonzero):\n"); for (int i = 0; i < 256; i++) { float *c = h.consts[i]; if (c[0]||c[1]||c[2]||c[3]) printf("  c[%3d] = %9.4f %9.4f %9.4f %9.4f\n", i, c[0], c[1], c[2], c[3]); }
            for (int v = 0; v < 3; v++) { printf("vertex %d inputs:", v); for (int a = 0; a < 32; a++) { float *c = in[v][a]; if (c[0]||c[1]||c[2]||c[3]) printf("\n   in[%d] = %9.4f %9.4f %9.4f %9.4f", a, c[0], c[1], c[2], c[3]); } printf("\n"); } }
        return 0;
    }
    return 1;
}
