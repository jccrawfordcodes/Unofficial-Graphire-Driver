/* Graphire Driver - tray app, settings, logging. */
#include "app.h"
#include <shellapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include "resource.h"

#define WM_TRAY      (WM_APP + 1)
#define TIMER_POLL   1
#define TIMER_STATUS 2

enum { ID_STATUS = 100, ID_ABS, ID_REL, ID_PAUSE, ID_SHOWSTATUS, ID_EDIT, ID_RELOAD,
       ID_LOG, ID_RAW, ID_AUTOSTART, ID_EXIT };

static CRITICAL_SECTION g_cs;
static config_t g_cfg;
static live_t   g_live;
static volatile LONG g_paused;
static wchar_t  g_status[128] = L"Starting";
static wchar_t  g_dir[MAX_PATH], g_ini[MAX_PATH], g_logPath[MAX_PATH], g_exe[MAX_PATH];
static FILETIME g_iniTime;
static RECT     g_virt, g_target;
static HWND     g_hwnd, g_statusWnd;
static HINSTANCE g_inst;
static NOTIFYICONDATAW g_nid;
static UINT     g_taskbarCreated;
static FILE    *g_log;

/* ---------------- logging ---------------- */

void app_log(const char *fmt, ...)
{
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    EnterCriticalSection(&g_cs);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_cs);
}

/* ---------------- shared state ---------------- */

void cfg_get(config_t *o)        { EnterCriticalSection(&g_cs); *o = g_cfg; LeaveCriticalSection(&g_cs); }
void live_update(const live_t *l){ EnterCriticalSection(&g_cs); g_live = *l; LeaveCriticalSection(&g_cs); }
void live_get(live_t *o)         { EnterCriticalSection(&g_cs); *o = g_live; LeaveCriticalSection(&g_cs); }
int  app_paused(void)            { return g_paused != 0; }
void screen_virtual(RECT *r)     { EnterCriticalSection(&g_cs); *r = g_virt; LeaveCriticalSection(&g_cs); }
void screen_target(const config_t *c, RECT *r) { (void)c; EnterCriticalSection(&g_cs); *r = g_target; LeaveCriticalSection(&g_cs); }

void app_set_status(const wchar_t *s)
{
    EnterCriticalSection(&g_cs);
    wcsncpy(g_status, s, 127); g_status[127] = 0;
    LeaveCriticalSection(&g_cs);
    if (g_hwnd) PostMessageW(g_hwnd, WM_TIMER, TIMER_STATUS, 0);
}

/* ---------------- monitors ---------------- */

typedef struct { int want, idx; RECT r; int found; } monfind;

static BOOL CALLBACK mon_cb(HMONITOR m, HDC dc, LPRECT rc, LPARAM lp)
{
    (void)m; (void)dc;
    monfind *f = (monfind *)lp;
    f->idx++;
    app_log("Monitor %d: %ld,%ld - %ld,%ld", f->idx, rc->left, rc->top, rc->right, rc->bottom);
    if (f->idx == f->want) { f->r = *rc; f->found = 1; }
    return TRUE;
}

static void update_screens(void)
{
    RECT v;
    v.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    v.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    v.right = v.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    v.bottom = v.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    config_t c; cfg_get(&c);
    monfind f = { c.monitor, 0, v, 0 };
    EnumDisplayMonitors(NULL, NULL, mon_cb, (LPARAM)&f);
    if (c.monitor > 0 && !f.found) app_log("Monitor=%d not present, using all monitors", c.monitor);
    EnterCriticalSection(&g_cs);
    g_virt = v;
    g_target = f.found ? f.r : v;
    LeaveCriticalSection(&g_cs);
    app_log("Pen mapped to %ld,%ld - %ld,%ld", f.found ? f.r.left : v.left, f.found ? f.r.top : v.top,
            f.found ? f.r.right : v.right, f.found ? f.r.bottom : v.bottom);
}

/* ---------------- config ---------------- */

static const char DEFAULT_INI[] =
"; Graphire Driver settings. Saved changes are applied automatically.\r\n"
"\r\n"
"[Mapping]\r\n"
"; absolute = pen position maps to the screen (normal tablet use)\r\n"
"; relative = pen moves the cursor like a mouse\r\n"
"Mode=absolute\r\n"
"; Active tablet area in tablet units (full area: 0,0 - 10206,7422)\r\n"
"AreaLeft=0\r\n"
"AreaTop=0\r\n"
"AreaRight=10206\r\n"
"AreaBottom=7422\r\n"
"; 0 = span all monitors, 1 = first monitor, 2 = second, ... (see log for the list)\r\n"
"Monitor=0\r\n"
"; 1 = trim the tablet area so circles stay circles\r\n"
"KeepAspect=1\r\n"
"\r\n"
"[Relative]\r\n"
"; Speed: 1.0 = one tablet width moves one screen width\r\n"
"Sensitivity=1.0\r\n"
"; Acceleration: 0 = none; 0.5 = moderate; 1.5 = strong\r\n"
"Acceleration=0.5\r\n"
"MaxGain=4.0\r\n"
"\r\n"
"[Pressure]\r\n"
"; Raw pressure range (Graphire reports 0..511). Lower Max = full pressure with a lighter touch.\r\n"
"Min=0\r\n"
"Max=511\r\n"
"; Curve: 1.0 = linear, <1 = softer (easier to press), >1 = firmer\r\n"
"Curve=1.0\r\n"
"; 0 = use the tablet's own tip switch; otherwise raw pressure needed to count as touching\r\n"
"ContactThreshold=0\r\n"
"\r\n"
"[Buttons]\r\n"
"; Pen side switches: barrel, left, right, middle, double, none\r\n"
"; barrel = Windows Ink barrel button (press + tap = right-click in Ink apps)\r\n"
"LowerButton=right\r\n"
"UpperButton=middle\r\n"
"; 1 = eraser end erases in Windows Ink apps\r\n"
"Eraser=1\r\n"
"\r\n"
"[Smoothing]\r\n"
"; 0 = off, 0.3 = light, 0.7 = heavy (adds lag)\r\n"
"Amount=0\r\n"
"\r\n"
"[Mouse]\r\n"
"; Graphire puck. relative = moves like a mouse, absolute = maps like the pen\r\n"
"Mode=relative\r\n"
"Sensitivity=1.0\r\n"
"Acceleration=0.8\r\n"
"MaxGain=4.0\r\n"
"InvertWheel=0\r\n"
"\r\n"
"[Device]\r\n"
"; USB product id to use, 0 = any Wacom tablet (ET-0405A-U / Graphire2 is 17 = 0x0011)\r\n"
"ProductId=0\r\n"
"; Windows pen feedback: 1 = default, 2 = indirect, 3 = none (hides tap ripples)\r\n"
"Feedback=1\r\n"
"\r\n"
"[Debug]\r\n"
"; 1 = write every raw tablet report to the log\r\n"
"LogRaw=0\r\n";

static int ini_int(const wchar_t *sec, const wchar_t *key, int def)
{
    return (int)GetPrivateProfileIntW(sec, key, def, g_ini);
}

static double ini_dbl(const wchar_t *sec, const wchar_t *key, double def)
{
    wchar_t b[64];
    GetPrivateProfileStringW(sec, key, L"", b, 64, g_ini);
    if (!b[0]) return def;
    wchar_t *end; double v = wcstod(b, &end);
    return end == b ? def : v;
}

static void ini_str(const wchar_t *sec, const wchar_t *key, const wchar_t *def, wchar_t *out, int n)
{
    GetPrivateProfileStringW(sec, key, def, out, n, g_ini);
    for (wchar_t *p = out; *p; p++) *p = towlower(*p);
}

static btn_action parse_btn(const wchar_t *s)
{
    if (!wcscmp(s, L"barrel")) return ACT_BARREL;
    if (!wcscmp(s, L"left"))   return ACT_LEFT;
    if (!wcscmp(s, L"right"))  return ACT_RIGHT;
    if (!wcscmp(s, L"middle")) return ACT_MIDDLE;
    if (!wcscmp(s, L"double")) return ACT_DOUBLE;
    return ACT_NONE;
}

static void ensure_ini(void)
{
    if (GetFileAttributesW(g_ini) != INVALID_FILE_ATTRIBUTES) return;
    HANDLE h = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(h, DEFAULT_INI, sizeof DEFAULT_INI - 1, &w, NULL);
    CloseHandle(h);
}

static int ini_mtime(FILETIME *ft)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(g_ini, GetFileExInfoStandard, &a)) return 0;
    *ft = a.ftLastWriteTime;
    return 1;
}

static void load_config(void)
{
    ensure_ini();
    ini_mtime(&g_iniTime);
    config_t c; wchar_t s[32];
    ini_str(L"Mapping", L"Mode", L"absolute", s, 32); c.relative = !wcscmp(s, L"relative");
    c.areaL = ini_int(L"Mapping", L"AreaLeft", 0);
    c.areaT = ini_int(L"Mapping", L"AreaTop", 0);
    c.areaR = ini_int(L"Mapping", L"AreaRight", GR_MAX_X);
    c.areaB = ini_int(L"Mapping", L"AreaBottom", GR_MAX_Y);
    c.monitor = ini_int(L"Mapping", L"Monitor", 0);
    c.keepAspect = ini_int(L"Mapping", L"KeepAspect", 1);
    c.relSens = ini_dbl(L"Relative", L"Sensitivity", 1.0);
    c.relAccel = ini_dbl(L"Relative", L"Acceleration", 0.5);
    c.relMaxGain = ini_dbl(L"Relative", L"MaxGain", 4.0);
    c.pMin = ini_int(L"Pressure", L"Min", 0);
    c.pMax = ini_int(L"Pressure", L"Max", GR_MAX_PRESSURE);
    c.pCurve = ini_dbl(L"Pressure", L"Curve", 1.0);
    c.contactThreshold = ini_int(L"Pressure", L"ContactThreshold", 0);
    ini_str(L"Buttons", L"LowerButton", L"right", s, 32); c.lower = parse_btn(s);
    ini_str(L"Buttons", L"UpperButton", L"middle", s, 32); c.upper = parse_btn(s);
    c.eraser = ini_int(L"Buttons", L"Eraser", 1);
    c.smoothing = ini_dbl(L"Smoothing", L"Amount", 0);
    ini_str(L"Mouse", L"Mode", L"relative", s, 32); c.mouseRelative = wcscmp(s, L"absolute") != 0;
    c.mouseSens = ini_dbl(L"Mouse", L"Sensitivity", 1.0);
    c.mouseAccel = ini_dbl(L"Mouse", L"Acceleration", 0.8);
    c.mouseMaxGain = ini_dbl(L"Mouse", L"MaxGain", 4.0);
    c.invertWheel = ini_int(L"Mouse", L"InvertWheel", 0);
    c.pid = ini_int(L"Device", L"ProductId", 0);
    c.feedback = ini_int(L"Device", L"Feedback", 1);
    c.logRaw = ini_int(L"Debug", L"LogRaw", 0);

    EnterCriticalSection(&g_cs); g_cfg = c; LeaveCriticalSection(&g_cs);
    app_log("Settings loaded: mode=%s area=%d,%d-%d,%d monitor=%d pressure=%d..%d curve=%.2f",
            c.relative ? "relative" : "absolute", c.areaL, c.areaT, c.areaR, c.areaB,
            c.monitor, c.pMin, c.pMax, c.pCurve);
    update_screens();
}

/* ---------------- autostart ---------------- */

static const wchar_t RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static int autostart_get(void)
{
    HKEY k; int on = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        on = RegQueryValueExW(k, L"GraphireDriver", NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return on;
}

static void autostart_set(int on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t v[MAX_PATH + 4];
        swprintf(v, MAX_PATH + 4, L"\"%ls\"", g_exe);
        RegSetValueExW(k, L"GraphireDriver", 0, REG_SZ, (BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
    } else RegDeleteValueW(k, L"GraphireDriver");
    RegCloseKey(k);
}

/* ---------------- status window ---------------- */

static LRESULT CALLBACK status_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE: SetTimer(h, 1, 50, NULL); return 0;
    case WM_TIMER:  InvalidateRect(h, NULL, TRUE); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        live_t L; live_get(&L);
        config_t c; cfg_get(&c);
        wchar_t st[128];
        EnterCriticalSection(&g_cs); wcscpy(st, g_status); LeaveCriticalSection(&g_cs);
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        wchar_t b[256]; int y = 10;
#define LINE(...) do { swprintf(b, 256, __VA_ARGS__); TextOutW(dc, 12, y, b, (int)wcslen(b)); y += 20; } while (0)
        LINE(L"Status: %ls%ls", st, g_paused ? L" (paused)" : L"");
        LINE(L"Wacom mode: %ls    Reports: %lu", L.modeSwitched ? L"yes" : L"not confirmed", L.reports);
        LINE(L"Tool: %hs %ls", gr_tool_name(L.last.tool), L.last.prox ? L"(in range)" : L"(out of range)");
        LINE(L"Tablet X/Y: %d, %d", L.last.x, L.last.y);
        LINE(L"Screen X/Y: %d, %d", L.screenX, L.screenY);
        if (L.last.tool == GR_TOOL_MOUSE)
            LINE(L"Buttons: L=%d M=%d R=%d  wheel=%d", L.last.left, L.last.middle, L.last.right, L.last.wheel);
        else
            LINE(L"Tip=%d  Lower=%d  Upper=%d", L.last.tip, L.last.btn_lower, L.last.btn_upper);
        LINE(L"Pressure raw: %d   (max seen %d, setting Max=%d)", L.last.pressure, L.maxPressureSeen, c.pMax);
        /* pressure bar */
        RECT bar = { 12, y + 4, 12 + 360, y + 24 };
        FrameRect(dc, &bar, (HBRUSH)GetStockObject(GRAY_BRUSH));
        RECT fill = bar; InflateRect(&fill, -2, -2);
        fill.right = fill.left + (fill.right - fill.left) * L.outPressure / 1024;
        HBRUSH br = CreateSolidBrush(RGB(40, 120, 220));
        FillRect(dc, &fill, br); DeleteObject(br);
        y += 34;
        LINE(L"Output pressure: %d / 1024", L.outPressure);
#undef LINE
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY: g_statusWnd = NULL; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void show_status(void)
{
    if (g_statusWnd) { SetForegroundWindow(g_statusWnd); return; }
    UINT dpi = GetDpiForSystem();
    int W = MulDiv(420, dpi, 96), H = MulDiv(270, dpi, 96);
    g_statusWnd = CreateWindowExW(WS_EX_TOPMOST, L"GraphireStatus", APP_NAME L" - Status",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, W, H, NULL, NULL, g_inst, NULL);
    ShowWindow(g_statusWnd, SW_SHOW);
}

/* ---------------- tray ---------------- */

static void tray_update_tip(void)
{
    EnterCriticalSection(&g_cs);
    swprintf(g_nid.szTip, 128, L"%ls: %ls%ls", APP_NAME, g_status, g_paused ? L" (paused)" : L"");
    LeaveCriticalSection(&g_cs);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void tray_add(void)
{
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(IDI_APP));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wcscpy(g_nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void set_mode(int relative)
{
    WritePrivateProfileStringW(L"Mapping", L"Mode", relative ? L"relative" : L"absolute", g_ini);
    load_config();
}

static void open_file(const wchar_t *path)
{
    ShellExecuteW(NULL, L"open", L"notepad.exe", path, NULL, SW_SHOWNORMAL);
}

static void tray_menu(void)
{
    config_t c; cfg_get(&c);
    wchar_t st[160];
    EnterCriticalSection(&g_cs); swprintf(st, 160, L"Status: %ls", g_status); LeaveCriticalSection(&g_cs);

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_GRAYED, ID_STATUS, st);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (c.relative ? 0 : MF_CHECKED), ID_ABS, L"Absolute (pen) mode");
    AppendMenuW(m, MF_STRING | (c.relative ? MF_CHECKED : 0), ID_REL, L"Relative (mouse) mode");
    AppendMenuW(m, MF_STRING | (g_paused ? MF_CHECKED : 0), ID_PAUSE, L"Pause");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_SHOWSTATUS, L"Status && pressure test...");
    AppendMenuW(m, MF_STRING, ID_EDIT, L"Edit settings...");
    AppendMenuW(m, MF_STRING, ID_RELOAD, L"Reload settings");
    AppendMenuW(m, MF_STRING, ID_LOG, L"Open log");
    AppendMenuW(m, MF_STRING | (c.logRaw ? MF_CHECKED : 0), ID_RAW, L"Log raw reports");
    AppendMenuW(m, MF_STRING | (autostart_get() ? MF_CHECKED : 0), ID_AUTOSTART, L"Start with Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit");

    POINT p; GetCursorPos(&p);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, g_hwnd, NULL);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == g_taskbarCreated && g_taskbarCreated) { tray_add(); return 0; }
    switch (msg) {
    case WM_TRAY:
        if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU) tray_menu();
        else if (LOWORD(l) == WM_LBUTTONDBLCLK) show_status();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_ABS: set_mode(0); break;
        case ID_REL: set_mode(1); break;
        case ID_PAUSE: InterlockedExchange(&g_paused, !g_paused); tray_update_tip(); break;
        case ID_SHOWSTATUS: show_status(); break;
        case ID_EDIT: open_file(g_ini); break;
        case ID_RELOAD: load_config(); break;
        case ID_LOG: open_file(g_logPath); break;
        case ID_RAW: {
            config_t c; cfg_get(&c);
            WritePrivateProfileStringW(L"Debug", L"LogRaw", c.logRaw ? L"0" : L"1", g_ini);
            load_config();
            break;
        }
        case ID_AUTOSTART: autostart_set(!autostart_get()); break;
        case ID_EXIT: DestroyWindow(h); break;
        }
        return 0;
    case WM_TIMER:
        if (w == TIMER_POLL) {
            FILETIME ft;
            if (ini_mtime(&ft) && CompareFileTime(&ft, &g_iniTime) != 0) {
                Sleep(100);  /* let the editor finish writing */
                load_config();
            }
        }
        tray_update_tip();
        return 0;
    case WM_DISPLAYCHANGE:
        update_screens();
        return 0;
    case WM_DESTROY:
        device_stop();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev; (void)cmd; (void)show;
    g_inst = inst;
    InitializeCriticalSection(&g_cs);

    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Local\\GraphireDriverSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"Graphire Driver is already running (see the tray icon).", APP_NAME, MB_ICONINFORMATION);
        return 0;
    }

    GetModuleFileNameW(NULL, g_exe, MAX_PATH);
    wcscpy(g_dir, g_exe);
    wchar_t *slash = wcsrchr(g_dir, L'\\');
    if (slash) *slash = 0;
    swprintf(g_ini, MAX_PATH, L"%ls\\graphire.ini", g_dir);
    swprintf(g_logPath, MAX_PATH, L"%ls\\graphire.log", g_dir);
    g_log = _wfopen(g_logPath, L"w");
    if (!g_log) {  /* exe folder not writable (e.g. Program Files): use %LOCALAPPDATA% */
        wchar_t base[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH)) {
            swprintf(g_dir, MAX_PATH, L"%ls\\GraphireDriver", base);
            CreateDirectoryW(g_dir, NULL);
            swprintf(g_ini, MAX_PATH, L"%ls\\graphire.ini", g_dir);
            swprintf(g_logPath, MAX_PATH, L"%ls\\graphire.log", g_dir);
            g_log = _wfopen(g_logPath, L"w");
        }
    }
    app_log("Graphire Driver %s starting", APP_VERSION);

    load_config();

    WNDCLASSEXW wc = { sizeof wc };
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"GraphireDriverWnd";
    RegisterClassExW(&wc);
    WNDCLASSEXW sc = { sizeof sc };
    sc.lpfnWndProc = status_proc;
    sc.hInstance = inst;
    sc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    sc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    sc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    sc.lpszClassName = L"GraphireStatus";
    RegisterClassExW(&sc);

    /* a hidden top-level window (not message-only) so it receives WM_DISPLAYCHANGE and TaskbarCreated */
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, APP_NAME, WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    tray_add();
    SetTimer(g_hwnd, TIMER_POLL, 1000, NULL);

    device_start();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    app_log("Exiting");
    if (g_log) fclose(g_log);
    ReleaseMutex(mutex);
    return 0;
}
