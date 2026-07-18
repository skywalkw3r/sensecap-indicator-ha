#include "indicator_enabler.h"

#include "nav.h"
#include "view_data.h"

extern int indicator_display_view_init(void);

int indicator_view_init(void) {
	nav_init();

#ifdef INDICATOR_DISPLAY_H
	indicator_display_view_init();
#endif

	/* view_sensor_init() intentionally not called: the sensor data tile was
	 * removed (base D1 hardware has no onboard sensors — see nav.h). The
	 * sensor model keeps running harmlessly; nothing consumes its events. */

#ifdef WIFI_H
	indicator_wifi_view_init();
#endif

#ifdef HA_H
	indicator_ha_view_init();
#endif

#ifdef SETTINGS_H
	settings_view_init();
#endif

	return 0;
}
