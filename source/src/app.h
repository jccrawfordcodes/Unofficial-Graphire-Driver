#ifndef APP_H
#define APP_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A00000B
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "graphire.h"

#define APP_NAME    L"Graphire Driver"
#define APP_VERSION "1.0.0"

typedef enum {
    ACT_NONE = 0, ACT_BARREL, ACT_LEFT, ACT_RIGHT, ACT_MIDDLE, ACT_DOUBLE
} btn_action;

typedef struct {
    /* [Mapping] */
    int    relative;                 /* pen: 0 = absolute, 1 = relative */
    int    areaL, areaT, areaR, areaB;
    int    monitor;                  /* 0 = all monitors, n = n-th monitor */
    int    keepAspect;
    /* [Relative] */
    double relSens, relAccel, relMaxGain;
    /* [Pressure] */
    int    pMin, pMax;
    double pCurve;
    int    contactThreshold;         /* 0 = use tablet tip switch */
    /* [Buttons] */
    btn_action lower, upper;
    int    eraser;
    /* [Smoothing] */
    double smoothing;                /* 0 .. 0.95 */
    /* [Mouse] (Graphire puck) */
    int    mouseRelative;
    double mouseSens, mouseAccel, mouseMaxGain;
    int    invertWheel;
    /* [Device] */
    int    pid;                      /* 0 = any Wacom HID device */
    int    feedback;                 /* 1 default, 2 indirect, 3 none */
    /* [Debug] */
    int    logRaw;
} config_t;

/* live state shown in the status window */
typedef struct {
    int connected;
    int modeSwitched;
    gr_event last;
    int maxPressureSeen;
    unsigned long reports;
    int screenX, screenY, outPressure;
} live_t;

/* main.c */
void     app_log(const char *fmt, ...);
void     cfg_get(config_t *out);           /* thread-safe snapshot */
void     live_update(const live_t *l);
void     live_get(live_t *out);
int      app_paused(void);
void     app_set_status(const wchar_t *s);
void     screen_target(const config_t *c, RECT *r);   /* mapped screen rect */
void     screen_virtual(RECT *r);

/* device.c */
void     device_start(void);
void     device_stop(void);

/* output.c */
int      output_init(const config_t *c);
void     output_shutdown(void);
void     output_event(const config_t *c, const gr_event *e, live_t *l);
void     output_release_all(void);

#endif
