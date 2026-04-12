#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "ebmusv2.h"
#include "misc.h"

typedef struct {
    uint64_t tick;
    unsigned char status, d1, d2;
} MidiEvent;

// ---------------- HELPERS ----------------

static void w16(FILE* f, unsigned short v) {
    fputc((v >> 8) & 0xFF, f);
    fputc(v & 0xFF, f);
}

static void w32(FILE* f, unsigned int v) {
    fputc((v >> 24) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
    fputc(v & 0xFF, f);
}

static void var(FILE* f, uint32_t v) {
    unsigned char b[5];
    int i = 0;
    b[i++] = v & 0x7F;
    while ((v >>= 7)) b[i++] = 0x80 | (v & 0x7F);
    while (i--) fputc(b[i], f);
}

static int add(MidiEvent** a, int* c, int* cap, MidiEvent e) {
    if (*c >= *cap) {
        int nc = (*cap ? *cap * 2 : 512);
        MidiEvent* n = realloc(*a, nc * sizeof(MidiEvent));
        if (!n) return 0;
        *a = n;
        *cap = nc;
    }
    (*a)[(*c)++] = e;
    return 1;
}

static int cmp(const void* a, const void* b) {
    const MidiEvent* A = (const MidiEvent*)a;
    const MidiEvent* B = (const MidiEvent*)b;
    if (A->tick < B->tick) return -1;
    if (A->tick > B->tick) return 1;
    return 0;
}

// ---------------- EXPORT ----------------

BOOL export_song_to_midi(const char* path) {

    char buf[16];
    if (!InputBox("BPM (10–999)", "Enter BPM:", buf, sizeof(buf)))
        return FALSE;

    int bpm = atoi(buf);

    if (bpm < 10) bpm = 10;
    if (bpm > 999) bpm = 999;

    char out[MAX_PATH];
    snprintf(out, MAX_PATH, "%s.mid", path);

    FILE* f = fopen(out, "wb");
    if (!f) return FALSE;

    const int division = 980;

    int slots = cur_song.order_length * 8;
    MidiEvent** events = calloc(slots, sizeof(MidiEvent*));
    int* ev_count = calloc(slots, sizeof(int));
    int* ev_cap = calloc(slots, sizeof(int));
    if (!events || !ev_count || !ev_cap) {
        if (events) free(events);
        if (ev_count) free(ev_count);
        if (ev_cap) free(ev_cap);
        fclose(f);
        return FALSE;
    }

    struct song_state sim = pattop_state;

    int prev[8];
    int note[8];

    for (int i = 0; i < 8; i++) {
        prev[i] = sim.chan[i].samp_pos;
        note[i] = -1;
    }

    // =========================================================
    // STEP MODEL (THIS IS THE FIX)
    // =========================================================
    uint64_t step = 0;

    const int MAX_CYCLES = 200000;

    // ---------------- COLLECT EVENTS (NO TIME HERE) ----------------

    for (int ord = 0; ord < cur_song.order_length; ord++) {

        int pat = cur_song.order[ord];

        if (pat < 0 || pat >= cur_song.patterns) {

            int c = 0;
            while (do_cycle_no_sound(&sim) && ++c < MAX_CYCLES) {
                step++;
            }
            continue;
        }

        for (int ch = 0; ch < 8; ch++) {
            sim.chan[ch].ptr = cur_song.pattern[pat][ch].track;
            sim.chan[ch].sub_count = 0;
            prev[ch] = sim.chan[ch].samp_pos;
        }

        int cycles = 0;

        while (do_cycle_no_sound(&sim)) {

            if (++cycles > MAX_CYCLES)
                break;

            for (int ch = 0; ch < 8; ch++) {

                int p = prev[ch];
                int q = sim.chan[ch].samp_pos;

                int midi_ch = ch & 0x0F;

                // NOTE ON
                if (p < 0 && q >= 0) {

                    int n = (sim.chan[ch].note.cur >> 8) & 0x7F;
                    if (n > 127) n = 60;

                    int v = (sim.chan[ch].total_vol * 127) / 255;
                    if (v < 1) v = 1;

                    note[ch] = n;

                    MidiEvent e = {
                        step,
                        (unsigned char)(0x90 | midi_ch),
                        (unsigned char)n,
                        (unsigned char)v
                    };

                    int slot = ord * 8 + ch;
                    if (!add(&events[slot], &ev_count[slot], &ev_cap[slot], e)) {
                        MessageBox2("Out of memory during MIDI export", "Export", MB_ICONERROR);
                        for (int i = 0; i < slots; i++) if (events[i]) free(events[i]);
                        free(events); free(ev_count); free(ev_cap);
                        fclose(f);
                        return FALSE;
                    }
                }

                // NOTE OFF
                if (p >= 0 && q < 0 && note[ch] >= 0) {

                    MidiEvent e = {
                        step,
                        (unsigned char)(0x80 | midi_ch),
                        (unsigned char)note[ch],
                        0
                    };

                    int slot = ord * 8 + ch;
                    if (!add(&events[slot], &ev_count[slot], &ev_cap[slot], e)) {
                        MessageBox2("Out of memory during MIDI export", "Export", MB_ICONERROR);
                        for (int i = 0; i < slots; i++) if (events[i]) free(events[i]);
                        free(events); free(ev_count); free(ev_cap);
                        fclose(f);
                        return FALSE;
                    }
                    note[ch] = -1;
                }

                prev[ch] = q;
            }

            step++; // ONLY place time advances
        }
    }

    // ---------------- SORT per-slot (no time conversion yet) ----------------
    // (already sorted earlier per-slot when writing)

    // =========================================================
    // CONVERT STEP TIME → REAL MIDI TIME
    // Use tracker timing to compute how many MIDI ticks correspond to one
    // tracker cycle (step). Formula:
    // ticks_per_step = division * (seconds_per_step) / (seconds_per_quarter)
    // seconds_per_step = 256 / (timer_speed * tempo_shift)
    // seconds_per_quarter = tempo_us / 1e6
    // where tempo_us = 60000000 / bpm
    double tempo_us = 60000000.0 / (double)bpm;
    unsigned int tempo_shift = (pattop_state.tempo.cur >> 8);
    if (tempo_shift == 0) tempo_shift = 1;
    double seconds_per_step = 256.0 / ((double)timer_speed * (double)tempo_shift);
    double ticks_per_step = (double)division * seconds_per_step / (tempo_us / 1e6);

    // apply mapping
    for (int s = 0; s < slots; s++) {
        if (!events[s]) continue;
        for (int i = 0; i < ev_count[s]; i++) {
            events[s][i].tick = (uint64_t)llround((double)events[s][i].tick * ticks_per_step);
        }
    }

    // ---------------- HEADER ----------------
    int track_count = 1; // meta
    for (int s = 0; s < slots; s++) if (ev_count[s]) track_count++;
    fwrite("MThd", 1, 4, f);
    w32(f, 6);
    w16(f, 1); // format 1
    w16(f, track_count);
    w16(f, division);

    // ---------------- META TRACK (tempo)
    fwrite("MTrk", 1, 4, f);
    long lp = ftell(f);
    w32(f, 0);
    int tempo = 60000000 / bpm;
    var(f, 0);
    fputc(0xFF, f); fputc(0x51, f); fputc(3, f);
    fputc((tempo >> 16) & 0xFF, f); fputc((tempo >> 8) & 0xFF, f); fputc(tempo & 0xFF, f);
    // end of meta
    var(f, 0); fputc(0xFF, f); fputc(0x2F, f); fputc(0x00, f);
    long end = ftell(f);
    fseek(f, lp, SEEK_SET);
    w32(f, end - lp - 4);
    fseek(f, end, SEEK_SET);

    // ---------------- DSP TRACKS (one per pattern x dsp)
    for (int s = 0; s < slots; s++) {
        if (!ev_count[s]) continue;
        qsort(events[s], ev_count[s], sizeof(MidiEvent), cmp);
        fwrite("MTrk", 1, 4, f);
        long tpos = ftell(f);
        w32(f, 0);
        uint64_t last = 0;
        for (int i = 0; i < ev_count[s]; i++) {
            uint32_t delta = (uint32_t)(events[s][i].tick - last);
            var(f, delta);
            fputc(events[s][i].status, f);
            fputc(events[s][i].d1, f);
            fputc(events[s][i].d2, f);
            last = events[s][i].tick;
        }
        var(f, 0); fputc(0xFF, f); fputc(0x2F, f); fputc(0x00, f);
        long tend = ftell(f);
        fseek(f, tpos, SEEK_SET); w32(f, tend - tpos - 4); fseek(f, tend, SEEK_SET);
        free(events[s]);
    }

    for (int s = 0; s < slots; s++) if (events[s]) free(events[s]);
    free(events); free(ev_count); free(ev_cap);
    fclose(f);

    MessageBox2("MIDI Exported", "OK", MB_OK);
    return TRUE;
}