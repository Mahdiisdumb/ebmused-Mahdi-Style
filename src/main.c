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
#include <commdlg.h>
#include <commctrl.h>
#include <mmsystem.h>
#include "ebmusv2.h"
#include "misc.h"

enum { MAIN_WINDOW_WIDTH = 720, MAIN_WINDOW_HEIGHT = 540, TAB_CONTROL_WIDTH = 600, TAB_CONTROL_HEIGHT = 25, STATUS_WINDOW_HEIGHT = 24, CODELIST_WINDOW_WIDTH = 640, CODELIST_WINDOW_HEIGHT = 480 };

struct song cur_song;
BYTE packs_loaded[3] = { 0xFF,0xFF,0xFF };
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

static const int INST_TAB = 1;
static int current_tab;
static const char* const tab_class[NUM_TABS] = { "ebmused_bgmlist","ebmused_inst","ebmused_editor","ebmused_packs" };
static const char* const tab_name[NUM_TABS] = { "BGM Table","Instruments","Tracker","Data Packs" };
LRESULT CALLBACK BGMListWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK InstrumentsWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditorWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PackListWndProc(HWND, UINT, WPARAM, LPARAM);
static const WNDPROC tab_wndproc[NUM_TABS] = { BGMListWndProc,InstrumentsWndProc,EditorWndProc,PackListWndProc };

static char filename[MAX_PATH];
static OPENFILENAME ofn;
char* open_dialog(BOOL(WINAPI* func)(LPOPENFILENAME), char* filter, char* extension, DWORD flags) { *filename = '\0'; ofn.lStructSize = sizeof ofn; ofn.hwndOwner = hwndMain; ofn.lpstrFilter = filter; ofn.lpstrDefExt = extension; ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH; ofn.Flags = flags | OFN_NOCHANGEDIR; return func(&ofn) ? filename : NULL; }
static void write_hex_array(FILE* f, const BYTE* data, int len) { fputs("[", f); for (int i = 0; i < len; i++) { if (i)fputs(",", f); fprintf(f, "0x%02X", data[i]); }fputs("]", f); }
static void write_spc_json(FILE* f) { fprintf(f, "{\n  \"music_addr\":%u,\n  \"order_length\":%d,\n  \"order\":[", cur_song.address, cur_song.order_length); for (int i = 0; i < cur_song.order_length; i++) { if (i)fputs(",", f); fprintf(f, "%d", cur_song.order[i]); }fputs("],\n  \"patterns\":{\n", f); for (int p = 0; p < cur_song.patterns; p++) { fprintf(f, "    \"%d\":[\n", p); for (int ch = 0; ch < 8; ch++) { struct track* t = &cur_song.pattern[p][ch]; fprintf(f, "      "); if (!t->track)fputs("null", f); else write_hex_array(f, t->track, t->size + 1); fprintf(f, "%s\n", ch < 7 ? "," : ""); }fprintf(f, "    ]%s\n", p < cur_song.patterns - 1 ? "," : ""); }fputs("  },\n  \"subs\":[\n", f); for (int s = 0; s < cur_song.subs; s++) { struct track* t = &cur_song.sub[s]; fprintf(f, "    "); if (!t->track)fputs("null", f); else write_hex_array(f, t->track, t->size + 1); fprintf(f, "%s\n", s < cur_song.subs - 1 ? "," : ""); }fputs("  ]\n", f); extern int inst_base; extern WORD sample_ptr_base; fprintf(f, ",\n  \"inst_base\":%d,\n  \"sample_ptr_base\":%u\n", inst_base, sample_ptr_base); fputs("}\n", f); }
static BOOL parse_hex_array(const char* s, BYTE** out, int* outlen) { const char* p = s; if (*p != '[')return FALSE; p++; BYTE* buf = NULL; int cap = 0, len = 0; while (*p && *p != ']') { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',')p++; if (strncmp(p, "0x", 2) == 0) { unsigned int val; if (sscanf(p, "0x%2X", &val) == 1) { if (len >= cap) { cap = cap ? cap * 2 : 16; buf = realloc(buf, cap); }buf[len++] = (BYTE)val; }while (*p && *p != ',' && *p != ']')p++; } else while (*p && *p != ',' && *p != ']')p++; }if (out)*out = buf; if (outlen)*outlen = len; return TRUE; }
static void import_spc_json() { char* file = open_dialog(GetOpenFileName, "SPC JSON files (*.json)\0*.json\0All Files\0*.*\0", NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY); if (!file)return; FILE* f = fopen(file, "rb"); if (!f) { MessageBox2(strerror(errno), "Import SPC JSON", MB_ICONEXCLAMATION); return; }fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); char* buf = malloc(sz + 1); fread(buf, 1, sz, f); buf[sz] = '\0'; fclose(f); cJSON* root = cJSON_Parse(buf); if (!root) { MessageBox2("Invalid JSON file.", "Import SPC JSON", MB_ICONEXCLAMATION); free(buf); return; }free_song(&cur_song); cJSON* order_json = cJSON_GetObjectItem(root, "order"); if (cJSON_IsArray(order_json)) { cur_song.order_length = cJSON_GetArraySize(order_json); cur_song.order = malloc(sizeof(int) * cur_song.order_length); for (int i = 0; i < cur_song.order_length; i++) { cJSON* v = cJSON_GetArrayItem(order_json, i); cur_song.order[i] = cJSON_IsNumber(v) ? v->valueint : 0; } }cJSON* patterns_json = cJSON_GetObjectItem(root, "patterns"); if (cJSON_IsObject(patterns_json)) { int max_pattern = -1; cJSON* p; cJSON_ArrayForEach(p, patterns_json) { int pid = atoi(p->string); if (pid > max_pattern)max_pattern = pid; }cur_song.patterns = max_pattern + 1; cur_song.pattern = malloc(sizeof(*cur_song.pattern) * cur_song.patterns); for (int pid = 0; pid < cur_song.patterns; pid++)for (int ch = 0; ch < 8; ch++)cur_song.pattern[pid][ch].track = NULL; cJSON_ArrayForEach(p, patterns_json) { int pid = atoi(p->string); cJSON* channels = p; for (int ch = 0; ch < 8; ch++) { cJSON* arr = cJSON_GetArrayItem(channels, ch); if (!arr || cJSON_IsNull(arr))continue; int len = cJSON_GetArraySize(arr); BYTE* data = malloc(len + 1); for (int i = 0; i < len; i++) { cJSON* val = cJSON_GetArrayItem(arr, i); data[i] = cJSON_IsNumber(val) ? (BYTE)val->valueint : 0; }data[len] = 0; cur_song.pattern[pid][ch].track = data; cur_song.pattern[pid][ch].size = len; } } }cJSON* subs_json = cJSON_GetObjectItem(root, "subs"); if (cJSON_IsArray(subs_json)) { cur_song.subs = cJSON_GetArraySize(subs_json); cur_song.sub = malloc(sizeof(*cur_song.sub) * cur_song.subs); for (int s = 0; s < cur_song.subs; s++) { cJSON* arr = cJSON_GetArrayItem(subs_json, s); if (!arr || cJSON_IsNull(arr)) { cur_song.sub[s].track = NULL; cur_song.sub[s].size = 0; continue; }int len = cJSON_GetArraySize(arr); BYTE* data = malloc(len + 1); for (int i = 0; i < len; i++) { cJSON* val = cJSON_GetArrayItem(arr, i); data[i] = cJSON_IsNumber(val) ? (BYTE)val->valueint : 0; }data[len] = 0; cur_song.sub[s].track = data; cur_song.sub[s].size = len; } }cJSON* inst_base_json = cJSON_GetObjectItem(root, "inst_base"); if (cJSON_IsNumber(inst_base_json))inst_base = inst_base_json->valueint; cJSON* sample_ptr_json = cJSON_GetObjectItem(root, "sample_ptr_base"); if (cJSON_IsNumber(sample_ptr_json))sample_ptr_base = (WORD)sample_ptr_json->valueint; free(buf); cJSON_Delete(root); cur_song.changed = TRUE; save_cur_song_to_pack(); SendMessage(tab_hwnd[current_tab], WM_SONG_IMPORTED, 0, 0); }
static void export_spc_json() { char* file = open_dialog(GetSaveFileName, "SPC JSON files (*.json)\0*.json\0All Files\0*.*\0", "json", OFN_OVERWRITEPROMPT); if (!file)return; FILE* f = fopen(file, "wb"); if (!f) { MessageBox2(strerror(errno), "Export SPC JSON", MB_ICONEXCLAMATION); return; }write_spc_json(f); fclose(f); }
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow) { hinstance = hInst; INITCOMMONCONTROLSEX icce = { sizeof icce,ICC_WIN95_CLASSES }; InitCommonControlsEx(&icce); WNDCLASSEX wcex = { sizeof wcex,CS_HREDRAW | CS_VREDRAW,DefWindowProc,0,0,hInst,NULL,NULL,NULL,NULL,"MainWindow",NULL }; RegisterClassEx(&wcex); hwndMain = CreateWindow("MainWindow", "EBMUS Editor", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, MAIN_WINDOW_WIDTH, MAIN_WINDOW_HEIGHT, NULL, NULL, hInst, NULL); ShowWindow(hwndMain, nCmdShow); UpdateWindow(hwndMain); MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }return msg.wParam; }
LRESULT CALLBACK BGMListWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { switch (msg) { case WM_CREATE:return 0; case WM_PAINT: { PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); }break; case WM_SONG_IMPORTED:break; }return DefWindowProc(hwnd, msg, wParam, lParam); }
LRESULT CALLBACK InstrumentsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return DefWindowProc(hwnd, msg, wParam, lParam); }
LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return DefWindowProc(hwnd, msg, wParam, lParam); }
LRESULT CALLBACK PackListWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return DefWindowProc(hwnd, msg, wParam, lParam); }
static void free_song(struct song* s) { if (!s)return; for (int p = 0; p < s->patterns; p++)for (int c = 0; c < 8; c++)if (s->pattern[p][c].track)free(s->pattern[p][c].track); if (s->pattern)free(s->pattern); for (int sidx = 0; sidx < s->subs; sidx++)if (s->sub[sidx].track)free(s->sub[sidx].track); if (s->sub)free(s->sub); if (s->order)s->order = NULL; s->patterns = 0; s->subs = 0; s->order_length = 0; }
static void save_cur_song_to_pack() {/*implementation depends on your pack format, leave empty or implement as needed*/ }