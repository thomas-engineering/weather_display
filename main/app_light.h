/* Ambient-light sensing via the optional OV5647 on the MIPI-CSI connector.
 *
 * The board has no dedicated light sensor. The camera's SCCB (I2C) bus is the
 * same shared bus the BSP already brings up for the touch controller — see
 * BSP_I2C_SCL/SDA (GPIO 8/7) in the Waveshare BSP header, confirmed against
 * the vendor's own Arduino camera example, which uses the identical pins.
 * There is no separate reset/power-down GPIO on this board's camera FPC
 * connector either (the vendor example configures neither), so both are left
 * unset (-1) here too.
 *
 * The camera is optional hardware: app_light_init() must be safe to call on a
 * board with nothing plugged into the CSI connector, and everything else in
 * the app must keep working (manual brightness in particular) when it is.
 */
#ifndef APP_LIGHT_H
#define APP_LIGHT_H

#include <stdbool.h>
#include "driver/i2c_master.h"

/* Brings up the CSI/SCCB video device and probes for a responding sensor.
 * Safe to call with no camera fitted — logs and returns false rather than
 * failing app_main(). Call once, after the BSP's shared I2C bus is already
 * initialized (i.e. after bsp_display_start_with_config()).
 *
 * @param i2c_bus The BSP's shared I2C bus handle (bsp_i2c_get_handle()). The
 *                camera's SCCB reuses it instead of owning a second bus. */
bool app_light_init(i2c_master_bus_handle_t i2c_bus);

/* True if app_light_init() found a sensor that responds. False forever after
 * a failed init — there is no hot-plug detection, matching the rest of this
 * board's fixed peripherals. */
bool app_light_available(void);

/* Percent is 10-100, the same range and meaning as the manual brightness
 * slider. Only fires while adaptive mode is on and a camera is available. */
typedef void (*app_light_brightness_cb_t)(int percent);
void app_light_set_callback(app_light_brightness_cb_t cb);

/* Enables/disables periodic sampling. A no-op (and stays reported as off) if
 * app_light_available() is false — callers don't need to check both.
 *
 * The camera only actually streams while adaptive mode is on: enabling this
 * opens the CSI device and starts capturing, disabling it tears the stream
 * and its buffers back down. Toggling is cheap enough to call from the UI
 * event that flips the switch — the actual work happens on app_light's own
 * task, not the caller's. */
void app_light_set_adaptive(bool enabled);

/* Stops sampling and releases the camera entirely (stream, buffers, the
 * esp_video subsystem, the sampling task). Not called anywhere today — this
 * app never intentionally shuts the camera off for good — but provided for a
 * future power-down path. Never call this from app_light's own sampling
 * task. */
void app_light_deinit(void);

#endif
