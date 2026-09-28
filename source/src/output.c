/* Turns tablet events into Windows input:
 *  pen/eraser -> synthetic pen (Windows Ink: pressure, eraser, barrel)
 *  puck       -> mouse via SendInput */
#include "app.h"
#include <math.h>

static HSYNTHETICPOINTERDEVICE g_pen;

/* resolved at runtime (MinGW's import lib predates these user32 exports) */
typedef HSYNTHETICPOINTERDEVICE (WINAPI *pfnCreate)(POINTER_INPUT_TYPE, ULONG, POINTER_FEEDBACK_MODE);
typedef BOOL (WINAPI *pfnInject)(HSYNTHETICPOINTERDEVICE, const POINTER_TYPE_INFO *, UINT32);
typedef VOID (WINAPI *pfnDestroy)(HSYNTHETICPOINTERDEVICE);
static pfnCreate  pCreate;
static pfnInject  pInject;
static pfnDestroy pDestroy;
static DWORD g_lastErrLog;

/* ---------- helpers ---------- */

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

static double gain(double speed, double accel, double maxGain)
{
    /* speed in tablet units per report; ~50 is a brisk stroke */
    double g = 1.0 + accel * speed / 50.0;
    return g > maxGain ? maxGain : g;
}

static void mouse_send(DWORD flags, LONG data)
{
    INPUT in = {0};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    in.mi.mouseData = (DWORD)data;
    SendInput(1, &in, sizeof in);
}

static void mouse_move_to(double x, double y)
{
    RECT v; screen_virtual(&v);
    double w = v.right - v.left - 1, h = v.bottom - v.top - 1;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    INPUT in = {0};
    in.type = INPUT_MOUSE;
    in.mi.dx = (LONG)lround((x - v.left) * 65535.0 / w);
    in.mi.dy = (LONG)lround((y - v.top) * 65535.0 / h);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof in);
}

static void button_action(btn_action a, int down)
{
    switch (a) {
    case ACT_LEFT:   mouse_send(down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP, 0); break;
    case ACT_RIGHT:  mouse_send(down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP, 0); break;
    case ACT_MIDDLE: mouse_send(down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP, 0); break;
    case ACT_DOUBLE:
        if (down) {
            mouse_send(MOUSEEVENTF_LEFTDOWN, 0); mouse_send(MOUSEEVENTF_LEFTUP, 0);
            mouse_send(MOUSEEVENTF_LEFTDOWN, 0); mouse_send(MOUSEEVENTF_LEFTUP, 0);
        }
        break;
    default: break;
    }
}

/* Map tablet coordinates to a screen point (absolute mode). */
static void map_absolute(const config_t *c, int tx, int ty, double *sx, double *sy)
{
    RECT r; screen_target(c, &r);
    double L = c->areaL, T = c->areaT, W = c->areaR - c->areaL, H = c->areaB - c->areaT;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    double sw = r.right - r.left, sh = r.bottom - r.top;
    if (c->keepAspect && sw > 0 && sh > 0) {
        /* shrink the tablet area (anchored top-left) to match the screen's shape */
        double ta = W / H, sa = sw / sh;
        if (ta > sa) W = H * sa; else H = W / sa;
    }
    double nx = clampd((tx - L) / W, 0, 1), ny = clampd((ty - T) / H, 0, 1);
    *sx = r.left + nx * (sw - 1);
    *sy = r.top + ny * (sh - 1);
}

/* ---------- pen ---------- */

static struct {
    int inRange, inContact;
    int haveLast; int lastX, lastY;
    double curX, curY;          /* position before smoothing */
    double smX, smY; int haveSm;
    int lowerDown, upperDown;
    POINT pt;
} P;

static void pen_inject(UINT32 flags, POINTER_BUTTON_CHANGE_TYPE chg, POINT pt, UINT32 pressure, UINT32 penFlags)
{
    if (!g_pen) return;
    POINTER_TYPE_INFO ti;
    ZeroMemory(&ti, sizeof ti);
    ti.type = PT_PEN;
    POINTER_PEN_INFO *pi = &ti.penInfo;
    pi->pointerInfo.pointerType = PT_PEN;
    pi->pointerInfo.pointerId = 0;
    pi->pointerInfo.pointerFlags = flags;
    pi->pointerInfo.ptPixelLocation = pt;
    pi->pointerInfo.ButtonChangeType = chg;
    pi->penFlags = penFlags;
    pi->penMask = PEN_MASK_PRESSURE;
    pi->pressure = pressure;
    if (!pInject(g_pen, &ti, 1)) {
        DWORD now = GetTickCount();
        if (now - g_lastErrLog > 2000) {
            app_log("InjectSyntheticPointerInput failed, error %lu (flags %08X)", GetLastError(), flags);
            g_lastErrLog = now;
        }
    }
}

static void pen_leave(void)
{
    if (P.inContact)
        pen_inject(POINTER_FLAG_INRANGE | POINTER_FLAG_UP, POINTER_CHANGE_FIRSTBUTTON_UP, P.pt, 0, 0);
    if (P.inRange || P.inContact)
        pen_inject(POINTER_FLAG_UPDATE, POINTER_CHANGE_NONE, P.pt, 0, 0);
    P.inRange = P.inContact = 0;
    P.haveLast = P.haveSm = 0;
}

static void pen_buttons_release(const config_t *c)
{
    if (P.lowerDown && c->lower != ACT_BARREL) button_action(c->lower, 0);
    if (P.upperDown && c->upper != ACT_BARREL) button_action(c->upper, 0);
    P.lowerDown = P.upperDown = 0;
}

static UINT32 map_pressure(const config_t *c, int raw)
{
    double lo = c->pMin, hi = c->pMax > c->pMin ? c->pMax : c->pMin + 1;
    double p = clampd((raw - lo) / (hi - lo), 0, 1);
    if (c->pCurve > 0 && c->pCurve != 1.0) p = pow(p, c->pCurve);
    UINT32 v = (UINT32)lround(p * 1024.0);
    return v > 1024 ? 1024 : v;
}

static void pen_event(const config_t *c, const gr_event *e, live_t *l)
{
    if (!e->prox) { pen_buttons_release(c); pen_leave(); return; }

    /* position */
    if (c->relative) {
        if (!P.inRange || !P.haveLast) {
            POINT cp; GetCursorPos(&cp);
            if (!P.inRange) { P.curX = cp.x; P.curY = cp.y; }
        } else {
            RECT r; screen_target(c, &r);
            double base = (double)(r.right - r.left) / GR_MAX_X * c->relSens;
            double dx = e->x - P.lastX, dy = e->y - P.lastY;
            double g = gain(sqrt(dx * dx + dy * dy), c->relAccel, c->relMaxGain);
            RECT v; screen_virtual(&v);
            P.curX = clampd(P.curX + dx * base * g, v.left, v.right - 1);
            P.curY = clampd(P.curY + dy * base * g, v.top, v.bottom - 1);
        }
        P.lastX = e->x; P.lastY = e->y; P.haveLast = 1;
    } else {
        map_absolute(c, e->x, e->y, &P.curX, &P.curY);
    }

    if (!P.haveSm || c->smoothing <= 0) { P.smX = P.curX; P.smY = P.curY; P.haveSm = 1; }
    else {
        double a = 1.0 - clampd(c->smoothing, 0, 0.95);
        P.smX += a * (P.curX - P.smX);
        P.smY += a * (P.curY - P.smY);
    }
    POINT pt = { (LONG)lround(P.smX), (LONG)lround(P.smY) };

    /* pressure / contact */
    UINT32 pr = map_pressure(c, e->pressure);
    int contact = c->contactThreshold > 0 ? e->pressure >= c->contactThreshold : e->tip;
    if (contact && pr == 0) pr = 1;
    if (!contact) pr = 0;

    int eraser = e->tool == GR_TOOL_ERASER && c->eraser;
    UINT32 pf = 0;
    if (eraser) pf |= PEN_FLAG_INVERTED | (contact ? PEN_FLAG_ERASER : 0);
    if ((c->lower == ACT_BARREL && e->btn_lower) || (c->upper == ACT_BARREL && e->btn_upper))
        pf |= PEN_FLAG_BARREL;

    /* first contact of a stroke must be preceded by a hover frame */
    if (!P.inRange && contact)
        pen_inject(POINTER_FLAG_INRANGE | POINTER_FLAG_UPDATE, POINTER_CHANGE_NONE, pt, 0, pf & ~PEN_FLAG_ERASER);

    UINT32 flags = POINTER_FLAG_INRANGE;
    POINTER_BUTTON_CHANGE_TYPE chg = POINTER_CHANGE_NONE;
    if (contact) {
        flags |= POINTER_FLAG_INCONTACT | POINTER_FLAG_FIRSTBUTTON;
        if (!P.inContact) { flags |= POINTER_FLAG_DOWN; chg = POINTER_CHANGE_FIRSTBUTTON_DOWN; }
        else flags |= POINTER_FLAG_UPDATE;
    } else {
        if (P.inContact) { flags |= POINTER_FLAG_UP; chg = POINTER_CHANGE_FIRSTBUTTON_UP; }
        else flags |= POINTER_FLAG_UPDATE;
    }
    P.pt = pt;
    pen_inject(flags, chg, pt, pr, pf);
    P.inRange = 1;
    P.inContact = contact;

    /* side switches mapped to mouse actions */
    if (c->lower != ACT_BARREL && e->btn_lower != P.lowerDown) button_action(c->lower, e->btn_lower);
    if (c->upper != ACT_BARREL && e->btn_upper != P.upperDown) button_action(c->upper, e->btn_upper);
    P.lowerDown = e->btn_lower; P.upperDown = e->btn_upper;

    l->screenX = pt.x; l->screenY = pt.y; l->outPressure = (int)pr;
}

/* ---------- puck ---------- */

static struct {
    int inRange, haveLast, lastX, lastY;
    double x, y;
    int left, right, middle;
} M;

static void mouse_release(void)
{
    if (M.left)   mouse_send(MOUSEEVENTF_LEFTUP, 0);
    if (M.right)  mouse_send(MOUSEEVENTF_RIGHTUP, 0);
    if (M.middle) mouse_send(MOUSEEVENTF_MIDDLEUP, 0);
    M.left = M.right = M.middle = 0;
    M.inRange = M.haveLast = 0;
}

static void mouse_event_(const config_t *c, const gr_event *e, live_t *l)
{
    if (!e->prox) { mouse_release(); return; }

    if (c->mouseRelative) {
        if (!M.haveLast) {
            POINT cp; GetCursorPos(&cp); M.x = cp.x; M.y = cp.y;
        } else {
            RECT v; screen_virtual(&v);
            RECT r; screen_target(c, &r);
            double base = (double)(r.right - r.left) / GR_MAX_X * c->mouseSens;
            double dx = e->x - M.lastX, dy = e->y - M.lastY;
            double g = gain(sqrt(dx * dx + dy * dy), c->mouseAccel, c->mouseMaxGain);
            M.x = clampd(M.x + dx * base * g, v.left, v.right - 1);
            M.y = clampd(M.y + dy * base * g, v.top, v.bottom - 1);
        }
        M.lastX = e->x; M.lastY = e->y; M.haveLast = 1;
    } else {
        map_absolute(c, e->x, e->y, &M.x, &M.y);
    }
    mouse_move_to(M.x, M.y);
    M.inRange = 1;

    if (e->left != M.left)     mouse_send(e->left ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP, 0);
    if (e->right != M.right)   mouse_send(e->right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP, 0);
    if (e->middle != M.middle) mouse_send(e->middle ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP, 0);
    M.left = e->left; M.right = e->right; M.middle = e->middle;

    if (e->wheel) mouse_send(MOUSEEVENTF_WHEEL, (c->invertWheel ? -e->wheel : e->wheel) * WHEEL_DELTA);

    l->screenX = (int)M.x; l->screenY = (int)M.y; l->outPressure = 0;
}

/* ---------- public ---------- */

static config_t g_lastCfg;

int output_init(const config_t *c)
{
    POINTER_FEEDBACK_MODE fb = (POINTER_FEEDBACK_MODE)(c->feedback >= 1 && c->feedback <= 3 ? c->feedback : 1);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    pCreate  = (pfnCreate)(void *)GetProcAddress(u, "CreateSyntheticPointerDevice");
    pInject  = (pfnInject)(void *)GetProcAddress(u, "InjectSyntheticPointerInput");
    pDestroy = (pfnDestroy)(void *)GetProcAddress(u, "DestroySyntheticPointerDevice");
    if (!pCreate || !pInject || !pDestroy) {
        app_log("Pen injection API missing from user32 (needs Windows 10 1809 or newer)");
        return 0;
    }
    g_pen = pCreate(PT_PEN, 1, fb);
    if (!g_pen) {
        app_log("CreateSyntheticPointerDevice failed, error %lu (needs Windows 10 1809+)", GetLastError());
        return 0;
    }
    app_log("Synthetic pen device created (feedback mode %d)", (int)fb);
    return 1;
}

void output_shutdown(void)
{
    output_release_all();
    if (g_pen) { pDestroy(g_pen); g_pen = NULL; }
}

void output_release_all(void)
{
    pen_buttons_release(&g_lastCfg);
    if (P.inRange || P.inContact) pen_leave();
    if (M.inRange || M.left || M.right || M.middle) mouse_release();
}

void output_event(const config_t *c, const gr_event *e, live_t *l)
{
    /* if the pen side-switch mapping changed while pressed, release with the old mapping */
    if (g_lastCfg.lower != c->lower || g_lastCfg.upper != c->upper) pen_buttons_release(&g_lastCfg);
    g_lastCfg = *c;

    gr_tool t = e->tool;
    if (t == GR_TOOL_MOUSE) {
        if (P.inRange) { pen_buttons_release(c); pen_leave(); }
        mouse_event_(c, e, l);
    } else if (t == GR_TOOL_PEN || t == GR_TOOL_ERASER) {
        if (M.inRange) mouse_release();
        pen_event(c, e, l);
    } else if (!e->prox) {
        output_release_all();
    }
}
