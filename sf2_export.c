#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ebmusv2.h"

static void w16(FILE* f, unsigned short v) {
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
}
static void w32(FILE* f, unsigned int v) {
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f); fputc((v >> 24) & 0xFF, f);
}

BOOL export_sf2(const char* path, int* inst_list, int inst_count) {
    char out[MAX_PATH];
    snprintf(out, MAX_PATH, "%s.sf2", path);

    FILE* f = fopen(out, "wb");
    if (!f) return FALSE;

    fwrite("RIFF", 1, 4, f);
    long rp = ftell(f); w32(f, 0);
    fwrite("sfbk", 1, 4, f);

    // sdta
    fwrite("LIST", 1, 4, f);
    long sp = ftell(f); w32(f, 0);
    fwrite("sdta", 1, 4, f);

    fwrite("smpl", 1, 4, f);
    long smpl_pos = ftell(f); w32(f, 0);

    long smpl_start = ftell(f);

    uint32_t cursor = 0;

    // Write sample data in order of instruments in inst_list
    for (int idx = 0; idx < inst_count; idx++) {
        int i = inst_list[idx];
        if (i < 0 || i >= 128) continue;
        if (samp[i].data && samp[i].length > 0)
            fwrite(samp[i].data, sizeof(short), (size_t)samp[i].length, f);
    }

    short z = 0;
    for (int i = 0; i < 46; i++) fwrite(&z, 2, 1, f);

    long end = ftell(f);
    fseek(f, smpl_pos, SEEK_SET);
    w32(f, (unsigned int)(end - smpl_start));
    fseek(f, end, SEEK_SET);

    long sdta_end = ftell(f);
    fseek(f, sp, SEEK_SET);
    w32(f, sdta_end - sp - 4);
    fseek(f, sdta_end, SEEK_SET);

    // pdta (minimal valid)
    fwrite("LIST", 1, 4, f);
    long pp = ftell(f); w32(f, 0);
    fwrite("pdta", 1, 4, f);

    // phdr
    fwrite("phdr", 1, 4, f);
    w32(f, (inst_count + 1) * 38);

    for (int i = 0; i < inst_count; i++) {
        char n[20] = { 0 }; snprintf(n, sizeof(n), "p%03d", i);
        fwrite(n, 1, 20, f);
        w16(f, i); w16(f, 0); w16(f, i);
        w32(f, 0); w32(f, 0); w32(f, 0);
    }
    char zname[20] = { 0 };
    fwrite(zname, 1, 20, f);
    w16(f, 0); w16(f, 0); w16(f, inst_count);
    w32(f, 0); w32(f, 0); w32(f, 0);

    // pbag
    fwrite("pbag", 1, 4, f);
    w32(f, (inst_count + 1) * 4);
    for (int i = 0; i <= inst_count; i++) { w16(f, i); w16(f, 0); }

    fwrite("pmod", 1, 4, f); w32(f, 0);

    // pgen
    fwrite("pgen", 1, 4, f);
    w32(f, (inst_count + 1) * 4);
    for (int i = 0; i < inst_count; i++) { w16(f, 41); w16(f, i); }
    w16(f, 0); w16(f, 0);

    // inst
    fwrite("inst", 1, 4, f);
    w32(f, (inst_count + 1) * 22);
    for (int i = 0; i < inst_count; i++) {
        char n[20] = { 0 }; snprintf(n, sizeof(n), "i%03d", i);
        fwrite(n, 1, 20, f);
        w16(f, i);
    }
    fwrite(zname, 1, 20, f); w16(f, inst_count);

    // ibag
    fwrite("ibag", 1, 4, f);
    w32(f, (inst_count + 1) * 4);
    for (int i = 0; i <= inst_count; i++) { w16(f, i); w16(f, 0); }

    fwrite("imod", 1, 4, f); w32(f, 0);

    // igen
    fwrite("igen", 1, 4, f);
    w32(f, (inst_count + 1) * 4);
    for (int i = 0; i < inst_count; i++) { w16(f, 53); w16(f, i); }
    w16(f, 0); w16(f, 0);

    // shdr
    fwrite("shdr", 1, 4, f);
    w32(f, (inst_count + 1) * 46);

    for (int idx = 0; idx < inst_count; idx++) {
        int i = inst_list[idx];
        if (i < 0 || i >= 128) continue;

        char n[20] = { 0 };
        snprintf(n, sizeof(n), "s%03d", idx);
        fwrite(n, 1, 20, f);

        uint32_t start = cursor;
        uint32_t end2 = cursor + (samp[i].length > 0 ? (uint32_t)samp[i].length : 0u);

        w32(f, start);
        w32(f, end2);
        w32(f, end2);
        w32(f, end2);

        w32(f, (unsigned int)(mixrate ? mixrate : 44100));
        fputc(60, f); fputc(0, f);
        w16(f, 0); w16(f, 1);

        cursor += (samp[i].length > 0 ? (uint32_t)samp[i].length : 0u);
    }

    fwrite(zname, 1, 20, f);
    w32(f, 0); w32(f, 0); w32(f, 0); w32(f, 0);
    w32(f, 0); fputc(0, f); fputc(0, f); w16(f, 0); w16(f, 0);

    long pdta_end = ftell(f);
    fseek(f, pp, SEEK_SET);
    w32(f, pdta_end - pp - 4);
    fseek(f, pdta_end, SEEK_SET);

    long final = ftell(f);
    fseek(f, rp, SEEK_SET);
    w32(f, final - 8);

    fclose(f);
    return TRUE;
}