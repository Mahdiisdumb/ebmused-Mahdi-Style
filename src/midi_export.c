#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "ebmusv2.h"
#include "misc.h"

extern BOOL export_sf2(const char* path, int* inst_map, int inst_count);

typedef struct {
    uint64_t tick;
    unsigned char status, d1, d2;
    int inst;
} MidiEvent;

static int resolve_inst(struct channel_state* c) {
    if (!c || !c->samp) return -1;
    return c->samp->id;
}

// ---------- helpers ----------
static void w16(FILE* f, unsigned short v) {
    fputc((v >> 8) & 0xFF, f); fputc(v & 0xFF, f);
}
static void w32(FILE* f, unsigned int v) {
    fputc((v >> 24) & 0xFF, f); fputc((v >> 16) & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);  fputc(v & 0xFF, f);
}
static void var(FILE* f, uint32_t v) {
    unsigned char b[5]; int i = 0;
    b[i++] = v & 0x7F;
    while ((v >>= 7)) b[i++] = 0x80 | (v & 0x7F);
    while (i--) fputc(b[i], f);
}
static int add(MidiEvent** a, int* c, int* cap, MidiEvent e) {
    if (*c >= *cap) {
        int nc = (*cap ? *cap * 2 : 65536);
        MidiEvent* n = realloc(*a, nc * sizeof(MidiEvent));
        if (!n) return 0;
        *a = n; *cap = nc;
    }
    (*a)[(*c)++] = e;
    return 1;
}
static int cmp(const void* a, const void* b) {
    const MidiEvent* A = a; const MidiEvent* B = b;
    if (A->inst != B->inst) return A->inst - B->inst;
    if (A->tick < B->tick) return -1;
    if (A->tick > B->tick) return 1;
    return 0;
}

// ---------- EXPORT ----------
BOOL export_song_to_midi(const char* path) {

    char buf[16];
    if (!InputBox("BPM (10–999)", "Enter BPM:", buf, sizeof(buf)))
        return FALSE;

    int bpm = atoi(buf);
    if (bpm < 10) bpm = 10;
    if (bpm > 999) bpm = 999;

    int used_inst[128] = { 0 };
    int inst_map[128];

    MidiEvent* events = NULL;
    int ev_count = 0, ev_cap = 0;

    struct song_state sim = pattop_state;

    int prev[8], note[8], note_inst[8];

    for (int i = 0; i < 8; i++) {
        prev[i] = sim.chan[i].samp_pos;
        note[i] = -1;
        note_inst[i] = -1;
    }

    uint64_t step = 0;

    // ---------- collect ----------
    for (int ord = 0; ord < cur_song.order_length; ord++) {

        int pat = cur_song.order[ord];
        if (pat < 0 || pat >= cur_song.patterns) continue;

        for (int ch = 0; ch < 8; ch++) {
            sim.chan[ch].ptr = cur_song.pattern[pat][ch].track;
            sim.chan[ch].sub_count = 0;
        }

        while (do_cycle_no_sound(&sim)) {

            for (int ch = 0; ch < 8; ch++) {

                int p = prev[ch];
                int q = sim.chan[ch].samp_pos;

                struct channel_state* c = &sim.chan[ch];
                int raw_inst = resolve_inst(c);
                if (raw_inst < 0 || raw_inst >= 128) {
                    prev[ch] = q;
                    continue;
                }

                // NOTE ON
                if (p < 0 && q >= 0) {

                    int n = (sim.chan[ch].note.cur >> 8) & 0x7F;
                    int v = (sim.chan[ch].total_vol * 127) / 255;

                    note[ch] = n;
                    note_inst[ch] = raw_inst;
                    used_inst[raw_inst] = 1;

                    add(&events, &ev_count, &ev_cap, (MidiEvent){ step, (unsigned char)(0x90 | (ch & 0xF)), (unsigned char)n, (unsigned char)v, raw_inst });
                }

                // NOTE OFF
                if (p >= 0 && q < 0 && note[ch] >= 0) {

                    add(&events, &ev_count, &ev_cap, (MidiEvent){ step, (unsigned char)(0x80 | (ch & 0xF)), (unsigned char)note[ch], 0, note_inst[ch] });

                    note[ch] = -1;
                }

                // PAN
                int pan = c->panning.cur >> 8;
                if (pan < 0) pan = 0;
                if (pan > 255) pan = 255;

                int midi_pan = (pan * 127) / 255;

                add(&events, &ev_count, &ev_cap, (MidiEvent){ step, (unsigned char)(0xB0 | (ch & 0xF)), 10, (unsigned char)midi_pan, raw_inst });

                prev[ch] = q;
            }

            step++;
        }
    }

    // ---------- build map (AFTER collect, correct order) ----------
    int inst_count = 0;
    for (int i = 0; i < 128; i++) {
        if (used_inst[i])
            inst_map[i] = inst_count++;
        else
            inst_map[i] = -1;
    }

    // ---------- REMAP EVENTS (THIS WAS MISSING) ----------
    for (int i = 0; i < ev_count; i++) {
        int raw = events[i].inst;
        events[i].inst = inst_map[raw];
    }

    qsort(events, ev_count, sizeof(MidiEvent), cmp);

    FILE* f = fopen(path, "wb");
    if (!f) return FALSE;

    int division = 960;

    double tempo_us = 60000000.0 / (double)bpm;
    unsigned int tempo_shift = (pattop_state.tempo.cur >> 8);
    if (tempo_shift == 0) tempo_shift = 1;

    double seconds_per_step =
        256.0 / ((double)timer_speed * (double)tempo_shift);

    double ticks_per_step =
        (double)division * seconds_per_step / (tempo_us / 1e6);

    for (int i = 0; i < ev_count; i++) {
        events[i].tick =
            (uint64_t)llround((double)events[i].tick * ticks_per_step);
    }

    // ---------- MIDI header ----------
    fwrite("MThd", 1, 4, f);
    w32(f, 6);
    w16(f, 1);
    w16(f, 1 + inst_count);
    w16(f, division);

    // tempo track
    fwrite("MTrk", 1, 4, f);
    long lp = ftell(f);
    w32(f, 0);

    var(f, 0);
    fputc(0xFF, f); fputc(0x51, f); fputc(3, f);

    unsigned int tempo = 60000000 / bpm;
    fputc((tempo >> 16) & 0xFF, f);
    fputc((tempo >> 8) & 0xFF, f);
    fputc(tempo & 0xFF, f);

    var(f, 0);
    fputc(0xFF, f); fputc(0x2F, f); fputc(0, f);

    long end = ftell(f);
    fseek(f, lp, SEEK_SET);
    w32(f, end - lp - 4);
    fseek(f, end, SEEK_SET);

    // ---------- tracks ----------
    for (int i = 0; i < inst_count; i++) {

        fwrite("MTrk", 1, 4, f);
        long tp = ftell(f);
        w32(f, 0);

        uint64_t last = 0;

        for (int j = 0; j < ev_count; j++) {
            if (events[j].inst != i) continue;

            uint32_t d = (uint32_t)(events[j].tick - last);
            var(f, d);

            fputc(events[j].status, f);
            fputc(events[j].d1, f);
            fputc(events[j].d2, f);

            last = events[j].tick;
        }

        var(f, 0);
        fputc(0xFF, f); fputc(0x2F, f); fputc(0, f);

        long te = ftell(f);
        fseek(f, tp, SEEK_SET);
        w32(f, te - tp - 4);
        fseek(f, te, SEEK_SET);
    }

    fclose(f);

    export_sf2(path, inst_map, inst_count);

    free(events);

    return TRUE;
}