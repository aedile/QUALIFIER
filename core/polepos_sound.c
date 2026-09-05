/*
 * polepos_sound.c - 8-voice Namco WSG, engine sample generator, 52XX speech samples,
 * 54XX noise (high-level). Follows MAME's namco.cpp (polepos_wsg), polepos_a.cpp and
 * the pre-MCU 52XX model.
 */
#include "polepos_internal.h"
#include <string.h>
#include <math.h>

static const pp_roms_t *R;
static int sound_on;

/* ---- WSG ---- */
static uint8_t regs[0x40];
static uint32_t vcount[8];
#define WSG_RATE 192000
#define WSG_FRAC 17

void pp_wsg_write(int reg, uint8_t d) { regs[reg & 0x3f] = d; }
uint8_t pp_wsg_read(int reg) { return regs[reg & 0x3f]; }
void pp_sound_enable(int on) { sound_on = on; }

static void wsg_render(int16_t *buf, int samples, int rate)
{
    if (!sound_on) return;
    uint32_t step_scale = (uint32_t)(((uint64_t)WSG_RATE << 8) / (uint32_t)rate);   /* 24.8 */
    for (int ch = 0; ch < 8; ch++) {
        uint32_t freq = regs[ch * 4 + 0] | ((uint32_t)regs[ch * 4 + 1] << 8);
        int wave = regs[ch * 4 + 0x23] & 7;
        int fl = regs[ch * 4 + 3] >> 4, fr = regs[ch * 4 + 3] & 0x0f;
        int rl = regs[ch * 4 + 0x23] >> 4, rr = regs[ch * 4 + 2] >> 4;
        if (regs[ch * 4 + 0x23] & 8) fl = fr = rl = rr = 0;
        int vol = fl + fr + (rl + rr) / 2;
        if (!vol || !freq) continue;
        const uint8_t *w = R->wave + wave * 32;
        uint32_t step = (uint32_t)(((uint64_t)freq * step_scale) >> 8);
        uint32_t c = vcount[ch];
        for (int i = 0; i < samples; i++) {
            int s = ((int)(w[(c >> WSG_FRAC) & 0x1f] & 0x0f) - 8) * vol;   /* +-8 * up to 60 */
            int32_t v = buf[i] + s * 12;
            if (v > 32767) v = 32767; if (v < -32768) v = -32768;
            buf[i] = (int16_t)v;
            c += step;
        }
        vcount[ch] = c;
    }
}

/* ---- biquad (MAME filter2_context) ---- */
typedef struct { double a1, a2, b0, b1, b2, x0, x1, x2, y0, y1, y2; } biquad_t;
static void biquad_setup(biquad_t *f, int type, double fc, double d, double gain, int rate)
{
    double two_over_T = 2.0 * rate, two_over_T2 = two_over_T * two_over_T;
    double w = rate * 2.0 * tan(M_PI * fc / rate), w2 = w * w;
    double den = two_over_T2 + d * w * two_over_T + w2;
    f->a1 = 2.0 * (-two_over_T2 + w2) / den;
    f->a2 = (two_over_T2 - d * w * two_over_T + w2) / den;
    if (type == 0) { f->b0 = f->b2 = w2 / den; f->b1 = 2.0 * f->b0; }                 /* lowpass */
    else if (type == 2) { f->b0 = d * w * two_over_T / den; f->b1 = 0; f->b2 = -f->b0; } /* bandpass */
    else { f->b0 = f->b2 = two_over_T2 / den; f->b1 = -2.0 * f->b0; }                    /* highpass */
    f->b0 *= gain; f->b1 *= gain; f->b2 *= gain;
    f->x0 = f->x1 = f->x2 = f->y0 = f->y1 = f->y2 = 0;
}
static inline double biquad_step(biquad_t *f, double x)
{
    f->x0 = x;
    f->y0 = -f->a1 * f->y1 - f->a2 * f->y2 + f->b0 * f->x0 + f->b1 * f->x1 + f->b2 * f->x2;
    f->x2 = f->x1; f->x1 = f->x0; f->y2 = f->y1; f->y1 = f->y0;
    return f->y0;
}
static void opamp_bandpass(biquad_t *f, double r1, double r2, double r3, double c1, double c2, int rate)
{
    double gain, r_in;
    if (r2 == 0) { gain = 1; r_in = r1; } else { gain = r2 / (r1 + r2); r_in = 1.0 / (1.0 / r1 + 1.0 / r2); }
    double fc = 1.0 / (2 * M_PI * sqrt(r_in * r3 * c1 * c2));
    double d = (c1 + c2) / sqrt(r3 / r_in * c1 * c2);
    gain *= -r3 / r_in * c2 / (c1 + c2);
    biquad_setup(f, 2, fc, d, gain, rate);
}

/* ---- engine ---- */
static uint8_t eng_msb, eng_lsb; static int eng_enable;
static uint32_t eng_pos;
static biquad_t eng_f[3]; static int eng_rate;
static const double volume_table[8] = {
    (1.0/(1.0/1000+1.0/250)*3 + 2200) / 10000, (1.0/(1.0/1000+1.0/250)*2 + 1000 + 2200) / 10000,
    (1.0/(1.0/1000+1.0/250)*2 + 2200 + 2200) / 10000, (1.0/(1.0/1000+1.0/250) + 2200 + 1000 + 2200) / 10000,
    (4700 + 1.0/(1.0/1000+1.0/250)*2 + 2200) / 10000, (4700 + 1.0/(1.0/1000+1.0/250) + 1000 + 2200) / 10000,
    (4700 + 2200 + 1.0/(1.0/1000+1.0/250) + 2200) / 10000, (4700 + 2200 + 1000 + 2200) / 10000 };
static const double r_filt_out[3] = { 4700, 7500, 10000 };
void pp_engine_lsb(uint8_t d) { eng_lsb = d & 62; eng_enable = d & 1; }
void pp_engine_msb(uint8_t d) { eng_msb = d & 63; }
static void engine_setup(int rate)
{
    eng_rate = rate;
    opamp_bandpass(&eng_f[0], 220e3, 33e3, 390e3, 0.01e-6, 0.01e-6, rate);
    opamp_bandpass(&eng_f[1], 150e3, 22e3, 330e3, 0.0047e-6, 0.0047e-6, rate);
    biquad_setup(&eng_f[2], 1, 950, 1.0 / 0.707, 1, rate);
}
static void engine_render(int16_t *buf, int samples, int rate)
{
    if (rate != eng_rate) engine_setup(rate);
    if (!eng_enable) return;
    uint32_t clock = (uint32_t)(((uint64_t)(3072000 / 16) * ((eng_msb + 1) * 64 + eng_lsb + 1)) / (64 * 64));
    uint32_t step = (uint32_t)(((uint64_t)clock << 12) / (uint32_t)rate);
    int slot = (eng_msb >> 3) & 7;
    double volume = volume_table[slot];
    const uint8_t *base = R->engine + slot * 0x800;
    double r_filt_total = 1.0 / (1.0 / 4700 + 1.0 / 7500 + 1.0 / 10000);
    for (int i = 0; i < samples; i++) {
        double x = (3.4 / 255 * base[(eng_pos >> 12) & 0x7ff] - 2) * volume;
        double i_total = 0;
        for (int k = 0; k < 3; k++) {
            double y = biquad_step(&eng_f[k], x);
            if (y > 1.5) y = 1.5; if (y < -2) y = -2;
            i_total += y / r_filt_out[k];
        }
        i_total *= r_filt_total / 2;
        int32_t v = buf[i] + (int32_t)(i_total * 20000.0);
        if (v > 32767) v = 32767; if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
        eng_pos += step;
    }
}

/* ---- 52XX speech samples ---- */
static int n52_start, n52_end, n52_length, n52_pos; static double n52_cycle, n52_step; static int n52_rate;
static biquad_t n52_hp, n52_lp;
void pp_n52_write(uint8_t d)
{
    d &= 0x0f;
    if (!d) return;
    n52_start = R->voice[d - 1] + (R->voice[d - 1 + 0x10] << 8);
    n52_end = R->voice[d] + (R->voice[d + 0x10] << 8);
    if (n52_end >= 0x6000) n52_end = 0x6000;
    n52_length = (n52_end - n52_start) * 2;
    n52_pos = 0; n52_cycle = 0;
}
static void n52_render(int16_t *buf, int samples, int rate)
{
    if (rate != n52_rate) {
        n52_rate = rate; n52_step = (1536000.0 / 384) / rate;
        biquad_setup(&n52_hp, 1, 100, 1.0 / 0.3, 1, rate);
        biquad_setup(&n52_lp, 0, 1200, 1.0 / 0.8, 0.5, rate);
    }
    if (n52_start >= n52_end) return;
    for (int i = 0; i < samples; i++) {
        n52_cycle += n52_step;
        if (n52_cycle >= 1) { int whole = (int)n52_cycle; n52_pos += whole; n52_cycle -= whole; }
        if (n52_pos > n52_length) { n52_start = n52_end = n52_length = n52_pos = 0; return; }
        int rom_pos = n52_start + (n52_pos >> 1);
        int s4 = (((n52_pos & 1) ? R->voice[rom_pos] >> 4 : R->voice[rom_pos]) & 0x0f) - 8;
        double y = biquad_step(&n52_lp, biquad_step(&n52_hp, (double)s4));
        int32_t v = buf[i] + (int32_t)(y * 0x0fff);
        if (v > 32767) v = 32767; if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

/* ---- 54XX noise (high level) ---- */
typedef struct { int active; float env, decay, lp, alpha, gain; } noise_t;
static noise_t nz[3]; static int n54_param_left; static int n54_pending[3]; static uint32_t rng = 0x12345678;
void pp_n54_write(uint8_t d)
{
    if (n54_param_left) { n54_param_left--; return; }
    switch (d >> 4) {
        case 1: n54_pending[0] = 1; break;
        case 2: n54_pending[1] = 1; break;
        case 3: case 4: n54_param_left = 4; break;
        case 5: n54_pending[2] = 1; break;
        case 6: n54_param_left = 5; break;
        case 7: nz[2].gain = (d & 0x0f) / 15.0f; break;
        default: break;
    }
}
static void trigger(int w, float decay_s, float cutoff, float gain, int rate)
{
    nz[w].active = 1; nz[w].env = 1.0f; nz[w].decay = 1.0f - 1.0f / (decay_s * rate);
    nz[w].alpha = 1.0f - (float)(1.0 / (1.0 + rate / (6.2832 * cutoff))); nz[w].gain = gain;
}
static void n54_render(int16_t *buf, int samples, int rate)
{
    if (n54_pending[0]) { trigger(0, 0.8f, 700.0f, 0.9f, rate); n54_pending[0] = 0; }
    if (n54_pending[1]) { trigger(1, 0.3f, 2500.0f, 0.6f, rate); n54_pending[1] = 0; }
    if (n54_pending[2]) { trigger(2, 1.5f, 1200.0f, nz[2].gain > 0 ? nz[2].gain : 0.5f, rate); n54_pending[2] = 0; }
    if (!nz[0].active && !nz[1].active && !nz[2].active) return;
    for (int i = 0; i < samples; i++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        float white = ((int32_t)(rng & 0xffff) - 32768) / 32768.0f, v = 0;
        for (int k = 0; k < 3; k++) {
            noise_t *n = &nz[k];
            if (!n->active) continue;
            n->lp += n->alpha * (white - n->lp);
            v += n->lp * n->env * n->gain;
            n->env *= n->decay;
            if (n->env < 0.002f) n->active = 0;
        }
        int32_t s = buf[i] + (int32_t)(v * 12000.0f);
        if (s > 32767) s = 32767; if (s < -32768) s = -32768;
        buf[i] = (int16_t)s;
    }
}

void pp_sound_init(const pp_roms_t *r) { R = r; pp_sound_reset(); }
void pp_sound_reset(void)
{
    memset(regs, 0, sizeof(regs)); memset(vcount, 0, sizeof(vcount)); sound_on = 0;
    eng_msb = eng_lsb = 0; eng_enable = 0; eng_pos = 0;
    n52_start = n52_end = n52_length = n52_pos = 0;
    memset(nz, 0, sizeof(nz)); n54_param_left = 0;
}
void pp_sound_render(int16_t *buf, int samples, int rate)
{
    memset(buf, 0, samples * sizeof(int16_t));
    wsg_render(buf, samples, rate);
    engine_render(buf, samples, rate);
    n52_render(buf, samples, rate);
    n54_render(buf, samples, rate);
}
