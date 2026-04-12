#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "ebmusv2.h"
#include "misc.h"

typedef struct {
    uint64_t tick;
    unsigned char status, d1, d2;
} MidiEvent;

// ---------------- HELPERS ----------------

static void write_be16(FILE* f, unsigned short v) {
    fputc((v >> 8) & 0xFF, f);
    fputc(v & 0xFF, f);
}

static void write_be32(FILE* f, unsigned int v) {
    fputc((v >> 24) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
    fputc(v & 0xFF, f);
}

static void write_varlen(FILE* f, unsigned int v) {
    unsigned char buf[5];
    int i = 0;
    buf[i++] = v & 0x7F;
    while ((v >>= 7)) buf[i++] = 0x80 | (v & 0x7F);
    while (i--) fputc(buf[i], f);
}

// 64-bit variant for very large deltas (safe guard)
static void write_varlen64(FILE* f, uint64_t v) {
    unsigned char buf[10]; int i = 0;
    buf[i++] = v & 0x7F;
    while ((v >>= 7)) buf[i++] = 0x80 | (v & 0x7F);
    while (i--) fputc(buf[i], f);
}

static int append_event(MidiEvent** arr, int* count, int* cap, MidiEvent ev) {
    if (*count >= *cap) {
        int newcap = (*cap == 0) ? 64 : (*cap * 2);
        MidiEvent* tmp = realloc(*arr, newcap * sizeof(MidiEvent));
        if (!tmp) return 0; // allocation failed
        *arr = tmp;
        *cap = newcap;
    }
    (*arr)[(*count)++] = ev;
    return 1;
}

static int cmp_event(const void* a, const void* b) {
    const MidiEvent* A = a;
    const MidiEvent* B = b;
    if (A->tick != B->tick) return (A->tick < B->tick) ? -1 : 1;
    return (A->status & 0xF0) - (B->status & 0xF0);
}

// ---------------- EXPORT ----------------

BOOL export_song_to_midi(const char* path) {
    char buf[16] = { 0 };

    int bpm = 120;

    if (!InputBox("Set BPM", "Target MIDI BPM:", buf, sizeof(buf))) {
        return FALSE;
    }

    bpm = atoi(buf);
    if (bpm <= 0) bpm = 120;

    char midpath[MAX_PATH];
    snprintf(midpath, MAX_PATH, "%s.mid", path);

    FILE* f = fopen(midpath, "wb");
    if (!f) {
        MessageBox2("Failed to open MIDI file", "Export", MB_ICONERROR);
        return FALSE;
    }

    int patterns = cur_song.patterns;
    if (patterns <= 0 || cur_song.order_length <= 0) {
        fclose(f);
        return FALSE;
    }

    int slots = 8;

    MidiEvent** events = calloc(slots, sizeof(MidiEvent*));
    int* ev_count = calloc(slots, sizeof(int));
    int* ev_cap = calloc(slots, sizeof(int));

    if (!events || !ev_count || !ev_cap) {
        fclose(f);
        return FALSE;
    }

    struct song_state sim = pattop_state;

    int prev_pos[8];
    int active_note[8];
    uint64_t note_start[8];

    for (int i = 0; i < 8; i++) {
        prev_pos[i] = sim.chan[i].samp_pos;
        active_note[i] = 0;
        note_start[i] = 0;
    }

    const unsigned int division = 480;
    uint64_t tick = 0;

    const int MAX_CYCLES = 200000;

    // =========================
    // ORDER LOOP
    // =========================
    for (int ord = 0; ord < cur_song.order_length; ord++) {

        int pat = cur_song.order[ord];
        uint64_t pattern_tick_start = tick;

        if (pat < 0 || pat >= patterns) {
            struct song_state sim_empty = sim;

            for (int ch = 0; ch < 8; ch++)
                sim_empty.chan[ch].ptr = NULL;

            int cycles = 0;
            while (do_cycle_no_sound(&sim_empty) && ++cycles < MAX_CYCLES) {
                tick++;
            }
            continue;
        }

        for (int ch = 0; ch < 8; ch++) {
            sim.chan[ch].ptr = cur_song.pattern[pat][ch].track;
            sim.chan[ch].sub_count = 0;
            prev_pos[ch] = sim.chan[ch].samp_pos;
        }

        int cycles = 0;

        // =========================
        // PATTERN LOOP
        // =========================
        while (do_cycle_no_sound(&sim)) {
            if (++cycles > MAX_CYCLES) break;

            for (int ch = 0; ch < 8; ch++) {

                int prev = prev_pos[ch];
                int curr = sim.chan[ch].samp_pos;

                uint64_t mtick = pattern_tick_start + (uint64_t)cycles;

                // NOTE ON
                if (prev < 0 && curr >= 0) {

                    int note = (sim.chan[ch].note.cur >> 8) & 0x7F;

                    // fallback safety (fixes bad pitch cases)
                    if (note < 0 || note > 127) note = 60;

                    active_note[ch] = note;
                    note_start[ch] = mtick;

                    int vel = (sim.chan[ch].total_vol * 127) / 255;
                    if (vel < 1) vel = 1;

                    MidiEvent ev = {
                        mtick,
                        (unsigned char)(0x90 | ch),
                        (unsigned char)note,
                        (unsigned char)vel
                    };

                    append_event(&events[ch], &ev_count[ch], &ev_cap[ch], ev);
                }

                // NOTE OFF
                if (prev >= 0 && curr < 0) {

                    MidiEvent ev = {
                        mtick,
                        (unsigned char)(0x80 | ch),
                        (unsigned char)active_note[ch],
                        0
                    };

                    append_event(&events[ch], &ev_count[ch], &ev_cap[ch], ev);
                }

                prev_pos[ch] = curr;
            }
        }

        tick += (uint64_t)cycles;
    }

    // ================= MIDI HEADER =================

    fwrite("MThd", 1, 4, f);
    write_be32(f, 6);
    write_be16(f, 1);
    write_be16(f, 9);
    write_be16(f, division);

    // ================= TEMPO =================

    uint32_t tempo_us = (uint32_t)(60000000 / bpm);

    fwrite("MTrk", 1, 4, f);
    long lenpos = ftell(f);
    write_be32(f, 0);

    write_varlen(f, 0);

    fputc(0xFF, f);
    fputc(0x51, f);
    fputc(3, f);

    fputc((tempo_us >> 16) & 0xFF, f);
    fputc((tempo_us >> 8) & 0xFF, f);
    fputc(tempo_us & 0xFF, f);

    write_varlen(f, 0);

    fputc(0xFF, f);
    fputc(0x2F, f);
    fputc(0, f);

    long end = ftell(f);
    fseek(f, lenpos, SEEK_SET);
    write_be32(f, end - lenpos - 4);
    fseek(f, end, SEEK_SET);

    // ================= DSP TRACKS =================

    for (int ch = 0; ch < 8; ch++) {

        if (!ev_count[ch]) continue;

        qsort(events[ch], ev_count[ch], sizeof(MidiEvent), cmp_event);

        fwrite("MTrk", 1, 4, f);
        long lpos = ftell(f);
        write_be32(f, 0);

        uint64_t last = 0;

        for (int i = 0; i < ev_count[ch]; i++) {

            uint64_t delta = events[ch][i].tick - last;
            write_varlen64(f, delta);

            fputc(events[ch][i].status, f);
            fputc(events[ch][i].d1, f);
            fputc(events[ch][i].d2, f);

            last = events[ch][i].tick;
        }

        write_varlen(f, 0);
        fputc(0xFF, f);
        fputc(0x2F, f);
        fputc(0, f);

        long cend = ftell(f);
        fseek(f, lpos, SEEK_SET);
        write_be32(f, cend - lpos - 4);
        fseek(f, cend, SEEK_SET);

        free(events[ch]);
    }

    free(events);
    free(ev_count);
    free(ev_cap);

    fclose(f);

    MessageBox2("MIDI export done", "Export", MB_OK);
    return TRUE;
}