// main.c - MSVC-ready

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include "ebmusv2.h"
#include "misc.h"
#include "id.h"

enum {
    MAIN_WINDOW_WIDTH = 720,
    MAIN_WINDOW_HEIGHT = 540,
    TAB_CONTROL_WIDTH = 600,
    TAB_CONTROL_HEIGHT = 25,
    STATUS_WINDOW_HEIGHT = 24,
    CODELIST_WINDOW_WIDTH = 640,
    CODELIST_WINDOW_HEIGHT = 480
};
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

// Forward declarations
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK BGMListWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK InstrumentsWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditorWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PackListWndProc(HWND, UINT, WPARAM, LPARAM);

static const WNDPROC tab_wndproc[NUM_TABS] = {
    BGMListWndProc,
    InstrumentsWndProc,
    EditorWndProc,
    PackListWndProc
};

// Helper functions
static char filename[MAX_PATH];
static OPENFILENAME ofn;

char* open_dialog(BOOL(WINAPI* func)(LPOPENFILENAME),
    char* filter, char* extension, DWORD flags)
{
    *filename = '\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndMain;
    ofn.lpstrFilter = filter;
    ofn.lpstrDefExt = extension;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = flags | OFN_NOCHANGEDIR;
    return func(&ofn) ? filename : NULL;
}

// Subsystem: Windows GUI requires WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    hinstance = hInstance;
    WNDCLASS wc = { 0 };
    MSG msg;

    // Main window class
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCE(IDM_MENU);
    wc.lpszClassName = "ebmused_main";
    RegisterClass(&wc);

    // Tab classes
    wc.lpszMenuName = NULL;
    for (int i = 0; i < NUM_TABS; i++) {
        wc.lpfnWndProc = tab_wndproc[i];
        wc.lpszClassName = tab_class[i];
        RegisterClass(&wc);
    }

    // Other auxiliary classes
    extern LRESULT CALLBACK InstTestWndProc(HWND, UINT, WPARAM, LPARAM);
    extern LRESULT CALLBACK StateWndProc(HWND, UINT, WPARAM, LPARAM);
    extern LRESULT CALLBACK CodeListWndProc(HWND, UINT, WPARAM, LPARAM);
    extern LRESULT CALLBACK OrderWndProc(HWND, UINT, WPARAM, LPARAM);
    extern LRESULT CALLBACK TrackerWndProc(HWND, UINT, WPARAM, LPARAM);

    wc.lpfnWndProc = InstTestWndProc; wc.lpszClassName = "ebmused_insttest"; RegisterClass(&wc);
    wc.lpfnWndProc = StateWndProc;    wc.lpszClassName = "ebmused_state";   RegisterClass(&wc);
    wc.lpfnWndProc = CodeListWndProc; wc.lpszClassName = "ebmused_codelist"; RegisterClass(&wc);
    wc.lpfnWndProc = OrderWndProc;    wc.lpszClassName = "ebmused_order";   RegisterClass(&wc);
    wc.lpfnWndProc = TrackerWndProc;  wc.lpszClassName = "ebmused_tracker"; RegisterClass(&wc);

    setup_dpi_scale_values();
    InitCommonControls();
    set_up_fonts();

    // Create main window
    hwndMain = CreateWindow(
        "ebmused_main",
        "EarthBound Music Editor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        scale_x(MAIN_WINDOW_WIDTH), scale_y(MAIN_WINDOW_HEIGHT),
        NULL, NULL, hInstance, NULL
    );

    hwndStatus = CreateStatusWindow(WS_CHILD | WS_VISIBLE, NULL, hwndMain, IDS_STATUS);
    ShowWindow(hwndMain, nCmdShow);
    UpdateWindow(hwndMain);

    hmenu = GetMenu(hwndMain);
    CheckMenuRadioItem(hmenu, ID_OCTAVE_1, ID_OCTAVE_1 + 4, ID_OCTAVE_1 + 2, MF_BYCOMMAND);
    hcontextmenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDM_CONTEXTMENU));
    HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDA_ACCEL));

    if (__argc > 1) open_rom(__argv[1], FALSE);

    tab_selected(0);

    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(hwndMain, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    DestroyMenu(hcontextmenu);
    destroy_fonts();
    return (int)msg.wParam;
}