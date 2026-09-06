/*
 * input.cpp - medal controls for Pole Position (held sideways like a wheel)
 *   tilt (IMU) -> steering wheel
 *   BOOT button -> accelerator (full throttle)
 *   PWR button: tap (<0.3 s) -> coin; hold 0.3-1.5 s and release -> gear change; hold 2 s -> power off
 */
#include "input.h"
#include "qmi8658.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "INPUT";
#define PIN_BTN_BOOT GPIO_NUM_9
#define PIN_BTN_PWR  GPIO_NUM_18
#define PIN_BAT_EN   GPIO_NUM_15
#define IMU_PERIOD_US 16000
#define FULL_LOCK_DEG 30.0f       /* tilt for full wheel deflection */
#define FULL_LOCK_COUNTS 12.0f    /* wheel counts at full deflection: 8 counts is already a hard swerve */
#define DEADBAND_DEG 1.5f
#define STEER_SIGN (-1.0f)

static bool imu_ok, pwr_was_down;
static int64_t pwr_down_since, imu_last_us, coin_until;
static bool boot_was_down;
uint32_t input_dbg_presses[2];           /* BOOT, PWR press edges since boot (diagnostics) */
uint8_t input_dbg_levels;                /* raw levels: bit0 BOOT, bit1 PWR (1 = released) */
int16_t input_dbg_accel[3]; float input_dbg_angle; uint8_t input_dbg_steer; uint8_t input_dbg_neutral;
static float neutral_ang; static bool have_neutral;

static float wheel_angle(void)
{
    int16_t ax, ay, az;
    qmi8658_read_accel(&ax, &ay, &az);
    input_dbg_accel[0] = ax; input_dbg_accel[1] = ay; input_dbg_accel[2] = az;
    /* rotating the medal like a wheel (any grip) rotates gravity within the panel plane */
    return input_dbg_angle = atan2f((float)ay, (float)ax) * 57.2958f;
}

void input_init(void)
{
    gpio_config_t bat = {}; bat.pin_bit_mask = 1ULL << PIN_BAT_EN; bat.mode = GPIO_MODE_OUTPUT; gpio_config(&bat);
    gpio_set_level(PIN_BAT_EN, 1);
    gpio_config_t io = {}; io.pin_bit_mask = (1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_PWR); io.mode = GPIO_MODE_INPUT; io.pull_up_en = GPIO_PULLUP_ENABLE; gpio_config(&io);
    i2c_config_t i2c = {}; i2c.mode = I2C_MODE_MASTER; i2c.sda_io_num = GPIO_NUM_8; i2c.scl_io_num = GPIO_NUM_7;
    i2c.sda_pullup_en = GPIO_PULLUP_ENABLE; i2c.scl_pullup_en = GPIO_PULLUP_ENABLE; i2c.master.clk_speed = 100000;
    i2c_param_config(I2C_NUM_0, &i2c);
    esp_err_t err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "I2C init failed: %s", esp_err_to_name(err));
    imu_ok = qmi8658_init();
    ESP_LOGI(TAG, "input ready (IMU %s); neutral wheel pose is captured on the first throttle press", imu_ok ? "ok" : "missing");
}

void input_update(pp_input_t *in)
{
    int64_t now = esp_timer_get_time();
    bool boot = gpio_get_level(PIN_BTN_BOOT) == 0;
    bool pwr = gpio_get_level(PIN_BTN_PWR) == 0;

    if (boot && !boot_was_down) input_dbg_presses[0]++;
    if (pwr && !pwr_was_down) input_dbg_presses[1]++;
    input_dbg_levels = (uint8_t)((gpio_get_level(PIN_BTN_BOOT) ? 1 : 0) | (gpio_get_level(PIN_BTN_PWR) ? 2 : 0));
    boot_was_down = boot;
    in->accel = boot ? 0x90 : 0;
    in->brake = 0;
    if (boot && !have_neutral && imu_ok) { neutral_ang = wheel_angle(); have_neutral = true; ESP_LOGI(TAG, "neutral wheel pose captured"); }

    if (pwr && !pwr_was_down) pwr_down_since = now;
    if (pwr && now - pwr_down_since >= 2000000) {
        ESP_LOGI(TAG, "power off");
        gpio_set_level(PIN_BAT_EN, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!pwr && pwr_was_down) {
        int64_t held = now - pwr_down_since;
        if (held < 300000) {
            coin_until = now + 150000;
            if (imu_ok) { neutral_ang = wheel_angle(); have_neutral = true; }   /* a coin also re-centres the wheel */
            ESP_LOGI(TAG, "coin (wheel centred at %.1f)", (double)neutral_ang);
        }
        else if (held < 1500000) { in->gear = !in->gear; ESP_LOGI(TAG, "gear %s", in->gear ? "high" : "low"); }
    }
    pwr_was_down = pwr;
    in->coin1 = now < coin_until;

    if (imu_ok && now - imu_last_us >= IMU_PERIOD_US) {
        imu_last_us = now;
        if (have_neutral) {
            float d = wheel_angle() - neutral_ang;
            while (d > 180) d -= 360;
            while (d < -180) d += 360;
            if (d > -DEADBAND_DEG && d < DEADBAND_DEG) d = 0;
            float v = 128.0f + STEER_SIGN * d * (FULL_LOCK_COUNTS / FULL_LOCK_DEG);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            in->steer = (uint8_t)v;
        } else {
            wheel_angle();                           /* keep the diagnostics live before the first throttle press */
            in->steer = 128;
        }
        input_dbg_steer = in->steer; input_dbg_neutral = have_neutral;
    }
}
