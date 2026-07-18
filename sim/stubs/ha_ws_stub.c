/* Stub deps for compiling main/ha/ha_ws_status_screen.c in the simulator.
 *
 * The real WebSocket client (ha_ws.c) is a model file the sim never builds;
 * the status screen only needs a snapshot, which presents as an unconfigured,
 * disabled device — same philosophy as ha_cfg_stub.c. */

#include <string.h>

#include "ha_ws.h"

void ha_ws_status_get(ha_ws_status_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    out->status = HA_WS_STATUS_DISABLED;
}
