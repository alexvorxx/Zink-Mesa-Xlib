/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"
#include "si_build_pm4.h"
#include "sid.h"
#include "util/u_memory.h"

static void si_pm4_cmd_begin(struct si_pm4_state *state, unsigned opcode)
{
   if (!state->max_dw)
      state->max_dw = ARRAY_SIZE(state->pm4);
   assert(state->ndw < state->max_dw);
   assert(opcode <= 254);
   state->last_opcode = opcode;
   state->last_pm4 = state->ndw++;
}

void si_pm4_cmd_add(struct si_pm4_state *state, uint32_t dw)
{
   if (!state->max_dw)
      state->max_dw = ARRAY_SIZE(state->pm4);
   assert(state->ndw < state->max_dw);
   state->pm4[state->ndw++] = dw;
   state->last_opcode = 255; /* invalid opcode */
}

static void si_pm4_cmd_end(struct si_pm4_state *state, bool predicate)
{
   unsigned count;
   count = state->ndw - state->last_pm4 - 2;
   state->pm4[state->last_pm4] = PKT3(state->last_opcode, count, predicate);
}

static void si_pm4_set_reg_custom(struct si_pm4_state *state, unsigned reg, uint32_t val,
                                  unsigned opcode, unsigned idx)
{
   reg >>= 2;

   if (!state->max_dw)
      state->max_dw = ARRAY_SIZE(state->pm4);

   assert(state->ndw + 2 <= state->max_dw);

   if (opcode != state->last_opcode || reg != (state->last_reg + 1) || idx != state->last_idx) {
      si_pm4_cmd_begin(state, opcode);
      state->pm4[state->ndw++] = reg | (idx << 28);
   }

   assert(reg <= UINT16_MAX);
   state->last_reg = reg;
   state->last_idx = idx;
   state->pm4[state->ndw++] = val;
   si_pm4_cmd_end(state, false);
}

void si_pm4_set_reg(struct si_pm4_state *state, unsigned reg, uint32_t val)
{
   unsigned opcode;

   SI_CHECK_SHADOWED_REGS(reg, 1);

   if (reg >= SI_CONFIG_REG_OFFSET && reg < SI_CONFIG_REG_END) {
      opcode = PKT3_SET_CONFIG_REG;
      reg -= SI_CONFIG_REG_OFFSET;

   } else if (reg >= SI_SH_REG_OFFSET && reg < SI_SH_REG_END) {
      opcode = PKT3_SET_SH_REG;
      reg -= SI_SH_REG_OFFSET;

   } else if (reg >= SI_CONTEXT_REG_OFFSET && reg < SI_CONTEXT_REG_END) {
      opcode = PKT3_SET_CONTEXT_REG;
      reg -= SI_CONTEXT_REG_OFFSET;

   } else if (reg >= CIK_UCONFIG_REG_OFFSET && reg < CIK_UCONFIG_REG_END) {
      opcode = PKT3_SET_UCONFIG_REG;
      reg -= CIK_UCONFIG_REG_OFFSET;

   } else {
      PRINT_ERR("Invalid register offset %08x!\n", reg);
      return;
   }

   si_pm4_set_reg_custom(state, reg, val, opcode, 0);
}

void si_pm4_set_reg_idx3(struct si_screen *sscreen, struct si_pm4_state *state,
                         unsigned reg, uint32_t val)
{
   SI_CHECK_SHADOWED_REGS(reg, 1);

   if (sscreen->info.uses_kernel_cu_mask) {
      assert(sscreen->info.gfx_level >= GFX10);
      si_pm4_set_reg_custom(state, reg - SI_SH_REG_OFFSET, val, PKT3_SET_SH_REG_INDEX, 3);
   } else {
      si_pm4_set_reg_custom(state, reg - SI_SH_REG_OFFSET, val, PKT3_SET_SH_REG, 0);
   }
}

void si_pm4_set_reg_va(struct si_pm4_state *state, unsigned reg, uint32_t val)
{
   si_pm4_set_reg(state, reg, val);
   state->reg_va_low_idx = state->ndw - 1;
}

void si_pm4_clear_state(struct si_pm4_state *state)
{
   state->ndw = 0;
}

void si_pm4_free_state(struct si_context *sctx, struct si_pm4_state *state, unsigned idx)
{
   if (!state)
      return;

   if (idx != ~0) {
      if (sctx->emitted.array[idx] == state)
         sctx->emitted.array[idx] = NULL;

      if (sctx->queued.array[idx] == state) {
         sctx->queued.array[idx] = NULL;
         sctx->dirty_states &= ~BITFIELD_BIT(idx);
      }
   }

   FREE(state);
}

void si_pm4_emit(struct si_context *sctx, struct si_pm4_state *state)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   if (state->is_shader) {
      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, ((struct si_shader*)state)->bo,
                                RADEON_USAGE_READ | RADEON_PRIO_SHADER_BINARY);
   }

   radeon_begin(cs);
   radeon_emit_array(state->pm4, state->ndw);
   radeon_end();

   if (state->atom.emit)
      state->atom.emit(sctx);
}

void si_pm4_reset_emitted(struct si_context *sctx)
{
   memset(&sctx->emitted, 0, sizeof(sctx->emitted));

   for (unsigned i = 0; i < SI_NUM_STATES; i++) {
      if (sctx->queued.array[i])
         sctx->dirty_states |= BITFIELD_BIT(i);
   }
}

struct si_pm4_state *si_pm4_create_sized(unsigned max_dw)
{
   struct si_pm4_state *pm4;
   unsigned size = sizeof(*pm4) + 4 * (max_dw - ARRAY_SIZE(pm4->pm4));

   pm4 = (struct si_pm4_state *)calloc(1, size);
   if (pm4)
      pm4->max_dw = max_dw;
   return pm4;
}

struct si_pm4_state *si_pm4_clone(struct si_pm4_state *orig)
{
   struct si_pm4_state *pm4 = si_pm4_create_sized(orig->max_dw);

   if (pm4)
      memcpy(pm4, orig, sizeof(*pm4) + 4 * (pm4->max_dw - ARRAY_SIZE(pm4->pm4)));
   return pm4;
}
