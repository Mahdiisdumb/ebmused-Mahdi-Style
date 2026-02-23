// The original MinGW project uses a different name for this symbol for some reason.
// Feature check macros from the MinGW-w64 wiki:
// https://sourceforge.net/p/mingw-w64/wiki2/Answer%20Check%20For%20Mingw-w64/
#ifdef __MINGW32__
#include <_mingw.h>
#endif
#if defined(__MINGW32__) && !defined(__MINGW64_VERSION_MAJOR)
#define _ARGC _argc
#define _ARGV _argv
#else
#define _ARGC __argc
#define _ARGV __argv
#endif

#include "id.h"
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <uxtheme.h>
#include <commdlg.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include "ebmusv2.h" // cJSON included here
#include "misc.h"

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

enum {
	MAIN_WINDOW_WIDTH = 720,
	MAIN_WINDOW_HEIGHT = 540,
	TAB_CONTROL_WIDTH = 600,
	TAB_CONTROL_HEIGHT = 25,
	STATUS_WINDOW_HEIGHT = 24,
	CODELIST_WINDOW_WIDTH = 640,
	CODELIST_WINDOW_HEIGHT = 480
};

// Dark mode color constants
#define DM_BG       RGB(32,  32,  32)
#define DM_BG2      RGB(45,  45,  45)
#define DM_FG       RGB(220, 220, 220)
#define DM_ACCENT   RGB(80,  130, 200)
#define DM_BORDER   RGB(70,  70,  70)

struct song cur_song;
BYTE packs_loaded[3] = { 0xFF, 0xFF, 0xFF };
int current_block = -1;
struct song_state pattop_state, state;
int octave = 2;
int midiDevice = -1;
HINSTANCE hinstance;
HWND hwndMain;
HWND hwndStatus;
HMENU hmenu, hcontextmenu;
HWND tab_hwnd[NUM_TABS];
BOOL dark_mode = FALSE;

// Dark mode brushes — created once, reused
static HBRUSH hbrDarkBg = NULL;
static HBRUSH hbrDarkBg2 = NULL;
static HBRUSH hbrDarkEdit = NULL;

static const int INST_TAB = 1;
static int current_tab;
static const char* const tab_class[NUM_TABS] = {
	"ebmused_bgmlist",
	"ebmused_inst",
	"ebmused_editor",
	"ebmused_packs"
};
static const char* const tab_name[NUM_TABS] = {
	"BGM Table",
	"Instruments",
	"Tracker",
	"Data Packs"
};
LRESULT CALLBACK BGMListWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK InstrumentsWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditorWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PackListWndProc(HWND, UINT, WPARAM, LPARAM);
static const WNDPROC tab_wndproc[NUM_TABS] = {
	BGMListWndProc,
	InstrumentsWndProc,
	EditorWndProc,
	PackListWndProc,
};

// ─── Dark mode helpers ────────────────────────────────────────────────────────

// Recreate brushes when dark mode state changes
static void rebuild_dark_brushes(void) {
	if (hbrDarkBg) { DeleteObject(hbrDarkBg);   hbrDarkBg = NULL; }
	if (hbrDarkBg2) { DeleteObject(hbrDarkBg2);  hbrDarkBg2 = NULL; }
	if (hbrDarkEdit) { DeleteObject(hbrDarkEdit);  hbrDarkEdit = NULL; }
	if (dark_mode) {
		hbrDarkBg = CreateSolidBrush(DM_BG);
		hbrDarkBg2 = CreateSolidBrush(DM_BG2);
		hbrDarkEdit = CreateSolidBrush(DM_BG2);
	}
}

// Apply/remove dark title bar via DWM (Windows 10 1809+)
static void apply_dark_titlebar(HWND hwnd, BOOL enable) {
	// DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (19 on older builds, try both)
	BOOL value = enable;
	DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value));
	DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value));
}

// Set a window and all its children to use dark/light theme
static void set_window_theme_recursive(HWND hwnd, BOOL enable) {
	SetWindowTheme(hwnd, enable ? L"DarkMode_Explorer" : L"", NULL);
	for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
		set_window_theme_recursive(child, enable);
	}
}

// Handle WM_CTLCOLOR* for dark mode — call this from any WndProc that hosts controls
LRESULT handle_dark_ctlcolor(UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (!dark_mode) return 0;
	HDC hdc = (HDC)wParam;
	switch (uMsg) {
	case WM_CTLCOLORSTATIC:
		SetTextColor(hdc, DM_FG);
		SetBkColor(hdc, DM_BG);
		return (LRESULT)hbrDarkBg;
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
		SetTextColor(hdc, DM_FG);
		SetBkColor(hdc, DM_BG2);
		return (LRESULT)hbrDarkEdit;
	case WM_CTLCOLORBTN:
		// Buttons need owner-draw for full dark mode; this at least fixes the background bleed
		SetTextColor(hdc, DM_FG);
		SetBkColor(hdc, DM_BG);
		return (LRESULT)hbrDarkBg;
	case WM_CTLCOLORDLG:
		SetTextColor(hdc, DM_FG);
		SetBkColor(hdc, DM_BG);
		return (LRESULT)hbrDarkBg;
	case WM_CTLCOLORSCROLLBAR:
		return (LRESULT)hbrDarkBg2;
	}
	return 0;
}

// Broadcast a repaint + theme update to all child tab windows
static void broadcast_dark_mode(void) {
	for (int i = 0; i < NUM_TABS; i++) {
		if (tab_hwnd[i]) {
			set_window_theme_recursive(tab_hwnd[i], dark_mode);
			RedrawWindow(tab_hwnd[i], NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
		}
	}
}

// ─── File dialog ──────────────────────────────────────────────────────────────

static char filename[MAX_PATH];
static OPENFILENAME ofn;
char* open_dialog(BOOL(WINAPI* func)(LPOPENFILENAME),
	char* filter, char* extension, DWORD flags)
{
	*filename = '\0';
	ofn.lStructSize = sizeof ofn;
	ofn.hwndOwner = hwndMain;
	ofn.lpstrFilter = filter;
	ofn.lpstrDefExt = extension;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = flags | OFN_NOCHANGEDIR;
	return func(&ofn) ? filename : NULL;
}

// ─── JSON export (cJSON) ──────────────────────────────────────────────────────

// Builds a cJSON byte array from raw BYTE data
static cJSON* make_byte_array(const BYTE* data, int len) {
	cJSON* arr = cJSON_CreateArray();
	for (int i = 0; i < len; i++)
		cJSON_AddItemToArray(arr, cJSON_CreateNumber(data[i]));
	return arr;
}

static void write_spc_json(FILE* f) {
	extern int inst_base;
	extern WORD sample_ptr_base;

	cJSON* root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "music_addr", cur_song.address);
	cJSON_AddNumberToObject(root, "order_length", cur_song.order_length);

	// Order
	cJSON* order_arr = cJSON_CreateArray();
	for (int i = 0; i < cur_song.order_length; i++)
		cJSON_AddItemToArray(order_arr, cJSON_CreateNumber(cur_song.order[i]));
	cJSON_AddItemToObject(root, "order", order_arr);

	// Patterns — object keyed by pattern index
	cJSON* patterns_obj = cJSON_CreateObject();
	for (int p = 0; p < cur_song.patterns; p++) {
		cJSON* pattern_arr = cJSON_CreateArray();
		for (int ch = 0; ch < 8; ch++) {
			struct track* t = &cur_song.pattern[p][ch];
			if (!t->track) {
				cJSON_AddItemToArray(pattern_arr, cJSON_CreateNull());
			}
			else {
				cJSON_AddItemToArray(pattern_arr, make_byte_array(t->track, t->size + 1));
			}
		}
		char key[16];
		sprintf(key, "%d", p);
		cJSON_AddItemToObject(patterns_obj, key, pattern_arr);
	}
	cJSON_AddItemToObject(root, "patterns", patterns_obj);

	// Subs
	cJSON* subs_arr = cJSON_CreateArray();
	for (int s = 0; s < cur_song.subs; s++) {
		struct track* t = &cur_song.sub[s];
		if (!t->track) {
			cJSON_AddItemToArray(subs_arr, cJSON_CreateNull());
		}
		else {
			cJSON_AddItemToArray(subs_arr, make_byte_array(t->track, t->size + 1));
		}
	}
	cJSON_AddItemToObject(root, "subs", subs_arr);

	cJSON_AddNumberToObject(root, "inst_base", inst_base);
	cJSON_AddNumberToObject(root, "sample_ptr_base", sample_ptr_base);

	char* json_str = cJSON_Print(root);
	if (json_str) {
		fputs(json_str, f);
		cJSON_free(json_str);
	}
	cJSON_Delete(root);
}

// ─── JSON import (cJSON) ──────────────────────────────────────────────────────

static void import_spc_json_from_file(const char* path) {
	extern int inst_base;
	extern WORD sample_ptr_base;

	// ── Read file into memory ──────────────────────────────────────────────────
	FILE* f = fopen(path, "rb");
	if (!f) {
		MessageBox2(strerror(errno), "Import SPC JSON", MB_ICONEXCLAMATION);
		return;
	}
	fseek(f, 0, SEEK_END);
	long flen = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* data = malloc(flen + 1);
	if (!data) {
		fclose(f);
		MessageBox2("Out of memory", "Import SPC JSON", MB_ICONEXCLAMATION);
		return;
	}
	fread(data, 1, flen, f);
	data[flen] = '\0';
	fclose(f);

	cJSON* root = cJSON_Parse(data);
	free(data);
	if (!root) {
		const char* err = cJSON_GetErrorPtr();
		char msg[256];
		sprintf(msg, "Failed to parse JSON.\n%.200s", err ? err : "Unknown error");
		MessageBox2(msg, "Import SPC JSON", MB_ICONEXCLAMATION);
		return;
	}

	// ── Read scalar fields ────────────────────────────────────────────────────
	cJSON* item;

	item = cJSON_GetObjectItem(root, "music_addr");
	WORD music_addr = (item && cJSON_IsNumber(item)) ? (WORD)item->valueint : 0;

	item = cJSON_GetObjectItem(root, "order_length");
	int order_length = (item && cJSON_IsNumber(item)) ? item->valueint : 0;

	item = cJSON_GetObjectItem(root, "inst_base");
	if (item && cJSON_IsNumber(item)) inst_base = item->valueint;

	item = cJSON_GetObjectItem(root, "sample_ptr_base");
	if (item && cJSON_IsNumber(item)) sample_ptr_base = (WORD)item->valueint;

	// ── Read order array ──────────────────────────────────────────────────────
	cJSON* order_arr = cJSON_GetObjectItem(root, "order");
	int actual_order_len = (order_arr && cJSON_IsArray(order_arr)) ? cJSON_GetArraySize(order_arr) : 0;
	// Trust actual array size over the stored order_length to avoid OOB writes
	if (order_length > actual_order_len) order_length = actual_order_len;

	// ── Read patterns object ──────────────────────────────────────────────────
	// JSON structure: patterns is an OBJECT keyed "0","1","2"...
	// Each value is an ARRAY of 8 items, each item is either an ARRAY of bytes or null.
	cJSON* patterns_obj = cJSON_GetObjectItem(root, "patterns");
	int pattern_count = 0;
	if (patterns_obj && cJSON_IsObject(patterns_obj)) {
		// Count keys — they should be "0".."N-1"
		cJSON* child = patterns_obj->child;
		while (child) { pattern_count++; child = child->next; }
	}

	// ── Read subs array ───────────────────────────────────────────────────────
	// JSON structure: subs is an ARRAY of byte-arrays (or null entries)
	cJSON* subs_arr = cJSON_GetObjectItem(root, "subs");
	int sub_count = (subs_arr && cJSON_IsArray(subs_arr)) ? cJSON_GetArraySize(subs_arr) : 0;

	// ── Rebuild cur_song ──────────────────────────────────────────────────────
	free_song(&cur_song);

	cur_song.address = music_addr;
	cur_song.order_length = order_length;

	for (int i = 0; i < order_length; i++) {
		cJSON* el = cJSON_GetArrayItem(order_arr, i);
		cur_song.order[i] = (el && cJSON_IsNumber(el)) ? (BYTE)el->valueint : 0;
	}

	// Patterns
	cur_song.patterns = pattern_count;
	for (int p = 0; p < pattern_count; p++) {
		char key[16];
		sprintf(key, "%d", p);
		cJSON* pat = cJSON_GetObjectItem(patterns_obj, key);
		for (int ch = 0; ch < 8; ch++) {
			struct track* t = &cur_song.pattern[p][ch];
			t->track = NULL;
			t->size = 0;

			cJSON* ch_item = (pat && cJSON_IsArray(pat)) ? cJSON_GetArrayItem(pat, ch) : NULL;
			if (!ch_item || cJSON_IsNull(ch_item)) continue;
			if (!cJSON_IsArray(ch_item)) continue;

			int len = cJSON_GetArraySize(ch_item);
			if (len <= 0) continue;

			t->track = malloc(len);
			if (!t->track) continue; // skip on alloc failure, don't crash
			t->size = len - 1;       // size excludes the terminating byte

			for (int b = 0; b < len; b++) {
				cJSON* byte_item = cJSON_GetArrayItem(ch_item, b);
				t->track[b] = (byte_item && cJSON_IsNumber(byte_item)) ? (BYTE)byte_item->valueint : 0;
			}
		}
	}

	// Subs
	cur_song.subs = sub_count;
	for (int s = 0; s < sub_count; s++) {
		struct track* t = &cur_song.sub[s];
		t->track = NULL;
		t->size = 0;

		cJSON* sub_item = cJSON_GetArrayItem(subs_arr, s);
		if (!sub_item || cJSON_IsNull(sub_item)) continue;
		if (!cJSON_IsArray(sub_item)) continue;

		int len = cJSON_GetArraySize(sub_item);
		if (len <= 0) continue;

		t->track = malloc(len);
		if (!t->track) continue;
		t->size = len - 1;

		for (int b = 0; b < len; b++) {
			cJSON* byte_item = cJSON_GetArrayItem(sub_item, b);
			t->track[b] = (byte_item && cJSON_IsNumber(byte_item)) ? (BYTE)byte_item->valueint : 0;
		}
	}

	cJSON_Delete(root);

	initialize_state();
	cur_song.changed = TRUE;
	save_cur_song_to_pack();
	SendMessage(tab_hwnd[current_tab], WM_SONG_IMPORTED, 0, 0);
	SendMessage(tab_hwnd[current_tab], WM_SONG_LOADED, 0, 0);
}

// ─── Misc helpers (unchanged) ─────────────────────────────────────────────────

BOOL get_original_rom() {
	char* file = open_dialog(GetOpenFileName,
		"SNES ROM files (*.smc, *.sfc)\0*.smc;*.sfc\0All Files\0*.*\0",
		NULL,
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY);
	BOOL ret = file && open_orig_rom(file);
	metadata_changed |= ret;
	return ret;
}

static void tab_selected(int new) {
	if (new < 0 || new >= NUM_TABS) return;
	current_tab = new;
	format_status(0, "%s", "");

	for (int i = 0; i < NUM_TABS; i++) {
		if (tab_hwnd[i]) {
			DestroyWindow(tab_hwnd[i]);
			tab_hwnd[i] = NULL;
		}
	}

	RECT rc;
	GetClientRect(hwndMain, &rc);
	int status_height = hwndStatus ? STATUS_WINDOW_HEIGHT : 0;
	tab_hwnd[new] = CreateWindow(tab_class[new], NULL,
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
		0, scale_y(TAB_CONTROL_HEIGHT), rc.right, rc.bottom - scale_y(TAB_CONTROL_HEIGHT + status_height),
		hwndMain, NULL, hinstance, NULL);

	// Apply current dark mode to the new tab immediately
	if (dark_mode) {
		set_window_theme_recursive(tab_hwnd[new], TRUE);
		RedrawWindow(tab_hwnd[new], NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}

	SendMessage(tab_hwnd[new], rom ? WM_ROM_OPENED : WM_ROM_CLOSED, 0, 0);
	SendMessage(tab_hwnd[new], cur_song.order_length ? WM_SONG_LOADED : WM_SONG_NOT_LOADED, 0, 0);
}

static void import() {
	if (packs_loaded[2] >= NUM_PACKS) {
		MessageBox2("No song pack selected", "Import", MB_ICONEXCLAMATION);
		return;
	}

	char* file = open_dialog(GetOpenFileName,
		"EarthBound Music files (*.ebm)\0*.ebm\0All Files\0*.*\0", NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY);
	if (!file) return;

	FILE* f = fopen(file, "rb");
	if (!f) {
		MessageBox2(strerror(errno), "Import", MB_ICONEXCLAMATION);
		return;
	}

	struct block b;
	if (!fread(&b, 4, 1, f) || b.spc_address + b.size > 0x10000 || _filelength(_fileno(f)) != 4 + b.size) {
		MessageBox2("File is not an EBmused export", "Import", MB_ICONEXCLAMATION);
		goto err1;
	}
	b.data = malloc(b.size);
	fread(b.data, b.size, 1, f);
	new_block(&b);
	SendMessage(tab_hwnd[current_tab], WM_SONG_IMPORTED, 0, 0);
err1:
	fclose(f);
}

// ─── SPC parsing (unchanged) ──────────────────────────────────────────────────

struct spcDetails {
	WORD music_table_addr;
	WORD instrument_table_addr;
	WORD music_addr;
	BYTE music_index;
};

BOOL try_parse_music_table(const BYTE* spc, struct spcDetails* out_details) {
	if (memcmp(spc, "\x1C\x5D\xF5", 3) != 0) return FALSE;
	WORD addr_hi = *((WORD*)&spc[3]);
	if (spc[5] == 0xF0) spc += 2;
	if (spc[5] != 0xFD) return FALSE;
	if (memcmp(&spc[6], "\xD0\x03\xC4", 3) == 0 && spc[10] == 0x6F) spc += 5;
	if (spc[6] != 0xF5) return FALSE;
	WORD addr_lo = *((WORD*)&spc[7]);
	if (spc[9] != 0xDA || spc[10] != 0x40) return FALSE;
	if (addr_lo != addr_hi - 1) return FALSE;
	out_details->music_table_addr = addr_lo;
	return TRUE;
}

BOOL try_parse_music_address(const BYTE* spc, struct spcDetails* out_details) {
	WORD loop_addr = *(WORD*)&spc[0x40];
	WORD* terminator = (WORD*)&spc[loop_addr];
	while (terminator[0]) {
		if ((BYTE*)terminator < &spc[0x10000 - 16 - 2]
			&& terminator - (WORD*)&spc[loop_addr] <= 0x100) {
			terminator++;
		}
		else {
			return FALSE;
		}
	}

	typedef WORD PATTERN[8];
	PATTERN* patterns = (PATTERN*)&terminator[1];
	unsigned int numPatterns = 0;
	const unsigned int maxPatterns = (&spc[0xFFFF] - (BYTE*)patterns) / sizeof(PATTERN);
	for (unsigned int i = 0;
		i < maxPatterns
		&& (patterns[i][0] < patterns[i][1] || !patterns[i][1])
		&& (patterns[i][1] < patterns[i][2] || !patterns[i][2])
		&& (patterns[i][2] < patterns[i][3] || !patterns[i][3])
		&& (patterns[i][3] < patterns[i][4] || !patterns[i][4])
		&& (patterns[i][4] < patterns[i][5] || !patterns[i][5])
		&& (patterns[i][5] < patterns[i][6] || !patterns[i][6])
		&& (patterns[i][6] < patterns[i][7] || !patterns[i][7]);
		i++) {
		numPatterns = i + 1;
	}

	if (patterns[0][0] <= 0xFF || numPatterns == 0 || numPatterns >= maxPatterns) return FALSE;

	WORD* music_addr_ptr = (WORD*)&spc[loop_addr];
	BOOL patternExists = TRUE;
	for (WORD* prev = &music_addr_ptr[-1]; prev && patternExists; prev--) {
		patternExists = FALSE;
		for (unsigned int i = 0; i < numPatterns; i++) {
			if (patterns[i] == (WORD*)&spc[*prev]) {
				patternExists = TRUE;
				music_addr_ptr = prev;
				break;
			}
		}
	}

	if ((BYTE*)music_addr_ptr - spc <= 0xFF) return FALSE;
	out_details->music_addr = (BYTE*)music_addr_ptr - spc;
	return TRUE;
}

BOOL try_parse_inst_directory(const BYTE* spc, struct spcDetails* out_details) {
	if (memcmp(spc, "\xCF\xDA\x14\x60\x98", 5) == 0 && memcmp(&spc[6], "\x14\x98", 2) == 0 && spc[9] == 0x15) {
		out_details->instrument_table_addr = spc[5] | (spc[8] << 8);
		return TRUE;
	}
	return FALSE;
}

enum SPC_RESULTS { HAS_MUSIC = 1 << 0, HAS_MUSIC_TABLE = 1 << 1, HAS_INSTRUMENTS = 1 << 2 };

enum SPC_RESULTS try_parse_spc(const BYTE* spc, struct spcDetails* out_details) {
	BOOL foundMusic = FALSE, foundMusicTable = FALSE, foundInst = FALSE;
	for (int i = 0; i < 0xFF00 && !(foundMusicTable && foundInst); i++) {
		if (!foundMusicTable && spc[i] == 0x1C)
			foundMusicTable = try_parse_music_table(&spc[i], out_details);
		else if (!foundInst && spc[i] == 0xCF)
			foundInst = try_parse_inst_directory(&spc[i], out_details);
	}

	foundMusic = try_parse_music_address(spc, out_details);

	if (!foundMusic && foundMusicTable) {
		BYTE bgm_index = spc[0x00] ? spc[0x00]
			: spc[0x04] ? spc[0x04]
			: spc[0x08] ? spc[0x08]
			: spc[0xF3] ? spc[0xF3]
			: spc[0xF4];
		if (bgm_index) {
			out_details->music_index = bgm_index;
			out_details->music_addr = ((WORD*)&spc[out_details->music_table_addr])[bgm_index];
			foundMusic = TRUE;
		}
		else {
			WORD music_addr = *((WORD*)&spc[0x40]);
			WORD closestDiff = 0xFFFF;
			for (unsigned int i = 0; i < 0xFF; i++) {
				WORD addr = ((WORD*)&spc[out_details->music_table_addr])[i];
				if (music_addr < addr && addr - music_addr < closestDiff) {
					closestDiff = addr - music_addr;
					bgm_index = i;
				}
			}
			if (music_addr > 0xFF) {
				out_details->music_addr = music_addr;
				out_details->music_index = 0;
				foundMusic = TRUE;
			}
		}
	}

	enum SPC_RESULTS results = 0;
	if (foundMusicTable) results |= HAS_MUSIC_TABLE;
	if (foundMusic)      results |= HAS_MUSIC;
	if (foundInst)       results |= HAS_INSTRUMENTS;
	return results;
}

static void import_spc() {
	char* file = open_dialog(GetOpenFileName,
		"SPC Savestates (*.spc)\0*.spc\0All Files\0*.*\0",
		NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY);
	if (!file) return;

	FILE* f = fopen(file, "rb");
	if (!f) { MessageBox2(strerror(errno), "Import", MB_ICONEXCLAMATION); return; }

	BYTE backup_spc[0x10000];
	memcpy(backup_spc, spc, 0x10000);
	WORD original_sample_ptr_base = sample_ptr_base;
	BYTE dsp[0x80];

	if (fseek(f, 0x100, SEEK_SET) == 0
		&& fread(spc, 0x10000, 1, f) == 1
		&& fread(dsp, 0x80, 1, f) == 1) {
		sample_ptr_base = dsp[0x5D] << 8;
		free_samples();
		decode_samples(&spc[sample_ptr_base]);

		struct spcDetails details;
		enum SPC_RESULTS results = try_parse_spc(spc, &details);
		if (results) {
			if (results & HAS_INSTRUMENTS) {
				printf("Instrument table: %#X\n", details.instrument_table_addr);
				inst_base = details.instrument_table_addr;
			}
			if (results & HAS_MUSIC) {
				printf("Music addr: %#X\n", details.music_addr);
				free_song(&cur_song);
				decompile_song(&cur_song, details.music_addr, 0xffff);
			}
			initialize_state();
			cur_song.changed = TRUE;
			save_cur_song_to_pack();
			SendMessage(tab_hwnd[current_tab], WM_SONG_IMPORTED, 0, 0);
		}
		else {
			memcpy(spc, backup_spc, 0x10000);
			sample_ptr_base = original_sample_ptr_base;
			free_samples();
			decode_samples(&spc[sample_ptr_base]);
			MessageBox2("Could not parse SPC.", "SPC Import", MB_ICONEXCLAMATION);
		}
	}
	else {
		memcpy(spc, backup_spc, 0x10000);
		MessageBox2(feof(f) ? "End-of-file reached" : "Error reading SPC file", "Import", MB_ICONEXCLAMATION);
	}
	fclose(f);
}

static void export_ebm() {
	struct block* b = save_cur_song_to_pack();
	if (!b) { MessageBox2("No song loaded", "Export", MB_ICONEXCLAMATION); return; }

	char* file = open_dialog(GetSaveFileName, "EarthBound Music files (*.ebm)\0*.ebm\0", "ebm", OFN_OVERWRITEPROMPT);
	if (!file) return;

	FILE* f = fopen(file, "wb");
	if (!f) { MessageBox2(strerror(errno), "Export", MB_ICONEXCLAMATION); return; }
	fwrite(b, 4, 1, f);
	fwrite(b->data, b->size, 1, f);
	fclose(f);
}

static void write_spc(FILE* f);
static void export_spc() {
	if (cur_song.order_length > 0) {
		char* file = open_dialog(GetSaveFileName, "SPC files (*.spc)\0*.spc\0", "spc", OFN_OVERWRITEPROMPT);
		if (file) {
			FILE* f = fopen(file, "wb");
			if (f) { write_spc(f); fclose(f); }
			else MessageBox2(strerror(errno), "Export SPC", MB_ICONEXCLAMATION);
		}
	}
	else {
		MessageBox2("No song loaded", "Export SPC", MB_ICONEXCLAMATION);
	}
}

static void write_spc(FILE* f) {
	HRSRC res = FindResource(hinstance, MAKEINTRESOURCE(IDRC_SPC), RT_RCDATA);
	HGLOBAL res_handle = res ? LoadResource(NULL, res) : NULL;
	if (!res_handle) { MessageBox2("Template SPC could not be loaded", "Export SPC", MB_ICONEXCLAMATION); return; }

	BYTE* res_data = (BYTE*)LockResource(res_handle);
	DWORD spc_size = SizeofResource(NULL, res);
	BYTE* new_spc = memcpy(malloc(spc_size), res_data, spc_size);

	BYTE spc_copy[0x10000];
	memcpy(spc_copy, spc, 0x10000);
	struct SamplePointers { WORD start, loop; } *sample_pointers = (struct SamplePointers*)&spc_copy[sample_ptr_base];

	enum {
		SPC_HEADER_SIZE = 0x100,
		BRR_BLOCK_SIZE = 9,
		NUM_INSTRUMENTS = 80,
		BUFFER = 0x10
	};

	const WORD dstMusic = 0x3100;
	const int  music_size = compile_song(&cur_song);
	memcpy(&spc[SPC_HEADER_SIZE + dstMusic], &spc_copy[cur_song.address], music_size);
	cur_song.address = dstMusic;
	compile_song(&cur_song);

	const WORD inst_size = NUM_INSTRUMENTS * 6;
	const WORD REMAINDER = 0x100 - ((dstMusic + music_size) & 0xFF);
	const WORD dstSamplePtrs = dstMusic + music_size + REMAINDER;
	const WORD dstInstruments = dstSamplePtrs + 0x4 * NUM_INSTRUMENTS + BUFFER;
	const WORD dstSamples = dstInstruments + inst_size + BUFFER;

	memset(&new_spc[SPC_HEADER_SIZE + dstMusic], 0, dstSamples - dstMusic);
	memcpy(&new_spc[SPC_HEADER_SIZE + dstMusic], &spc[cur_song.address], music_size);
	memcpy(spc, spc_copy, 0x10000);
	memcpy(&new_spc[SPC_HEADER_SIZE + dstInstruments], &spc_copy[inst_base], inst_size);

	struct { WORD src, len, dst; } sampleMap[NUM_INSTRUMENTS];
	WORD offset = 0;
	for (unsigned int i = 0; i < NUM_INSTRUMENTS && sample_pointers[i].start < 0xFF00 && sample_pointers[i].start > 0xFF; i++) {
		const WORD src_addr = sample_pointers[i].start;
		WORD len = sample_pointers[i].loop - sample_pointers[i].start;
		WORD dst_addr = 0;

		for (unsigned int j = 0; j < i; j++) {
			if (sampleMap[j].src == src_addr && sampleMap[j].len == len) {
				dst_addr = sampleMap[j].dst;
				len = sampleMap[j].len;
				break;
			}
		}

		if (!dst_addr) {
			unsigned int num_brr = count_brr_blocks(spc_copy, src_addr);
			if (num_brr == 0) {
				printf("Invalid BRR block at instrument %d\n", i);
			}
			else if (offset < 0xFFFF - dstSamples - num_brr * BRR_BLOCK_SIZE) {
				dst_addr = dstSamples + offset;
				memcpy(&new_spc[SPC_HEADER_SIZE + dst_addr], &spc_copy[src_addr], num_brr * BRR_BLOCK_SIZE);
				offset += num_brr * BRR_BLOCK_SIZE;
			}
		}

		sampleMap[i].src = src_addr;
		sampleMap[i].dst = dst_addr;
		sampleMap[i].len = len;
		WORD loop_addr = dst_addr + len;
		memcpy(&new_spc[SPC_HEADER_SIZE + dstSamplePtrs + 0x4 * i], &dst_addr, 2);
		memcpy(&new_spc[SPC_HEADER_SIZE + dstSamplePtrs + 0x4 * i + 0x2], &loop_addr, 2);
	}

	const WORD repeat_address = dstMusic + 0x2 * cur_song.repeat_pos;
	memcpy(&new_spc[SPC_HEADER_SIZE + 0x40], &repeat_address, 2);
	const BYTE bgm = selected_bgm + 1;
	memcpy(&new_spc[SPC_HEADER_SIZE + 0xF4], &bgm, 1);
	memcpy(&new_spc[0x2F48 + 0x2 * bgm], &dstMusic, 2);
	new_spc[SPC_HEADER_SIZE + 0x972] = dstInstruments & 0xFF;
	new_spc[SPC_HEADER_SIZE + 0x975] = dstInstruments >> 8;
	new_spc[SPC_HEADER_SIZE + 0x1005D] = dstSamplePtrs >> 8;
	new_spc[SPC_HEADER_SIZE + 0x52A] = dstSamplePtrs >> 8;

	fwrite(new_spc, spc_size, 1, f);
	free(new_spc);
}

BOOL save_all_packs() {
	char buf[60];
	save_cur_song_to_pack();
	int  packs = 0;
	BOOL success = TRUE;
	for (int i = 0; i < NUM_PACKS; i++) {
		if (inmem_packs[i].status & IPACK_CHANGED) {
			BOOL saved = save_pack(i);
			success &= saved;
			packs += saved;
		}
	}
	if (packs) {
		SendMessage(tab_hwnd[current_tab], WM_PACKS_SAVED, 0, 0);
		sprintf(buf, "%d pack(s) saved", packs);
		MessageBox2(buf, "Save", MB_OK);
	}
	save_metadata();
	return success;
}

static BOOL validate_playable(void) {
	if (cur_song.order_length == 0) {
		MessageBox2("No song loaded", "Play", MB_ICONEXCLAMATION);
		return FALSE;
	}
	else if (samp[0].data == NULL) {
		MessageBox2("No instruments loaded", "Play", MB_ICONEXCLAMATION);
		return FALSE;
	}
	return TRUE;
}

// ─── Main window procedure ────────────────────────────────────────────────────

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	// Forward all WM_CTLCOLOR* to the dark mode handler first
	switch (uMsg) {
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
	case WM_CTLCOLORBTN:
	case WM_CTLCOLORDLG:
	case WM_CTLCOLORSCROLLBAR: {
		LRESULT res = handle_dark_ctlcolor(uMsg, wParam, lParam);
		if (res) return res;
		break;
	}
	}

	switch (uMsg) {
	case MM_WOM_OPEN: case MM_WOM_CLOSE: case MM_WOM_DONE:
		winmm_message(uMsg);
		break;

	case WM_CREATE: {
		HWND tabs = CreateWindow(WC_TABCONTROL, NULL,
			WS_CHILD | WS_VISIBLE | TCS_BUTTONS, 0, 0,
			scale_x(TAB_CONTROL_WIDTH), scale_y(TAB_CONTROL_HEIGHT),
			hWnd, NULL, hinstance, NULL);

		TC_ITEM item = { 0 };
		item.mask = TCIF_TEXT;
		for (int i = 0; i < NUM_TABS; i++) {
			item.pszText = (char*)tab_name[i];
			TabCtrl_InsertItem(tabs, i, &item);
		}
		SendMessage(tabs, WM_SETFONT, tabs_font(), TRUE);
		break;
	}

	case WM_SIZE: {
		int tabs_height = scale_y(TAB_CONTROL_HEIGHT);
		int status_height = hwndStatus ? scale_y(STATUS_WINDOW_HEIGHT) : 0;
		MoveWindow(tab_hwnd[current_tab], 0, tabs_height,
			LOWORD(lParam), HIWORD(lParam) - tabs_height - status_height, TRUE);
		SendMessage(hwndStatus, uMsg, wParam, lParam);
		break;
	}

	case WM_ERASEBKGND:
		if (dark_mode) {
			HDC hdc = (HDC)wParam;
			RECT rc;
			GetClientRect(hWnd, &rc);
			FillRect(hdc, &rc, hbrDarkBg);
			return 1;
		}
		break;

	case WM_COMMAND: {
		WORD id = LOWORD(wParam);
		switch (id) {
		case ID_OPEN: {
			char* file = open_dialog(GetOpenFileName,
				"SNES ROM files (*.smc, *.sfc)\0*.smc;*.sfc\0All Files\0*.*\0",
				NULL, OFN_FILEMUSTEXIST);
			if (file && open_rom(file, ofn.Flags & OFN_READONLY)) {
				SendMessage(tab_hwnd[current_tab], WM_ROM_CLOSED, 0, 0);
				SendMessage(tab_hwnd[current_tab], WM_ROM_OPENED, 0, 0);
			}
			break;
		}
		case ID_SAVE_ALL: save_all_packs(); break;
		case ID_CLOSE:
			if (!close_rom()) break;
			SendMessage(tab_hwnd[current_tab], WM_ROM_CLOSED, 0, 0);
			SetWindowText(hWnd, "EarthBound Music Editor");
			break;
		case ID_IMPORT:     import();     break;
		case ID_IMPORT_SPC: import_spc(); break;
		case ID_IMPORT_SPC_JSON: {
			char* file = open_dialog(GetOpenFileName,
				"SPC JSON files (*.json)\0*.json\0All Files\0*.*\0",
				NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY);
			if (file) import_spc_json_from_file(file);
			break;
		}
		case ID_EXPORT: export_ebm(); break;
		case ID_EXPORT_SPC_JSON: {
			if (cur_song.order_length > 0) {
				char* file = open_dialog(GetSaveFileName,
					"SPC JSON files (*.json)\0*.json\0", "json", OFN_OVERWRITEPROMPT);
				if (file) {
					FILE* f = fopen(file, "wb");
					if (f) { write_spc_json(f); fclose(f); }
					else MessageBox2(strerror(errno), "Export SPC JSON", MB_ICONEXCLAMATION);
				}
			}
			else {
				MessageBox2("No song loaded", "Export SPC JSON", MB_ICONEXCLAMATION);
			}
			break;
		}
		case ID_EXPORT_SPC: export_spc(); break;
		case ID_EXIT: DestroyWindow(hWnd); break;
		case ID_OPTIONS: {
			extern BOOL CALLBACK OptionsDlgProc(HWND, UINT, WPARAM, LPARAM);
			DialogBox(hinstance, MAKEINTRESOURCE(IDD_OPTIONS), hWnd, OptionsDlgProc);
			if (current_tab == INST_TAB) start_playing();
			break;
		}
		case ID_DARK_MODE: {
			dark_mode = !dark_mode;
			CheckMenuItem(hmenu, ID_DARK_MODE, dark_mode ? MF_CHECKED : MF_UNCHECKED);

			rebuild_dark_brushes();
			apply_dark_titlebar(hwndMain, dark_mode);

			// Update main window background class brush
			HBRUSH bg = dark_mode ? hbrDarkBg : (HBRUSH)(COLOR_3DFACE + 1);
			SetClassLongPtr(hwndMain, GCLP_HBRBACKGROUND, (LONG_PTR)bg);

			// Apply theme to all registered window classes and repaint
			set_window_theme_recursive(hwndMain, dark_mode);
			broadcast_dark_mode();

			RedrawWindow(hwndMain, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
			break;
		}
		case ID_CUT:
		case ID_COPY:
		case ID_PASTE:
		case ID_DELETE:
		case ID_SPLIT_PATTERN:
		case ID_JOIN_PATTERNS:
		case ID_MAKE_SUBROUTINE:
		case ID_UNMAKE_SUBROUTINE:
		case ID_TRANSPOSE:
		case ID_CLEAR_SONG:
		case ID_ZOOM_OUT:
		case ID_ZOOM_IN:
		case ID_INCREMENT_DURATION:
		case ID_DECREMENT_DURATION:
		case ID_SET_DURATION_1:
		case ID_SET_DURATION_2:
		case ID_SET_DURATION_3:
		case ID_SET_DURATION_4:
		case ID_SET_DURATION_5:
		case ID_SET_DURATION_6:
			editor_command(id);
			break;
		case ID_PLAY:
			if (validate_playable()) {
				start_playing();
				EnableMenuItem(hmenu, ID_STOP, MF_ENABLED);
			}
			break;
		case ID_STOP:
			if (current_tab == INST_TAB) stop_capturing_audio();
			else {
				stop_playing();
				EnableMenuItem(hmenu, ID_PLAY, MF_ENABLED);
			}
			break;
		case ID_CAPTURE_AUDIO: {
			if (current_tab == INST_TAB) {
				if (is_capturing_audio()) stop_capturing_audio();
				else start_capturing_audio();
			}
			else {
				if (is_capturing_audio()) stop_capturing_audio();
				else if (validate_playable() && start_capturing_audio()) {
					start_playing();
					EnableMenuItem(hmenu, ID_STOP, MF_ENABLED);
				}
			}
			break;
		}
		case ID_OCTAVE_1: case ID_OCTAVE_1 + 1: case ID_OCTAVE_1 + 2:
		case ID_OCTAVE_1 + 3: case ID_OCTAVE_1 + 4:
			octave = id - ID_OCTAVE_1;
			CheckMenuRadioItem(hmenu, ID_OCTAVE_1, ID_OCTAVE_1 + 4, id, MF_BYCOMMAND);
			break;
		case ID_HELP:
			CreateWindow("ebmused_codelist", "Code list",
				WS_OVERLAPPEDWINDOW | WS_VISIBLE,
				CW_USEDEFAULT, CW_USEDEFAULT,
				scale_x(CODELIST_WINDOW_WIDTH), scale_y(CODELIST_WINDOW_HEIGHT),
				NULL, NULL, hinstance, NULL);
			break;
		case ID_ABOUT: {
			extern BOOL CALLBACK AboutDlgProc(HWND, UINT, WPARAM, LPARAM);
			DialogBox(hinstance, MAKEINTRESOURCE(IDD_ABOUT), hWnd, AboutDlgProc);
			break;
		}
		case ID_STATUS_BAR: {
			if (hwndStatus) {
				DestroyWindow(hwndStatus);
				hwndStatus = NULL;
				CheckMenuItem(hmenu, ID_STATUS_BAR, MF_UNCHECKED);
				RECT rc; GetClientRect(hwndMain, &rc);
				MoveWindow(tab_hwnd[current_tab], 0, scale_y(TAB_CONTROL_HEIGHT),
					rc.right, rc.bottom - scale_y(TAB_CONTROL_HEIGHT), TRUE);
			}
			else {
				hwndStatus = CreateStatusWindow(WS_CHILD | WS_VISIBLE, NULL, hwndMain, IDS_STATUS);
				CheckMenuItem(hmenu, ID_STATUS_BAR, MF_CHECKED);
				RECT rc; GetClientRect(hwndMain, &rc);
				MoveWindow(tab_hwnd[current_tab], 0, scale_y(TAB_CONTROL_HEIGHT),
					rc.right, rc.bottom - scale_y(TAB_CONTROL_HEIGHT + STATUS_WINDOW_HEIGHT), TRUE);
			}
			break;
		}
		default: printf("Command %d not yet implemented\n", id); break;
		}
		break;
	}

	case WM_NOTIFY: {
		NMHDR* pnmh = (LPNMHDR)lParam;
		if (pnmh->code == TCN_SELCHANGE)
			tab_selected(TabCtrl_GetCurSel(pnmh->hwndFrom));
		break;
	}

	case WM_CLOSE:
		if (!close_rom()) break;
		DestroyWindow(hWnd);
		break;

	case WM_DESTROY:
		// Clean up dark mode brushes
		if (hbrDarkBg) { DeleteObject(hbrDarkBg);   hbrDarkBg = NULL; }
		if (hbrDarkBg2) { DeleteObject(hbrDarkBg2);  hbrDarkBg2 = NULL; }
		if (hbrDarkEdit) { DeleteObject(hbrDarkEdit);  hbrDarkEdit = NULL; }
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

// ─── WinMain ──────────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	hinstance = hInstance;
	WNDCLASS wc = { 0 };
	MSG msg;

	wc.lpfnWndProc = MainWndProc;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
	wc.lpszMenuName = MAKEINTRESOURCE(IDM_MENU);
	wc.lpszClassName = "ebmused_main";
	RegisterClass(&wc);

	wc.lpszMenuName = NULL;
	for (int i = 0; i < NUM_TABS; i++) {
		wc.lpfnWndProc = tab_wndproc[i];
		wc.lpszClassName = tab_class[i];
		RegisterClass(&wc);
	}

	extern LRESULT CALLBACK InstTestWndProc(HWND, UINT, WPARAM, LPARAM);
	extern LRESULT CALLBACK StateWndProc(HWND, UINT, WPARAM, LPARAM);
	extern LRESULT CALLBACK CodeListWndProc(HWND, UINT, WPARAM, LPARAM);
	extern LRESULT CALLBACK OrderWndProc(HWND, UINT, WPARAM, LPARAM);
	extern LRESULT CALLBACK TrackerWndProc(HWND, UINT, WPARAM, LPARAM);

	wc.lpfnWndProc = InstTestWndProc; wc.lpszClassName = "ebmused_insttest"; RegisterClass(&wc);
	wc.lpfnWndProc = StateWndProc;    wc.lpszClassName = "ebmused_state";    RegisterClass(&wc);

	wc.hbrBackground = NULL;
	wc.lpfnWndProc = CodeListWndProc; wc.lpszClassName = "ebmused_codelist"; RegisterClass(&wc);
	wc.lpfnWndProc = OrderWndProc;    wc.lpszClassName = "ebmused_order";    RegisterClass(&wc);

	wc.style = CS_HREDRAW;
	wc.lpfnWndProc = TrackerWndProc; wc.lpszClassName = "ebmused_tracker"; RegisterClass(&wc);

	setup_dpi_scale_values();
	InitCommonControls();
	set_up_fonts();

	hwndMain = CreateWindow("ebmused_main", "EarthBound Music Editor ~ Mahdiisdumb Version",
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, scale_x(MAIN_WINDOW_WIDTH), scale_y(MAIN_WINDOW_HEIGHT),
		NULL, NULL, hInstance, NULL);
	hwndStatus = CreateStatusWindow(WS_CHILD | WS_VISIBLE, NULL, hwndMain, IDS_STATUS);
	ShowWindow(hwndMain, nCmdShow);

	hmenu = GetMenu(hwndMain);
	CheckMenuItem(hmenu, ID_DARK_MODE, dark_mode ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuRadioItem(hmenu, ID_OCTAVE_1, ID_OCTAVE_1 + 4, ID_OCTAVE_1 + 2, MF_BYCOMMAND);
	hcontextmenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDM_CONTEXTMENU));
	HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDA_ACCEL));

	if (_ARGC > 1) open_rom(_ARGV[1], FALSE);
	tab_selected(0);

	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!TranslateAccelerator(hwndMain, hAccel, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	DestroyMenu(hcontextmenu);
	destroy_fonts();
	return msg.wParam;
}