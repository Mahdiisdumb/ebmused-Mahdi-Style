#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ebmusv2.h"

/* ---- external globals ---- */
extern int mixrate;
extern unsigned char spc[];
extern int inst_base;


#ifndef MAX_INSTRUMENTS
#define MAX_INSTRUMENTS 256
#endif

/* ================= WAV + FFMPEG ================= */

static int convert_with_ffmpeg_temp(
    short* samples,
    uint32_t samples_len,
    unsigned int sr,
    short** out_buf,
    uint32_t* out_samples
) {
    if (!samples || !samples_len || !out_buf || !out_samples) return 0;

    *out_buf = NULL;
    *out_samples = 0;

    CHAR tmpPath[MAX_PATH], inName[MAX_PATH], outName[MAX_PATH];

    if (!GetTempPathA(MAX_PATH, tmpPath)) return 0;
    if (!GetTempFileNameA(tmpPath, "ebm", 0, inName)) return 0;

    snprintf(outName, MAX_PATH, "%s.wav", inName);

    FILE* wf = fopen(inName, "wb");
    if (!wf) return 0;

    uint32_t sr_final = sr ? sr : (mixrate ? mixrate : 44100);
    uint32_t data_bytes = samples_len * 2;

    fwrite("RIFF", 1, 4, wf);
    uint32_t riff_size = 36 + data_bytes;
    fwrite(&riff_size, 4, 1, wf);
    fwrite("WAVE", 1, 4, wf);

    fwrite("fmt ", 1, 4, wf);
    uint32_t fmt_sz = 16;
    uint16_t audio_format = 1;
    uint16_t ch = 1;
    uint16_t bits = 16;

    fwrite(&fmt_sz, 4, 1, wf);
    fwrite(&audio_format, 2, 1, wf);
    fwrite(&ch, 2, 1, wf);
    fwrite(&sr_final, 4, 1, wf);

    uint32_t byte_rate = sr_final * 2;
    uint16_t block_align = 2;

    fwrite(&byte_rate, 4, 1, wf);
    fwrite(&block_align, 2, 1, wf);
    fwrite(&bits, 2, 1, wf);

    fwrite("data", 1, 4, wf);
    fwrite(&data_bytes, 4, 1, wf);
    fwrite(samples, sizeof(short), samples_len, wf);

    fclose(wf);

    char cmd[MAX_PATH * 3];
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -i \"%s\" -ar %u -ac 1 -sample_fmt s16 \"%s\" >nul 2>nul",
        inName, sr_final, outName);

    if (system(cmd) != 0) {
        DeleteFileA(inName);
        return 0;
    }

    FILE* rf = fopen(outName, "rb");
    if (!rf) {
        DeleteFileA(inName);
        DeleteFileA(outName);
        return 0;
    }

    char tag[5] = { 0 };
    uint32_t sz = 0;

    while (fread(tag, 1, 4, rf) == 4) {
        if (fread(&sz, 4, 1, rf) != 1) break;

        if (!memcmp(tag, "data", 4)) {
            uint32_t count = sz / 2;

            short* buf = (short*)malloc(count * sizeof(short));
            if (!buf) break;

            fread(buf, 2, count, rf);

            *out_buf = buf;
            *out_samples = count;

            fclose(rf);
            DeleteFileA(inName);
            DeleteFileA(outName);
            return 1;
        }

        fseek(rf, sz, SEEK_CUR);
    }

    fclose(rf);
    DeleteFileA(inName);
    DeleteFileA(outName);
    return 0;
}

/* ================= WRITERS ================= */

static void w16(FILE* f, uint16_t v) {
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}

static void w32(FILE* f, uint32_t v) {
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >> 24) & 0xFF, f);
}

/* ================= MAIN ================= */

BOOL export_sf2(const char* path, int* inst_list, int inst_count) {
    if (!path || !inst_list || inst_count <= 0) return FALSE;

    char out[MAX_PATH];
    snprintf(out, MAX_PATH, "%s.sf2", path);

    FILE* f = fopen(out, "wb");
    if (!f) return FALSE;

    int* samples_for_inst = calloc(inst_count, sizeof(int));
    uint32_t* actual_len = calloc(inst_count, sizeof(uint32_t));

    if (!samples_for_inst || !actual_len) {
        free(samples_for_inst);
        free(actual_len);
        if (f) fclose(f);
        return FALSE;
    }

    /* ================= RIFF ================= */
    fwrite("RIFF", 1, 4, f);
    long riff_pos = ftell(f); w32(f, 0);
    fwrite("sfbk", 1, 4, f);

    /* ================= INFO ================= */
    fwrite("LIST", 1, 4, f);
    long info_pos = ftell(f); w32(f, 0);
    fwrite("INFO", 1, 4, f);

    fwrite("ifil", 1, 4, f); w32(f, 4);
    w16(f, 2); w16(f, 1);

    const char* s1 = "EMU8000";
    fwrite("isng", 1, 4, f); w32(f, strlen(s1) + 1);
    fwrite(s1, 1, strlen(s1) + 1, f);

    const char* s2 = "ebmused export";
    fwrite("INAM", 1, 4, f); w32(f, strlen(s2) + 1);
    fwrite(s2, 1, strlen(s2) + 1, f);

    long info_end = ftell(f);
    fseek(f, info_pos, SEEK_SET);
    w32(f, (uint32_t)(info_end - info_pos - 4));
    fseek(f, info_end, SEEK_SET);

    /* ================= SDTA ================= */
    fwrite("LIST", 1, 4, f);
    long sdta_pos = ftell(f); w32(f, 0);
    fwrite("sdta", 1, 4, f);

    fwrite("smpl", 1, 4, f);
    long smpl_pos = ftell(f); w32(f, 0);
    long smpl_start = ftell(f);

    uint32_t cursor = 0;

    for (int i = 0; i < inst_count; i++) {
        samples_for_inst[i] = -1;

        int inst = inst_list[i];
        if (inst < 0 || inst >= MAX_INSTRUMENTS) continue;

        int sidx = (int)spc[inst_base + 6 * inst];
        if (sidx < 0 || sidx >= 128) continue;

        struct sample* sa = &samp[sidx];
        if (!sa->data || sa->length <= 0) continue;

        short* outbuf = NULL;
        uint32_t outlen = 0;

        if (convert_with_ffmpeg_temp(sa->data, sa->length,
            mixrate ? mixrate : 44100, &outbuf, &outlen)) {

            fwrite(outbuf, sizeof(short), outlen, f);
            actual_len[i] = outlen;
            cursor += outlen;
            free(outbuf);

        }
        else {
            fwrite(sa->data, sizeof(short), sa->length, f);
            actual_len[i] = sa->length;
            cursor += sa->length;
        }

        samples_for_inst[i] = sidx;
    }

    short zero = 0;
    for (int i = 0; i < 46; i++) fwrite(&zero, 2, 1, f);

    long smpl_end = ftell(f);

    fseek(f, smpl_pos, SEEK_SET);
    w32(f, (uint32_t)(smpl_end - smpl_start));
    fseek(f, smpl_end, SEEK_SET);

    long sdta_end = ftell(f);
    fseek(f, sdta_pos, SEEK_SET);
    w32(f, (uint32_t)(sdta_end - sdta_pos - 4));
    fseek(f, sdta_end, SEEK_SET);

    /* ================= PDTA (basic but valid structure) ================= */

    fwrite("LIST", 1, 4, f);
    long pdta_pos = ftell(f); w32(f, 0);
    fwrite("pdta", 1, 4, f);

    fwrite("phdr", 1, 4, f);
    w32(f, (inst_count + 1) * 38);

    for (int i = 0; i < inst_count; i++) {
        char name[20] = { 0 };
        snprintf(name, sizeof(name), "p%03d", i);
        fwrite(name, 1, 20, f);
        w16(f, i); w16(f, 0); w16(f, i);
        w32(f, 0); w32(f, 0); w32(f, 0);
    }

    char zero20[20] = { 0 };
    fwrite(zero20, 1, 20, f);
    w16(f, 0); w16(f, 0); w16(f, inst_count);
    w32(f, 0); w32(f, 0); w32(f, 0);

    fwrite("pbag", 1, 4, f);
    w32(f, (inst_count + 1) * 4);
    for (int i = 0; i <= inst_count; i++) {
        w16(f, i); w16(f, 0);
    }

    fwrite("pmod", 1, 4, f); w32(f, 0);

    fwrite("pgen", 1, 4, f);
    w32(f, (inst_count + 1) * 4);
    for (int i = 0; i < inst_count; i++) {
        w16(f, 41); w16(f, i);
    }
    w16(f, 0); w16(f, 0);

    fwrite("inst", 1, 4, f);
    w32(f, (inst_count + 1) * 22);

    for (int i = 0; i < inst_count; i++) {
        char name[20] = { 0 };
        snprintf(name, sizeof(name), "i%03d", i);
        fwrite(name, 1, 20, f);
        w16(f, i);
    }

    fwrite(zero20, 1, 20, f);
    w16(f, inst_count);

    fwrite("ibag", 1, 4, f);
    w32(f, (inst_count + 1) * 4);
    for (int i = 0; i <= inst_count; i++) {
        w16(f, i); w16(f, 0);
    }

    fwrite("imod", 1, 4, f); w32(f, 0);

    fwrite("igen", 1, 4, f);
    w32(f, (inst_count + 1) * 4);
    for (int i = 0; i < inst_count; i++) {
        w16(f, 53); w16(f, i);
    }
    w16(f, 0); w16(f, 0);

    fwrite("shdr", 1, 4, f);
    w32(f, (inst_count + 1) * 46);

    uint32_t cursor2 = 0;

    for (int i = 0; i < inst_count; i++) {
        char name[20] = { 0 };
        snprintf(name, sizeof(name), "s%03d", i);
        fwrite(name, 1, 20, f);

        uint32_t start = cursor2;
        uint32_t end = start + actual_len[i];

        w32(f, start);
        w32(f, end);
        w32(f, start);
        w32(f, end);

        w32(f, mixrate ? mixrate : 44100);
        fputc(60, f); fputc(0, f);
        w16(f, 0); w16(f, 1);

        cursor2 = end;
    }

    fwrite(zero20, 1, 20, f);
    w32(f, 0); w32(f, 0); w32(f, 0); w32(f, 0);
    w32(f, 0);
    fputc(0, f); fputc(0, f);
    w16(f, 0); w16(f, 0);

    long pdta_end = ftell(f);
    fseek(f, pdta_pos, SEEK_SET);
    w32(f, (uint32_t)(pdta_end - pdta_pos - 4));
    fseek(f, pdta_end, SEEK_SET);

    long end = ftell(f);
    fseek(f, riff_pos, SEEK_SET);
    w32(f, (uint32_t)(end - 8));

    fclose(f);
    free(samples_for_inst);
    free(actual_len);

    return TRUE;
}