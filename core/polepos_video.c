/*
 * polepos_video.c - background, road, sprites, text layer; per MAME polepos_v.cpp
 */
#include "polepos_internal.h"
#include <string.h>

static const pp_roms_t *R;
static uint16_t palette[PP_COLORS];
static uint16_t vpos_mod[256];
#define P (R->proms)

static inline uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
static inline int prom_rgb(uint8_t v) { return 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1); }

void pp_video_init(const pp_roms_t *r)
{
    R = r;
    for (int i = 0; i < 128; i++)
        palette[i] = rgb565(prom_rgb(P[0x000 + i]), prom_rgb(P[0x100 + i]), prom_rgb(P[0x200 + i]));
    for (int i = 0; i < 256; i++)
        vpos_mod[i] = (uint16_t)(P[0x500 + i] + (P[0x600 + i] << 4) + (P[0x700 + i] << 8));
}
const uint16_t *pp_palette(void) { return palette; }

/* fb row for a native scanline, or -1 if not visible */
static inline int fb_row(int y) { return (y >= 16 && y < 240) ? y - 16 : -1; }

static void draw_background(uint8_t *fb)
{
    /* 64x16 tiles, column-major, scrolled by hscroll, only the top half of the screen */
    for (int y = 16; y < 128; y++) {
        uint8_t *row = fb + (y - 16) * PP_FB_W;
        int trow = y >> 3, yin = y & 7;
        for (int x = 0; x < PP_FB_W; x++) {
            int sx = (x + pp_hscroll) & 0x1ff;
            uint16_t w = pp_view16[((sx >> 3) << 4) + trow];
            int code = (w & 0xff) | ((w & 0x4000) >> 6);
            int color = (w >> 8) & 0x3f;
            uint8_t pix = R->tiles[code * 64 + yin * 8 + (sx & 7)];
            row[x] = P[0x400 + color * 4 + pix] & 0x0f;
        }
    }
}

static void draw_road(uint8_t *fb)
{
    const uint8_t *road_bits1 = R->road + 0x2000, *road_bits2 = R->road + 0x4000;
    for (int y = 128; y < 256; y++) {
        int fy = fb_row(y);
        if (fy < 0) continue;
        uint8_t scan[256 + 8];
        uint8_t *dest = scan;
        int yoffs = ((vpos_mod[y] + pp_road_vscroll) >> 3) & 0x1ff;
        int roadpal = pp_road16[yoffs] & 15;
        int pen_base = roadpal << 6;
        int xoffs = pp_road16[0x380 + (y & 0x7f)] & 0x3ff;
        int xscroll = xoffs & 7;
        xoffs &= ~7;
        for (int x = 0; x < 256 / 8 + 1; x++, xoffs += 8) {
            if (xoffs & 0x200) {
                for (int i = 0; i < 8; i++) *dest++ = (uint8_t)(pen_base | 0);
            } else {
                int romoffs = ((y & 0x07f) << 6) + ((xoffs & 0x1f8) >> 3);
                int control = R->road[romoffs];
                int bits1 = road_bits1[romoffs];
                int bits2 = road_bits2[(romoffs & 0xfff) | ((romoffs & 0x1000) >> 1)];
                int roadval = control & 0x3f;
                int carin = control >> 7;
                for (int i = 8; i > 0; i--) {
                    int bits = ((bits1 >> i) & 1) + (((bits2 >> i) & 1) << 1);
                    if (!carin && bits) bits++;
                    *dest++ = (uint8_t)(pen_base | (roadval & 0x3f));
                    roadval += bits;
                }
            }
        }
        uint8_t *row = fb + fy * PP_FB_W;
        for (int x = 0; x < 256; x++) row[x] = (uint8_t)(0x40 + (P[0x800 + scan[x + xscroll]] & 0x0f));
    }
}

static void zoom_sprite(uint8_t *fb, int big, int code, int color, int flipx, int sx, int sy, int sizex, int sizey)
{
    const uint8_t *gfxdata = big ? R->bigsprites + (code & 0x7f) * 1024 : R->sprites + (code & 0x7f) * 256;
    int rowbytes = big ? 32 : 16;
    const uint8_t *lut = P + 0xc00 + (color & 0x3f) * 16;
    uint8_t bank = (color & 0x40) ? 0x50 : 0x10;
    int offsxor = flipx ? (big ? 0x1f : 0x0f) : 0;
    for (int y = 0; y <= sizey; y++) {
        int yy = (sy + y) & 0x1ff;
        if (yy >= 0x10 && yy < 0xf0) {
            int dy = R->scalelut[(y << 6) + sizey] & 0x1f;
            int xx = sx & 0x3ff, siz = 0, offs = 0;
            if (!big) dy >>= 1;
            const uint8_t *src = gfxdata + dy * rowbytes;
            uint8_t *row = fb + (yy - 16) * PP_FB_W;
            for (int x = (big ? 0x40 : 0x20); x > 0; x--) {
                if (xx < 0x100) {
                    int pen = src[(offs / 2) ^ offsxor];
                    uint8_t v = lut[pen] & 0x0f;
                    if (v != 15) row[xx] = (uint8_t)(bank + v);
                }
                offs++;
                siz = siz + 1 + sizex;
                if (siz & 0x40) { siz &= 0x3f; xx = (xx + 1) & 0x3ff; }
            }
        }
    }
}

static void draw_sprites(uint8_t *fb)
{
    const uint16_t *posmem = &pp_sprite16[0x380], *sizmem = &pp_sprite16[0x780];
    for (int i = 0; i < 64; i++, posmem += 2, sizmem += 2) {
        int sx = (posmem[1] & 0x3ff) - 0x40 + 4;
        int sy = 512 - (posmem[0] & 0x1ff) + 1;
        int sizex = (sizmem[1] & 0x3f00) >> 8;
        int sizey = (sizmem[0] & 0x3f00) >> 8;
        int code = sizmem[0] & 0x7f;
        int flipx = (sizmem[0] >> 7) & 1;
        int color = sizmem[1] & 0x3f;
        if (sy >= 128) color |= 0x40;
        zoom_sprite(fb, (sizmem[0] >> 15) & 1, code, color, flipx, sx, sy, sizex, sizey);
    }
}

static void draw_text(uint8_t *fb)
{
    for (int trow = 2; trow < 30; trow++) {            /* native rows 16..239 */
        for (int tcol = 0; tcol < 32; tcol++) {
            int idx = trow * 32 + tcol;
            uint16_t w = pp_alpha16[idx];
            int code = (w & 0xff) | ((w & 0x4000) >> 6);
            int color = (w >> 8) & 0x3f;
            if (!pp_chacl) { code &= 0xff; color = 0; }
            const uint8_t *lut = P + 0x300 + color * 4;
            uint8_t bank = (idx >= 32 * 16) ? 0x60 : 0x20;
            const uint8_t *src = R->chars + code * 64;
            uint8_t *dst = fb + (trow * 8 - 16) * PP_FB_W + tcol * 8;
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    uint8_t v = lut[src[y * 8 + x]] & 0x0f;
                    if (v != 15) dst[x] = (uint8_t)(bank + v);
                }
                dst += PP_FB_W;
            }
        }
    }
}

void pp_video_render(uint8_t *fb)
{
    memset(fb, 0, PP_FB_W * PP_FB_H);
    draw_background(fb);
    draw_road(fb);
    draw_sprites(fb);
    draw_text(fb);
}
