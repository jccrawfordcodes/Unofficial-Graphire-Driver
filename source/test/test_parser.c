#include "../src/graphire.h"
#include <stdio.h>
#include <assert.h>

int main(void)
{
    gr_state st = {0};
    gr_event e;

    /* pen hovering at (1000, 2000) */
    uint8_t hover[8] = {2, 0x80, 0xE8, 0x03, 0xD0, 0x07, 0, 0};
    assert(gr_parse(&st, hover, 8, &e));
    assert(e.prox && e.tool == GR_TOOL_PEN && e.x == 1000 && e.y == 2000 && !e.tip);

    /* pen touching, pressure 300 (0x12C), lower button */
    uint8_t touch[8] = {2, 0x83, 0x10, 0x27, 0xFE, 0x1C, 0x2C, 0x01};
    assert(gr_parse(&st, touch, 8, &e));
    assert(e.tip && e.btn_lower && !e.btn_upper && e.pressure == 300);
    assert(e.x == 10000 && e.y == 7422);

    /* leave proximity */
    uint8_t leave[8] = {2, 0x00, 0, 0, 0, 0, 0, 0};
    assert(gr_parse(&st, leave, 8, &e));
    assert(!e.prox && e.tool == GR_TOOL_PEN && st.cur == GR_TOOL_NONE);

    /* eraser */
    uint8_t er[8] = {2, 0xA1, 1, 0, 1, 0, 0xFF, 0x01};
    assert(gr_parse(&st, er, 8, &e));
    assert(e.tool == GR_TOOL_ERASER && e.tip && e.pressure == 511);

    /* mouse with wheel, middle + left, wheel byte -1 => scroll up */
    uint8_t ms[8] = {2, 0xC5, 0, 0, 0, 0, 0xFF, 0x05};
    assert(gr_parse(&st, ms, 8, &e));
    assert(e.tool == GR_TOOL_MOUSE && e.left && e.middle && !e.right && e.wheel == 1 && e.distance == 5);

    /* non-pen report (mouse-mode report id 1) is ignored */
    uint8_t m1[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    assert(!gr_parse(&st, m1, 8, &e));
    assert(!gr_parse(&st, touch, 5, &e));

    puts("parser tests passed");
    return 0;
}
