#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void render_init(void);
uint8_t *render_acquire(void);
void render_submit(uint8_t *fb);
uint32_t render_frames_drawn(void);
uint32_t render_frames_dropped(void);
uint32_t render_present_us(void);   /* CPU time spent converting frames for the panel (not DMA waits) */
#ifdef __cplusplus
}
#endif
