#ifndef POLEPOS_INTERNAL_H
#define POLEPOS_INTERNAL_H
#include "polepos.h"
extern uint16_t pp_sprite16[0x800], pp_road16[0x400], pp_alpha16[0x400], pp_view16[0x800];
extern uint16_t pp_hscroll, pp_road_vscroll;
extern uint8_t pp_chacl;
void pp_video_init(const pp_roms_t *r);
void pp_video_render(uint8_t *fb);
void pp_sound_init(const pp_roms_t *r);
void pp_sound_reset(void);
void pp_wsg_write(int reg, uint8_t d);
uint8_t pp_wsg_read(int reg);
void pp_sound_enable(int on);
void pp_engine_lsb(uint8_t d);
void pp_engine_msb(uint8_t d);
void pp_n52_write(uint8_t d);
void pp_n54_write(uint8_t d);
void pp_sound_render(int16_t *buf, int samples, int rate);
#endif
