/*
 * render.cpp - rotate the 256x224 native frame onto the 240x280 panel held sideways (landscape).
 * Panel (px 0..239, py 0..279): game x = py - 12, game y = 223 - (px - 8) (flip with the defines).
 */
#include "render.h"
#include "polepos.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "RENDER";
#define ROWS_PER_CHUNK 14
#define NUM_FB 1
#define X_MARGIN 12           /* (280 - 256) / 2 along the long axis */
#define Y_MARGIN 8            /* (240 - 224) / 2 along the short axis */
#define FLIP_X 0
#define FLIP_Y 1

static uint8_t *fbs[NUM_FB];
static QueueHandle_t free_q, frame_q;
static uint16_t *chunk;
static uint16_t pal_swapped[256];
static uint32_t frames_drawn, frames_dropped, present_cycles;

static void present(const uint8_t *fb)
{
    display_set_window(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    for (int row = 0; row < DISPLAY_HEIGHT; row += ROWS_PER_CHUNK) {
        int rows = (row + ROWS_PER_CHUNK <= DISPLAY_HEIGHT) ? ROWS_PER_CHUNK : (DISPLAY_HEIGHT - row);
        uint16_t *dst = chunk;
        uint32_t c0 = esp_cpu_get_cycle_count();
        for (int r = 0; r < rows; r++) {
            int py = row + r;
            int gx = py - X_MARGIN;
#if FLIP_X
            gx = PP_FB_W - 1 - gx;
#endif
            if (gx < 0 || gx >= PP_FB_W) { for (int px = 0; px < DISPLAY_WIDTH; px++) *dst++ = 0; continue; }
            for (int px = 0; px < Y_MARGIN; px++) *dst++ = 0;
#if FLIP_Y
            const uint8_t *src = fb + (PP_FB_H - 1) * PP_FB_W + gx;      /* gy = 223 downwards */
            for (int gy = 0; gy < PP_FB_H; gy++) { *dst++ = pal_swapped[*src]; src -= PP_FB_W; }
#else
            const uint8_t *src = fb + gx;
            for (int gy = 0; gy < PP_FB_H; gy++) { *dst++ = pal_swapped[*src]; src += PP_FB_W; }
#endif
            for (int px = 0; px < DISPLAY_WIDTH - Y_MARGIN - PP_FB_H; px++) *dst++ = 0;
        }
        present_cycles += esp_cpu_get_cycle_count() - c0;
        display_write_preswapped(chunk, rows * DISPLAY_WIDTH);
    }
    display_wait_done();
}

static void render_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint8_t *fb;
        if (xQueueReceive(frame_q, &fb, portMAX_DELAY) != pdTRUE) continue;
        present(fb);
        xQueueSend(free_q, &fb, 0);
        frames_drawn++;
    }
}

void render_init(void)
{
    const uint16_t *pal = pp_palette();
    for (int i = 0; i < 256; i++) {
        uint16_t c = pal[i & 0x7f];
        pal_swapped[i] = (uint16_t)((c >> 8) | (c << 8));
    }
    chunk = (uint16_t *)heap_caps_malloc(ROWS_PER_CHUNK * DISPLAY_WIDTH * sizeof(uint16_t), MALLOC_CAP_8BIT);
    free_q = xQueueCreate(NUM_FB, sizeof(uint8_t *));
    frame_q = xQueueCreate(NUM_FB, sizeof(uint8_t *));
    for (int i = 0; i < NUM_FB; i++) {
        fbs[i] = (uint8_t *)heap_caps_malloc(PP_FB_W * PP_FB_H, MALLOC_CAP_8BIT);
        if (!fbs[i]) { ESP_LOGE(TAG, "frame buffer allocation failed"); abort(); }
        xQueueSend(free_q, &fbs[i], 0);
    }
    if (!chunk) { ESP_LOGE(TAG, "chunk allocation failed"); abort(); }
    xTaskCreate(render_task, "render", 4096, nullptr, 6, nullptr);
    ESP_LOGI(TAG, "render task started");
}

uint8_t *render_acquire(void)
{
    uint8_t *fb;
    if (xQueueReceive(free_q, &fb, 0) != pdTRUE) { frames_dropped++; return nullptr; }
    return fb;
}
void render_submit(uint8_t *fb) { xQueueSend(frame_q, &fb, 0); }
uint32_t render_frames_drawn(void) { uint32_t v = frames_drawn; frames_drawn = 0; return v; }
uint32_t render_frames_dropped(void) { uint32_t v = frames_dropped; frames_dropped = 0; return v; }
uint32_t render_present_us(void) { uint32_t v = present_cycles / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ; present_cycles = 0; return v; }
