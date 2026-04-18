#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "ebmusv2.h"
#include "misc.h"
#include <ctype.h>

extern BOOL export_sf2(const char* path, int* inst_list, int inst_count);

typedef struct {
    uint64_t tick;
    unsigned char status, d1, d2;
    int inst;
    int pat;
} MidiEvent;

static int resolve_inst(struct channel_state* c) {
    if (!c) return -1;
    /* Prefer explicit instrument field if it points to a valid instrument with a sample */
    int inst = (int)c->inst;
    if (inst >= 0 && inst < MAX_INSTRUMENTS) {
        int samp_idx = spc[inst_base + 6*inst];
        if (samp_idx >= 0 && samp_idx < 128 && samp[samp_idx].data)
            return inst;
    }

    /* Fallback: try to find an instrument that refers to the current sample */
    if (c->samp) {
        int sid = c->samp->id;
        for (int i = 0; i < MAX_INSTRUMENTS; i++) {
            if (spc[inst_base + 6*i] == sid) return i;
        }
    }

    return -1;
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
    const MidiEvent* A = (const MidiEvent*)a; const MidiEvent* B = (const MidiEvent*)b;
    if (A->inst != B->inst) return A->inst - B->inst;
    if (A->tick < B->tick) return -1; // Keep comparator stable
    if (A->tick > B->tick) return 1;  // Keep comparator stable
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
    int inst_index[128];
    int inst_list[128];

    MidiEvent* events = NULL;
    int ev_count = 0, ev_cap = 0;

    struct song_state sim = pattop_state;

    int prev[8], note[8], note_inst[8];
    int prev_inst[8];

    for (int i = 0; i < 8; i++) {
        prev[i] = sim.chan[i].samp_pos;
        note[i] = -1;
        note_inst[i] = -1;
        prev_inst[i] = resolve_inst(&sim.chan[i]);
    }

    uint64_t step = 0;

    // ---------- collect ----------
    for (int ord = 0; ord < cur_song.order_length; ord++) {

        int pat = cur_song.order[ord];
        if (pat < 0 || pat >= cur_song.patterns) continue;

        for (int ch = 0; ch < 8; ch++) {
            sim.chan[ch].ptr = cur_song.pattern[pat][ch].track;
            sim.chan[ch].sub_count = 0;
            /* reset per-channel previous state for this pattern so we detect changes correctly */
            prev[ch] = sim.chan[ch].samp_pos;
            prev_inst[ch] = resolve_inst(&sim.chan[ch]);
        }

        while (do_cycle_no_sound(&sim)) {

            for (int ch = 0; ch < 8; ch++) {

                int p = prev[ch];
                int q = sim.chan[ch].samp_pos;

                struct channel_state* c = &sim.chan[ch];
                int raw_inst = resolve_inst(c);
                int old_inst = prev_inst[ch];

                /* Instrument changed on this DSP/channel: end previous note (if any) */
                if (raw_inst != old_inst) {
                    if (note[ch] >= 0 && note_inst[ch] == old_inst) {
                        add(&events, &ev_count, &ev_cap, (MidiEvent){ step, (unsigned char)(0x80 | (ch & 0xF)), (unsigned char)note[ch], 0, old_inst, pat });
                        note[ch] = -1;
                    }

                    /* start new note only if the new instrument is valid and a sample is active */
                    if (raw_inst >= 0 && raw_inst < 128 && q >= 0 && (sim.chan[ch].note.cur >> 8) & 0x7F) {
                        int n = (sim.chan[ch].note.cur >> 8) & 0x7F;
                        int v = (sim.chan[ch].total_vol * 127) / 255;
                        used_inst[raw_inst] = 1;
                        note[ch] = n;
                        note_inst[ch] = raw_inst;
                    add(&events, &ev_count, &ev_cap, (MidiEvent){ step, (unsigned char)(0x90 | (ch & 0xF)), (unsigned char)n, (unsigned char)v, raw_inst, pat });
                    }

                    prev_inst[ch] = raw_inst;
                }

                /* If instrument is invalid, skip adding events that reference it */
                if (raw_inst < 0 || raw_inst >= 128) {
                    prev[ch] = q;
                    continue;
                }

                // NOTE ON (only when sample just started and wasn't handled by inst-change above)
               if (p < 0 && q >= 0 && note[ch] == -1) {

                    int n = (sim.chan[ch].note.cur >> 8) & 0x7F;
                    int v = (sim.chan[ch].total_vol * 127) / 255;

                    note[ch] = n;
                    note_inst[ch] = raw_inst;
                    used_inst[raw_inst] = 1;

                    add(&events, &ev_count, &ev_cap, (MidiEvent){ step, (unsigned char)(0x90 | (ch & 0xF)), (unsigned char)n, (unsigned char)v, raw_inst, pat });
                }

                // NOTE OFF
                if (p >= 0 && q < 0 && note[ch] >= 0) {

                    add(&events, &ev_count, &ev_cap, (MidiEvent){ step, (unsigned char)(0x80 | (ch & 0xF)), (unsigned char)note[ch], 0, note_inst[ch], pat });

                    note[ch] = -1;
                }

                // PAN
                int pan = c->panning.cur >> 8;
                if (pan < 0) pan = 0;
                if (pan > 255) pan = 255;

                int midi_pan = (pan * 127) / 255;

                add(&events, &ev_count, &ev_cap, (MidiEvent){ step, (unsigned char)(0xB0 | (ch & 0xF)), 10, (unsigned char)midi_pan, raw_inst, pat });

                prev[ch] = q;
            }

            step++;
        }
    }

    // ---------- build instrument list (AFTER collect) ----------
    // Build per-pattern+instrument tracks: track id composed as (pattern_index * 256 + inst_index)
    int inst_count = 0;
    // map raw_inst to array of pattern-specific track indices: map[pattern][raw_inst] -> track id
    int map[256][128];
    memset(map, -1, sizeof(map));
    for (int p = 0; p < cur_song.order_length; p++) {
        for (int i = 0; i < 128; i++) {
            // see if this raw instrument appears in this pattern
            for (int e = 0; e < ev_count; e++) {
                if (events[e].pat == p && events[e].inst == i) {
                    if (map[p][i] == -1) {
                        map[p][i] = inst_count;
                        inst_list[inst_count] = i; // store raw inst for this new track
                        inst_count++;
                    }
                    break;
                }
            }
        }
    }

    // ---------- REMAP EVENTS to per-pattern instrument track indices ----------
    for (int i = 0; i < ev_count; i++) {
        int raw = events[i].inst;
        int pat = events[i].pat;
        if (pat < 0 || pat >= cur_song.order_length || raw < 0 || raw >= 128) {
            events[i].inst = -1;
        } else {
            events[i].inst = map[pat][raw];
        }
    }

    // sort by track then tick
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

    if (ev_count == 0) {
        // nothing to write besides tempo
    } else {
        // ---------- tracks ----------
        for (int i = 0; i < inst_count; i++) {

        fwrite("MTrk", 1, 4, f);
        long tp = ftell(f);
        w32(f, 0);

        uint64_t last = 0;

        // Insert a track name and a program change for the first channel used by this instrument
        int first_idx = -1;
        for (int j = 0; j < ev_count; j++) if (events[j].inst == i) { first_idx = j; break; }
        if (first_idx >= 0) {
            // Map back to original instrument index
            int orig_inst = inst_list[i];
            // Track name meta
            char tname[32];
            snprintf(tname, sizeof(tname), "inst_%03d", orig_inst);
            var(f, 0);
            fputc(0xFF, f); fputc(0x03, f); var(f, (uint32_t)strlen(tname));
            fwrite(tname, 1, strlen(tname), f);

            // Program change on first channel used
            unsigned char ch = events[first_idx].status & 0x0F;
            unsigned char program = (unsigned char)(orig_inst % 128);
            var(f, 0);
            fputc(0xC0 | (ch & 0x0F), f);
            fputc(program, f);
        }

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
        /* end for each instrument track */
        }
    /* end if (ev_count != 0) */

    fclose(f);

    /* build SF2 filename from song title (underscores for spaces) and same directory as MIDI */
    char base[MAX_PATH];
    strncpy(base, path, sizeof(base)); base[sizeof(base)-1] = '\0';
    char *dot = strrchr(base, '.'); if (dot) *dot = '\0';

    char dir[MAX_PATH] = {0};
    char *sep = strrchr(base, '\\'); if (!sep) sep = strrchr(base, '/');
    if (sep) {
        size_t dlen = (size_t)(sep - base + 1);
        if (dlen >= sizeof(dir)) dlen = sizeof(dir)-1;
        memcpy(dir, base, dlen);
        dir[dlen] = '\0';
    }

    const char *title_src = NULL;
    if (selected_bgm >= 0 && selected_bgm < NUM_SONGS && bgm_title[selected_bgm] && bgm_title[selected_bgm][0])
        title_src = bgm_title[selected_bgm];
    else title_src = "song";

    char title_sanit[128];
    size_t ti = 0;
    for (size_t k = 0; title_src[k] && ti + 1 < sizeof(title_sanit); k++) {
        unsigned char ch = (unsigned char)title_src[k];
        if (isspace(ch) || ch == '-') title_sanit[ti++] = '_';
        else if (isalnum(ch) || ch == '_') title_sanit[ti++] = (char)ch;
        else title_sanit[ti++] = '_';
    }
    title_sanit[ti] = '\0';

    char sf2base[MAX_PATH];
    if (dir[0]) snprintf(sf2base, sizeof(sf2base), "%s%s", dir, title_sanit);
    else snprintf(sf2base, sizeof(sf2base), "%s", title_sanit);

    export_sf2(sf2base, inst_list, inst_count);

    free(events);

    return TRUE;
}