#include "graphire.h"
#include <string.h>

const char *gr_tool_name(gr_tool t)
{
    switch (t) {
    case GR_TOOL_PEN:    return "pen";
    case GR_TOOL_ERASER: return "eraser";
    case GR_TOOL_MOUSE:  return "mouse";
    default:             return "none";
    }
}

int gr_parse(gr_state *st, const uint8_t *d, size_t n, gr_event *ev)
{
    memset(ev, 0, sizeof *ev);
    if (n < 8 || d[0] != GR_REPORT_PEN)
        return 0;

    ev->prox = (d[1] & 0x80) != 0;
    if (!ev->prox) {
        ev->tool = st->cur;
        st->cur = GR_TOOL_NONE;
        return 1;
    }

    switch ((d[1] >> 5) & 3) {
    case 0: ev->tool = GR_TOOL_PEN; break;
    case 1: ev->tool = GR_TOOL_ERASER; break;
    case 2: ev->middle = (d[1] & 0x04) != 0; /* mouse with wheel */
            /* fall through */
    default: ev->tool = GR_TOOL_MOUSE; break;
    }
    st->cur = ev->tool;

    ev->x = d[2] | (d[3] << 8);
    ev->y = d[4] | (d[5] << 8);

    if (ev->tool != GR_TOOL_MOUSE) {
        ev->pressure  = d[6] | ((d[7] & 0x03) << 8);
        ev->tip       = (d[1] & 0x01) != 0;
        ev->btn_lower = (d[1] & 0x02) != 0;
        ev->btn_upper = (d[1] & 0x04) != 0;
    } else {
        ev->left     = (d[1] & 0x01) != 0;
        ev->right    = (d[1] & 0x02) != 0;
        ev->wheel    = -(int)(int8_t)d[6];
        ev->distance = d[7] & 0x3f;
    }
    return 1;
}
