/*
 * input.cpp - medal controls for Pole Position (held sideways, like a wheel)
 *
 * The battery rail, the buttons and the tilt zero live in components/medal_input, which every
 * medal shares. Pole Position takes two things into its own hands:
 *
 *   The PWR button has three bands, so medal_input's coin-then-start sequence is switched off
 *   (manual_pwr) and this reads the raw press lengths instead: a tap is a coin, a medium press
 *   changes gear, a long one powers off.
 *
 *   The sound gesture is on BOTH buttons rather than a long hold of BOOT, because here BOOT is
 *   the accelerator. You hold the throttle down for the whole of a lap, so the usual three
 *   second hold toggled the sound a few seconds into every single race.
 *
 *   tilt (rotate it like a wheel) -> steering
 *   BOOT button                   -> accelerator (full throttle)
 *   PWR tap (<0.3 s)              -> coin
 *   PWR 0.3-1.5 s                 -> gear change
 *   PWR hold 2 s                  -> power off
 *   BOOT + PWR together, 1 s      -> sound off and on
 */
#include "input.h"
#include "medal_input.h"
#include "medalboot.h"
#include "qmi8658.h"
#include "audio_hal.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "INPUT";
#define FULL_LOCK_DEG 30.0f       /* tilt for full wheel deflection */
#define FULL_LOCK_COUNTS 12.0f    /* wheel counts at full deflection: 8 counts is already a hard swerve */
#define DEADBAND_DEG 1.5f
#define STEER_SIGN (-1.0f)
#define HOLD_MUTE_US 1000000      /* both buttons together, for this long */

static int64_t coin_until, both_down_since;
static bool both_armed, both_fired;
uint32_t input_dbg_presses[2];           /* BOOT, PWR press edges since boot (diagnostics) */
uint8_t input_dbg_levels;                /* raw levels: bit0 BOOT, bit1 PWR (1 = released) */
int16_t input_dbg_accel[3]; float input_dbg_angle; uint8_t input_dbg_steer; uint8_t input_dbg_neutral;

static void read_accel_dbg(int16_t *x, int16_t *y, int16_t *z)
{
    qmi8658_read_accel(x, y, z);
    input_dbg_accel[0] = *x; input_dbg_accel[1] = *y; input_dbg_accel[2] = *z;
}

void input_init(void)
{
    medal_input_config_t cfg = {};
    cfg.init_i2c = true;
    cfg.imu_init = qmi8658_init;
    cfg.read_accel = read_accel_dbg;
    cfg.power_off_hold_us = 2000000;
    cfg.manual_pwr = true;            /* PWR has three bands here; see the header comment */
    cfg.mute_hold_us = 0;             /* BOOT is the throttle, so the mute is on both buttons */
    cfg.exit_hold_us = MEDALBOOT_EXIT_HOLD_MS * 1000;   /* hold to leave for the menu */
    cfg.on_exit = medalboot_exit_to_menu;
    medal_input_init(&cfg);
}

void input_update(pp_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);
    int64_t now = esp_timer_get_time();

    if (st.boot && st.boot_held_us == 0) input_dbg_presses[0]++;
    if (st.pwr && st.pwr_held_us == 0) input_dbg_presses[1]++;
    input_dbg_levels = (uint8_t)((st.boot ? 0 : 1) | (st.pwr ? 0 : 2));

    in->accel = st.boot ? 0x90 : 0;
    in->brake = 0;

    /* sound: both buttons, held together */
    bool both = st.boot && st.pwr;
    if (both && !both_armed) { both_armed = true; both_fired = false; both_down_since = now; }
    if (!both) both_armed = false;
    if (both && !both_fired && now - both_down_since >= HOLD_MUTE_US) {
        both_fired = true;
        audio_set_mute(!audio_get_mute());
        ESP_LOGI(TAG, "sound %s", audio_get_mute() ? "off" : "on");
    }

    if (st.pwr_released && !both_fired) {
        int64_t held = st.pwr_release_held_us;
        if (held < 300000) {
            coin_until = now + 150000;
            medal_input_recentre();                  /* a coin also re-centres the wheel */
            ESP_LOGI(TAG, "coin (wheel re-centred)");
        } else if (held < 1500000) {
            in->gear = !in->gear;
            ESP_LOGI(TAG, "gear %s", in->gear ? "high" : "low");
        }
    }
    in->coin1 = now < coin_until;

    if (st.tilt_valid) {
        float d = st.lr;
        if (d > -DEADBAND_DEG && d < DEADBAND_DEG) d = 0;
        float v = 128.0f + STEER_SIGN * d * (FULL_LOCK_COUNTS / FULL_LOCK_DEG);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        in->steer = (uint8_t)v;
    } else {
        in->steer = 128;
    }
    input_dbg_angle = st.lr;
    input_dbg_steer = in->steer;
    input_dbg_neutral = medal_input_have_neutral();
}
