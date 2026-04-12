#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "ebmusv2.h"

static void write_le16(FILE* f, unsigned short v) {
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
}
static void write_le32(FILE* f, unsigned int v) {
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f); fputc((v >> 24) & 0xFF, f);
}

static void write_str(FILE* f, const char* s, int len) {
    char buf[64] = { 0 };
    strncpy(buf, s, len);
    fwrite(buf, 1, len, f);
}

BOOL export_sf2(const char* path) {
    char out[MAX_PATH];
    snprintf(out, MAX_PATH, "%s.sf2", path);

    FILE* f = fopen(out, "wb");
    if (!f) return FALSE;

    fwrite("RIFF", 1, 4, f);
    long riff_pos = ftell(f); write_le32(f, 0);
    fwrite("sfbk", 1, 4, f);

    // INFO
    fwrite("LIST", 1, 4, f);
    write_le32(f, 12);
    fwrite("INFO", 1, 4, f);
    fwrite("INAM", 1, 4, f);
    write_le32(f, 4);
    fwrite("EBM", 1, 4, f);

    // SDTA
    fwrite("LIST", 1, 4, f);
    long sdta_pos = ftell(f); write_le32(f, 0);
    fwrite("sdta", 1, 4, f);

    fwrite("smpl", 1, 4, f);
    long smpl_pos = ftell(f); write_le32(f, 0);

    long smpl_start = ftell(f);
    unsigned int frame_cursor = 0; // in sample frames (shorts)

    // write sample data sequentially
    for (int i = 0; i < 128; i++) {
        if (!samp[i].data || samp[i].length <= 0) continue;
        fwrite(samp[i].data, sizeof(short), samp[i].length, f);
        frame_cursor += samp[i].length;
    }

    long smpl_end = ftell(f);
    fseek(f, smpl_pos, SEEK_SET);
    write_le32(f, (unsigned int)(smpl_end - smpl_start));
    fseek(f, smpl_end, SEEK_SET);

    long sdta_end = ftell(f);
    fseek(f, sdta_pos, SEEK_SET);
    write_le32(f, sdta_end - sdta_pos - 4);
    fseek(f, sdta_end, SEEK_SET);

    // PDTA
    fwrite("LIST", 1, 4, f);
    long pdta_pos = ftell(f); write_le32(f, 0);
    fwrite("pdta", 1, 4, f);

    // Count samples and prepare index maps
    int sample_count = 0;
    int sample_index[128];
    for (int i = 0; i < 128; i++) {
        sample_index[i] = -1;
        if (samp[i].data && samp[i].length > 0) sample_index[i] = sample_count++;
    }

    // phdr: one preset per sample + terminal
    int phdr_count = sample_count + 1;
    fwrite("phdr", 1, 4, f); write_le32(f, phdr_count * 38);
    for (int i = 0; i < sample_count; i++) {
        char pname[20]; memset(pname,0,20); snprintf(pname,20,"EBm_preset_%03d", i);
        fwrite(pname,1,20,f);
        write_le16(f, i); // preset number
        write_le16(f, 0); // bank
        write_le16(f, i); // presetBagIndex -> i
        write_le32(f, 0); write_le32(f, 0); write_le32(f, 0);
    }
    // terminal phdr
    char tname[20]; memset(tname,0,20); fwrite(tname,1,20,f);
    write_le16(f, 0); write_le16(f, 0); write_le16(f, sample_count);
    write_le32(f, 0); write_le32(f, 0); write_le32(f, 0);

    // pbag: one per preset, plus terminal
    fwrite("pbag",1,4,f); write_le32(f, (sample_count + 1) * 4);
    for (int i = 0; i <= sample_count; i++) {
        write_le16(f, i); // gen index
        write_le16(f, 0); // mod index (unused)
    }

    // pmod empty
    fwrite("pmod",1,4,f); write_le32(f, 0);

    // pgen: one generator per preset mapping to instrument (op 41 = instrument)
    fwrite("pgen",1,4,f); write_le32(f, (sample_count) * 4);
    for (int i = 0; i < sample_count; i++) {
        write_le16(f, 41); // sfGenInstrument
        write_le16(f, i);  // instrument index
    }

    // inst: write instrument headers (one per sample) + terminal
    fwrite("inst",1,4,f); write_le32(f, (sample_count + 1) * 22);
    for (int i = 0; i < sample_count; i++) {
        char iname[20]; memset(iname,0,20); snprintf(iname,20,"EBm_inst_%03d", i);
        fwrite(iname,1,20,f);
        write_le16(f, i); // instBagIndex
    }
    // terminal inst
    char iname0[20]; memset(iname0,0,20); fwrite(iname0,1,20,f); write_le16(f, sample_count);

    // ibag: one per instrument + terminal
    fwrite("ibag",1,4,f); write_le32(f, (sample_count + 1) * 4);
    for (int i = 0; i <= sample_count; i++) {
        write_le16(f, i); // instGen index
        write_le16(f, 0); // instMod index
    }

    // imod empty
    fwrite("imod",1,4,f); write_le32(f, 0);

    // igen: one per instrument mapping sample id (op 53 = sampleID)
    fwrite("igen",1,4,f); write_le32(f, (sample_count) * 4);
    for (int i = 0; i < sample_count; i++) {
        write_le16(f, 53); // sfGenSampleID
        write_le16(f, i);  // sample index
    }

    // shdr: one per sample + terminal
    fwrite("shdr",1,4,f); write_le32(f, (sample_count + 1) * 46);
    unsigned int frame_cursor2 = 0;
    for (int i = 0; i < 128; i++) {
        if (!samp[i].data || samp[i].length <= 0) continue;
        char nm[20]; memset(nm,0,20); snprintf(nm,20,"samp_%03d", i);
        fwrite(nm,1,20,f);
        write_le32(f, frame_cursor2);
        write_le32(f, frame_cursor2 + samp[i].length);
        write_le32(f, frame_cursor2 + 0); // loop start
        write_le32(f, frame_cursor2 + (samp[i].loop_len > 0 ? samp[i].loop_len : 0));
        write_le32(f, mixrate ? mixrate : 32000);
        fputc(60, f); fputc(0, f); // origPitch, pitchCorrection
        write_le16(f, 0); // sampleLink
        write_le16(f, 1); // sampleType mono
        frame_cursor2 += samp[i].length;
    }
    // terminal shdr
    char zname[20]; memset(zname,0,20); fwrite(zname,1,20,f);
    write_le32(f, 0); write_le32(f, 0); write_le32(f, 0); write_le32(f, 0);
    write_le32(f, 0); fputc(0, f); fputc(0, f); write_le16(f,0); write_le16(f,0);

    long pdta_end = ftell(f);
    fseek(f, pdta_pos, SEEK_SET);
    write_le32(f, (unsigned int)(pdta_end - pdta_pos - 4));
    fseek(f, pdta_end, SEEK_SET);

    long end = ftell(f);
    fseek(f, riff_pos, SEEK_SET);
    write_le32(f, end - 8);

    fclose(f);
    return TRUE;
}