#pragma once
#include "polepos.h"
#ifdef __cplusplus
extern "C" {
#endif
void input_init(void);
void input_update(pp_input_t *in);
extern uint32_t input_dbg_presses[2];
extern uint8_t input_dbg_levels;
extern int16_t input_dbg_accel[3]; extern float input_dbg_angle; extern uint8_t input_dbg_steer, input_dbg_neutral;
#ifdef __cplusplus
}
#endif
