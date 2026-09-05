/*
 * QUALIFIER - Namco Pole Position (1982) on the Waveshare ESP32-C6-LCD-1.69 Fiesta medal
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include "display.h"
#include "polepos.h"
#include "polepos_roms.h"
#include "render.h"
#include "input.h"
#include "audio_hal.h"

static const char *TAG = "QUAL";
#define DEBUG_LOG 1
static const int64_t FRAME_US = (int64_t)PP_CYCLES_PER_FRAME * 1000000 / PP_CPU_CLOCK;

extern "C" void app_main(void)
{
#if !DEBUG_LOG
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
    ESP_LOGI(TAG, "QUALIFIER starting, free heap %lu", (unsigned long)esp_get_free_heap_size());
    display_init();
    display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE);

    auto to_ram = [](const uint8_t *src, size_t n) {
        uint8_t *dst = (uint8_t *)malloc(n);
        if (!dst) { ESP_LOGE(TAG, "ROM RAM copy failed (%u bytes)", (unsigned)n); abort(); }
        memcpy(dst, src, n); return (const uint8_t *)dst;
    };
    pp_roms_t roms = {
        to_ram(pp_rom_z80, sizeof(pp_rom_z80)), pp_rom_sub1, pp_rom_sub2,   /* the core keeps its own word copies of the sub ROMs */
        /* graphics the renderer touches per pixel go to RAM; the 128 KB big-sprite set stays in flash */
        to_ram(pp_chars, sizeof(pp_chars)), to_ram(pp_tiles, sizeof(pp_tiles)), to_ram(pp_sprites, sizeof(pp_sprites)), pp_bigsprites,
        to_ram(pp_road, sizeof(pp_road)), to_ram(pp_scalelut, sizeof(pp_scalelut)), to_ram(pp_proms, sizeof(pp_proms)), pp_wave, pp_engine, pp_voice };
    pp_init(&roms);
    pp_set_dips(0xff, 0x6b);
    pp_set_time_source([]() -> uint64_t { return (uint64_t)esp_timer_get_time(); });
    render_init();
    input_init();
    audio_init();
    ESP_LOGI(TAG, "ready, free heap %lu", (unsigned long)esp_get_free_heap_size());

    int64_t last_us = esp_timer_get_time(), last_report = last_us, owed_us = 0;
    uint64_t t_emu = 0, t_render = 0, t_audio = 0;
    uint32_t frames = 0, skipped = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        owed_us += now - last_us;
        last_us = now;
        if (owed_us > 3 * FRAME_US) owed_us = 3 * FRAME_US;
        input_update(pp_input());
        while (owed_us >= FRAME_US) {
            int64_t t0 = esp_timer_get_time();
            pp_run_frame();
            int64_t t1 = esp_timer_get_time();
            t_emu += t1 - t0;
            frames++;
            owed_us -= FRAME_US;
            if (owed_us < FRAME_US) {
                uint8_t *fb = render_acquire();
                if (fb) { pp_render(fb); render_submit(fb); t_render += esp_timer_get_time() - t1; }
                else skipped++;
            } else {
                skipped++;
            }
        }
        int64_t ta = esp_timer_get_time();
        audio_update();
        t_audio += esp_timer_get_time() - ta;
        vTaskDelay(1);
        if (now - last_report >= 5000000) {
            pp_stats_t *st = pp_stats();
            ESP_LOGI(TAG, "5s: frames %lu drawn %lu skipped %lu dropped %lu; ms/s: emu %llu (z80 %llu sub1 %llu sub2 %llu) render %llu audio %llu; heap %lu; pc %04X %04X %04X latch %02X",
                     (unsigned long)frames, (unsigned long)render_frames_drawn(), (unsigned long)skipped, (unsigned long)render_frames_dropped(),
                     (unsigned long long)(t_emu / 5000), (unsigned long long)(st->cpu_us[0] / 5000), (unsigned long long)(st->cpu_us[1] / 5000), (unsigned long long)(st->cpu_us[2] / 5000),
                     (unsigned long long)(t_render / 5000), (unsigned long long)(t_audio / 5000),
                     (unsigned long)esp_get_free_heap_size(), pp_pc(0), pp_pc(1), pp_pc(2), pp_latch());
            frames = skipped = 0; t_emu = t_render = t_audio = 0; last_report = now; memset(st, 0, sizeof(*st));
        }
    }
}
