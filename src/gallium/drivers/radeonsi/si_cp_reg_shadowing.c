/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_build_pm4.h"
#include "ac_debug.h"
#include "ac_shadowed_regs.h"
#include "util/u_memory.h"

static void si_set_context_reg_array(struct radeon_cmdbuf *cs, unsigned reg, unsigned num,
                                     const uint32_t *values)
{
   radeon_begin(cs);
   radeon_set_context_reg_seq(reg, num);
   radeon_emit_array(values, num);
   radeon_end();
}

void si_init_cp_reg_shadowing(struct si_context *sctx)
{
   if (sctx->has_graphics &&
       (sctx->screen->info.mid_command_buffer_preemption_enabled ||
        sctx->screen->debug_flags & DBG(SHADOW_REGS))) {
      if (sctx->screen->info.has_fw_based_shadowing) {
         sctx->shadowing.registers =
               si_aligned_buffer_create(sctx->b.screen,
                                        PIPE_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_DRIVER_INTERNAL,
                                        PIPE_USAGE_DEFAULT,
                                        sctx->screen->info.fw_based_mcbp.shadow_size,
                                        sctx->screen->info.fw_based_mcbp.shadow_alignment);
         sctx->shadowing.csa =
               si_aligned_buffer_create(sctx->b.screen,
                                        PIPE_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_DRIVER_INTERNAL,
                                        PIPE_USAGE_DEFAULT,
                                        sctx->screen->info.fw_based_mcbp.csa_size,
                                        sctx->screen->info.fw_based_mcbp.csa_alignment);
         if (!sctx->shadowing.registers || !sctx->shadowing.csa)
            fprintf(stderr, "radeonsi: cannot create register shadowing buffer(s)\n");
         else
            sctx->ws->cs_set_mcbp_reg_shadowing_va(&sctx->gfx_cs,
                                                   sctx->shadowing.registers->gpu_address,
                                                   sctx->shadowing.csa->gpu_address);
      } else {
         sctx->shadowing.registers =
               si_aligned_buffer_create(sctx->b.screen,
                                        PIPE_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_DRIVER_INTERNAL,
                                        PIPE_USAGE_DEFAULT,
                                        SI_SHADOWED_REG_BUFFER_SIZE,
                                        4096);
         if (!sctx->shadowing.registers)
            fprintf(stderr, "radeonsi: cannot create a shadowed_regs buffer\n");
      }
   }

   si_init_gfx_preamble_state(sctx, sctx->shadowing.registers != NULL);

   if (sctx->shadowing.registers) {
      /* We need to clear the shadowed reg buffer. */
      si_cp_dma_clear_buffer(sctx, &sctx->gfx_cs, &sctx->shadowing.registers->b.b,
                             0, sctx->shadowing.registers->bo_size, 0, SI_OP_SYNC_AFTER,
                             SI_COHERENCY_CP, L2_BYPASS);

      /* Create the shadowing preamble. */
      struct si_shadow_preamble {
         struct si_pm4_state pm4;
         uint32_t more_pm4[150]; /* Add more space because the command buffer is large. */
      };
      struct si_pm4_state *shadowing_preamble = (struct si_pm4_state *)CALLOC_STRUCT(si_shadow_preamble);

      /* Add all the space that we allocated. */
      shadowing_preamble->max_dw = (sizeof(struct si_shadow_preamble) -
                                    offsetof(struct si_shadow_preamble, pm4.pm4)) / 4;

      ac_create_shadowing_ib_preamble(&sctx->screen->info,
                                      (pm4_cmd_add_fn)si_pm4_cmd_add, shadowing_preamble,
                                      sctx->shadowing.registers->gpu_address, sctx->screen->dpbb_allowed);

      /* Initialize shadowed registers as follows. */
      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, sctx->shadowing.registers,
                                RADEON_USAGE_READWRITE | RADEON_PRIO_DESCRIPTORS);
      if (sctx->shadowing.csa)
         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, sctx->shadowing.csa,
                                   RADEON_USAGE_READWRITE | RADEON_PRIO_DESCRIPTORS);
      si_pm4_emit(sctx, shadowing_preamble);
      ac_emulate_clear_state(&sctx->screen->info, &sctx->gfx_cs, si_set_context_reg_array);
      si_pm4_emit(sctx, sctx->cs_preamble_state);

      /* The register values are shadowed, so we won't need to set them again. */
      si_pm4_free_state(sctx, sctx->cs_preamble_state, ~0);
      sctx->cs_preamble_state = NULL;

      si_set_tracked_regs_to_clear_state(sctx);

      /* Setup preemption. The shadowing preamble will be executed as a preamble IB,
       * which will load register values from memory on a context switch.
       */
      sctx->ws->cs_setup_preemption(&sctx->gfx_cs, shadowing_preamble->pm4,
                                    shadowing_preamble->ndw);
      si_pm4_free_state(sctx, shadowing_preamble, ~0);
   }
}
