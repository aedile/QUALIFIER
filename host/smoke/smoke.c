/* Smoke test: run the Pole Position sub1 ROM on the Z8002 core with plain RAM. */
#include <stdio.h>
#include <string.h>
#include "z8002.h"
#include "polepos_roms.h"
static uint8_t ram[0x10000];
uint8_t z8k_rb(uint32_t a) { a &= 0xffff; return a < 0x8000 ? pp_rom_sub1[a] : ram[a]; }
uint16_t z8k_rw(uint32_t a) { return (uint16_t)((z8k_rb(a) << 8) | z8k_rb(a + 1)); }
void z8k_wb(uint32_t a, uint8_t d) { a &= 0xffff; if (a >= 0x8000) ram[a] = d; }
void z8k_ww(uint32_t a, uint16_t d) { z8k_wb(a, d >> 8); z8k_wb(a + 1, (uint8_t)d); }
uint8_t z8k_in(uint16_t p) { (void)p; return 0xff; }
void z8k_out(uint16_t p, uint8_t d) { (void)p; (void)d; }
int main(void)
{
    z8k_t cpu;
    z8k_init_tables();
    z8k_reset(&cpu);
    printf("reset vector: fcw %04X pc %04X\n", z8k_rw(2), z8k_rw(4));
    long total = 0; uint32_t last_inv = 0;
    for (int i = 0; i < 300; i++) {
        total += z8k_run(&cpu, 10000);
        if (i % 50 == 0 || z8k_invalid_count != last_inv) {
            printf("cycles %ld pc %04X fcw %04X halt %d invalid %u sp %04X\n", total, cpu.pc, cpu.fcw, cpu.halt, z8k_invalid_count, cpu.regs.W[15 ^ 3]);
            last_inv = z8k_invalid_count;
        }
        if (cpu.halt) { printf("HALTED at pc %04X after %ld cycles\n", cpu.pc, total); break; }
    }
    printf("done: pc %04X invalid opcodes %u\n", cpu.pc, z8k_invalid_count);
    return 0;
}
