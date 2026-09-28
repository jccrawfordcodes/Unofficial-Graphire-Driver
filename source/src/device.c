/* HID access to the Graphire: find it, switch it to Wacom mode, read reports. */
#include "app.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_COLS 16

typedef struct {
    HANDLE  h;
    USHORT  pid, usagePage, usage;
    USHORT  inLen, featLen;
    char    path[300];
} hidcol;

static HANDLE g_thread, g_stop;

static void close_cols(hidcol *c, int n)
{
    for (int i = 0; i < n; i++)
        if (c[i].h && c[i].h != INVALID_HANDLE_VALUE) CloseHandle(c[i].h);
}

/* Enumerate every HID collection belonging to a Wacom device. */
static int find_cols(const config_t *cfg, hidcol *out, int max, int verbose)
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return 0;

    int n = 0;
    SP_DEVICE_INTERFACE_DATA ifd = { sizeof ifd };
    for (DWORD i = 0; n < max && SetupDiEnumDeviceInterfaces(set, NULL, &hidGuid, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailA(set, &ifd, NULL, 0, &need, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *det = malloc(need);
        if (!det) continue;
        det->cbSize = sizeof *det;
        if (!SetupDiGetDeviceInterfaceDetailA(set, &ifd, det, need, NULL, NULL)) { free(det); continue; }

        /* zero-access open always works, even for mice/keyboards */
        HANDLE q = CreateFileA(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (q == INVALID_HANDLE_VALUE) { free(det); continue; }
        HIDD_ATTRIBUTES a = { sizeof a };
        BOOL ok = HidD_GetAttributes(q, &a);
        CloseHandle(q);
        if (!ok || a.VendorID != GR_VID || (cfg->pid && a.ProductID != cfg->pid)) { free(det); continue; }

        hidcol *c = &out[n];
        memset(c, 0, sizeof *c);
        c->pid = a.ProductID;
        snprintf(c->path, sizeof c->path, "%s", det->DevicePath);
        c->h = CreateFileA(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, NULL);
        DWORD openErr = c->h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        if (c->h == INVALID_HANDLE_VALUE) {
            c->h = NULL;
            /* still grab caps through a zero-access handle, for the log */
            q = CreateFileA(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        } else q = c->h;

        PHIDP_PREPARSED_DATA pp;
        if (q != INVALID_HANDLE_VALUE && HidD_GetPreparsedData(q, &pp)) {
            HIDP_CAPS caps;
            if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                c->usagePage = caps.UsagePage; c->usage = caps.Usage;
                c->inLen = caps.InputReportByteLength; c->featLen = caps.FeatureReportByteLength;
            }
            HidD_FreePreparsedData(pp);
        }
        if (!c->h && q != INVALID_HANDLE_VALUE) CloseHandle(q);

        if (verbose)
            app_log("HID %04X:%04X usage %04X:%04X in=%u feat=%u %s %s",
                    a.VendorID, a.ProductID, c->usagePage, c->usage, c->inLen, c->featLen,
                    c->h ? "OPEN" : "no-access", c->path);
        if (verbose && !c->h)
            app_log("   (open for read/write failed, error %lu)", openErr);
        n++;
        free(det);
    }
    SetupDiDestroyDeviceInfoList(set);
    return n;
}

/* Linux wacom_set_device_mode(): feature report {2, 2} = Wacom (pen) mode. */
static int set_wacom_mode(hidcol *c)
{
    if (!c->h || c->featLen < 2) return 0;
    BYTE buf[64];
    for (int attempt = 0; attempt < 5; attempt++) {
        memset(buf, 0, sizeof buf);
        buf[0] = 2; buf[1] = 2;
        if (!HidD_SetFeature(c->h, buf, c->featLen)) {
            app_log("SetFeature(2,2) on usage %04X:%04X failed, error %lu",
                    c->usagePage, c->usage, GetLastError());
            return 0;
        }
        memset(buf, 0, sizeof buf);
        buf[0] = 2;
        if (!HidD_GetFeature(c->h, buf, c->featLen)) {
            app_log("Mode set (could not read back, error %lu) - assuming OK", GetLastError());
            return 1;
        }
        if (buf[1] == 2) { app_log("Tablet switched to Wacom mode (usage %04X:%04X)", c->usagePage, c->usage); return 1; }
        Sleep(20);
    }
    app_log("Mode switch not confirmed after 5 attempts");
    return 1;
}

static int pick_reader(hidcol *c, int n)
{
    int best = -1, score = -1;
    for (int i = 0; i < n; i++) {
        if (!c[i].h || c[i].inLen < 8) continue;
        int s = 1;
        if (c[i].usagePage == 0x0D) s = 3;                 /* digitizer */
        else if (c[i].usagePage >= 0xFF00) s = 2;          /* vendor */
        else if (c[i].usagePage == 1 && (c[i].usage == 2 || c[i].usage == 6)) s = 0;
        if (s > score) { score = s; best = i; }
    }
    return best;
}

static void log_hex(const BYTE *b, DWORD n)
{
    char s[3 * 64 + 1]; int p = 0;
    for (DWORD i = 0; i < n && i < 64; i++) p += sprintf(s + p, "%02X ", b[i]);
    s[p] = 0;
    app_log("raw: %s", s);
}

static void read_loop(HANDLE h, USHORT inLen)
{
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BYTE buf[256];
    gr_state st = {0};
    live_t live;
    live_get(&live);
    live.connected = 1;
    live_update(&live);

    if (inLen > sizeof buf) inLen = sizeof buf;
    for (;;) {
        DWORD got = 0;
        ResetEvent(ov.hEvent);
        if (!ReadFile(h, buf, inLen, &got, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) { app_log("ReadFile failed, error %lu (unplugged?)", GetLastError()); break; }
            HANDLE w[2] = { ov.hEvent, g_stop };
            if (WaitForMultipleObjects(2, w, FALSE, INFINITE) != WAIT_OBJECT_0) {
                CancelIo(h);
                GetOverlappedResult(h, &ov, &got, TRUE);
                break;
            }
            if (!GetOverlappedResult(h, &ov, &got, FALSE)) { app_log("Read failed, error %lu (unplugged?)", GetLastError()); break; }
        }

        config_t cfg;
        cfg_get(&cfg);
        if (cfg.logRaw) log_hex(buf, got);

        gr_event e;
        if (!gr_parse(&st, buf, got, &e)) continue;
        live.reports++;
        live.last = e;
        if (e.prox && e.tool != GR_TOOL_MOUSE && e.pressure > live.maxPressureSeen) live.maxPressureSeen = e.pressure;

        if (app_paused()) output_release_all();
        else output_event(&cfg, &e, &live);
        live_update(&live);
    }
    output_release_all();
    CloseHandle(ov.hEvent);
    live.connected = 0;
    live_update(&live);
}

static DWORD WINAPI device_thread(LPVOID arg)
{
    (void)arg;
    int logged = 0;
    config_t cfg;
    cfg_get(&cfg);
    if (!output_init(&cfg))
        app_set_status(L"Pen injection unavailable - see log");

    while (WaitForSingleObject(g_stop, 0) == WAIT_TIMEOUT) {
        hidcol cols[MAX_COLS];
        cfg_get(&cfg);
        int n = find_cols(&cfg, cols, MAX_COLS, !logged);
        int r = pick_reader(cols, n);
        if (r < 0) {
            if (!logged) {
                app_log(n ? "Wacom device found but no readable collection (another driver may own it)"
                          : "No Wacom HID device found; waiting for tablet...");
            }
            logged = 1;
            app_set_status(n ? L"Tablet busy (another driver?)" : L"Tablet not found");
            close_cols(cols, n);
            WaitForSingleObject(g_stop, 2000);
            continue;
        }
        logged = 0;

        int switched = set_wacom_mode(&cols[r]);
        for (int i = 0; !switched && i < n; i++)
            if (i != r) switched = set_wacom_mode(&cols[i]);
        if (!switched) app_log("Warning: could not switch to Wacom mode; reading anyway");

        live_t l; live_get(&l); l.modeSwitched = switched; live_update(&l);
        app_log("Reading from usage %04X:%04X (%u-byte reports)", cols[r].usagePage, cols[r].usage, cols[r].inLen);
        app_set_status(L"Connected");
        read_loop(cols[r].h, cols[r].inLen);
        close_cols(cols, n);
        app_set_status(L"Tablet disconnected");
        logged = 0;
        WaitForSingleObject(g_stop, 1000);
    }
    output_shutdown();
    return 0;
}

void device_start(void)
{
    g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_thread = CreateThread(NULL, 0, device_thread, NULL, 0, NULL);
    SetThreadPriority(g_thread, THREAD_PRIORITY_HIGHEST);
}

void device_stop(void)
{
    if (!g_thread) return;
    SetEvent(g_stop);
    WaitForSingleObject(g_thread, 3000);
    CloseHandle(g_thread);
    g_thread = NULL;
}
