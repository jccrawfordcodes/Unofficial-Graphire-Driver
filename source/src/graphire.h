/* Wacom Graphire / Graphire2 (ET-0405A-U = Graphire2 4x5, USB 056A:0011) report parser.
 * Protocol per Linux drivers/hid/wacom_wac.c (wacom_graphire_irq). */
#ifndef GRAPHIRE_H
#define GRAPHIRE_H
#include <stdint.h>
#include <stddef.h>

#define GR_VID          0x056A
#define GR_PID          0x0011
#define GR_MAX_X        10206
#define GR_MAX_Y        7422
#define GR_MAX_PRESSURE 511
#define GR_REPORT_PEN   2      /* report id used in "Wacom mode" */

typedef enum { GR_TOOL_NONE = 0, GR_TOOL_PEN, GR_TOOL_ERASER, GR_TOOL_MOUSE } gr_tool;

typedef struct {
    int prox;          /* tool is in proximity */
    gr_tool tool;      /* tool (on leave: the tool that left) */
    int x, y;          /* tablet units */
    /* pen / eraser */
    int pressure;      /* 0..1023 raw (Graphire max 511) */
    int tip;           /* touching surface */
    int btn_lower;     /* side switch nearest tip */
    int btn_upper;     /* side switch farther from tip */
    /* mouse (puck) */
    int left, right, middle;
    int wheel;         /* +1 = scroll up, -1 = down */
    int distance;
} gr_event;

typedef struct { gr_tool cur; } gr_state;

/* Returns 1 if an event was decoded, 0 if the report is not a pen report. */
int gr_parse(gr_state *st, const uint8_t *d, size_t n, gr_event *ev);

const char *gr_tool_name(gr_tool t);

#endif
