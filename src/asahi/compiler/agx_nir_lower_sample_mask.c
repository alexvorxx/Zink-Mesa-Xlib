/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "agx_compiler.h"
#include "nir_intrinsics.h"

/*
 * sample_mask takes two bitmasks as arguments, TARGET and LIVE. Each bit refers
 * to an indexed sample. Roughly, the instruction does:
 *
 *    foreach sample in TARGET {
 *       if sample in LIVE {
 *          run depth/stencil test and update
 *       } else {
 *          kill sample
 *       }
 *    }
 *
 * As a special case, TARGET may be set to all-1s (~0) to refer to all samples
 * regardless of the framebuffer sample count.
 *
 * For example, to discard an entire pixel unconditionally, we could run:
 *
 *    sample_mask ~0, 0
 *
 * sample_mask must follow these rules:
 *
 * 1. All sample_mask instructions affecting a sample must execute before a
 *    local_store_pixel instruction targeting that sample. This ensures that
 *    nothing is written for discarded samples (whether discarded in shader or
 *    due to a failed depth/stencil test).
 *
 * 2. If sample_mask is used anywhere in a shader, then on every execution path,
 *    every sample must be killed or else run depth/stencil tests exactly ONCE.
 *
 * 3. If a sample is killed, future sample_mask instructions have
 *    no effect on that sample. The following code sequence correctly implements
 *    a conditional discard (if there are no other sample_mask instructions in
 *    the shader):
 *
 *       sample_mask discarded, 0
 *       sample_mask ~0, ~0
 *
 *    but this sequence is incorrect:
 *
 *       sample_mask ~0, ~discarded
 *       sample_mask ~0, ~0         <-- incorrect: depth/stencil tests run twice
 *
 * 4. If zs_emit is used anywhere in the shader, sample_mask must not be used.
 * Instead, zs_emit with depth = NaN can be emitted.
 *
 * This pass lowers discard_agx to sample_mask instructions satisfying these
 * rules. Other passes should not generate sample_mask instructions, as there
 * are too many footguns.
 */

#define ALL_SAMPLES (0xFF)
#define BASE_Z      1
#define BASE_S      2

static bool
lower_sample_mask_to_zs(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   bool depth_written =
      b->shader->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH);
   bool stencil_written =
      b->shader->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL);

   b->cursor = nir_before_instr(instr);

   /* Existing zs_emit instructions need to be fixed up to write their own depth
    * for consistency.
    */
   if (intr->intrinsic == nir_intrinsic_store_zs_agx && !depth_written) {
      /* Load the current depth at this pixel */
      nir_ssa_def *z = nir_channel(b, nir_load_frag_coord(b), 2);

      /* Write it out from this store_zs */
      nir_intrinsic_set_base(intr, nir_intrinsic_base(intr) | BASE_Z);
      nir_instr_rewrite_src_ssa(instr, &intr->src[1], z);

      /* We'll set outputs_written after the pass in case there are multiple
       * store_zs_agx instructions needing fixup.
       */
      b->shader->info.fs.depth_layout = FRAG_DEPTH_LAYOUT_ANY;
      return true;
   }

   if (intr->intrinsic != nir_intrinsic_discard_agx)
      return false;

   /* Write a NaN depth value for discarded samples */
   nir_store_zs_agx(b, intr->src[0].ssa, nir_imm_float(b, NAN),
                    stencil_written ? nir_imm_intN_t(b, 0, 16)
                                    : nir_ssa_undef(b, 1, 16) /* stencil */,
                    .base = BASE_Z | (stencil_written ? BASE_S : 0));

   nir_instr_remove(instr);
   return true;
}

static bool
lower_discard_to_sample_mask_0(nir_builder *b, nir_instr *instr,
                               UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_discard_agx)
      return false;

   b->cursor = nir_before_instr(instr);
   nir_sample_mask_agx(b, intr->src[0].ssa, nir_imm_intN_t(b, 0, 16));
   nir_instr_remove(instr);
   return true;
}

bool
agx_nir_lower_sample_mask(nir_shader *shader, unsigned nr_samples)
{
   if (!shader->info.fs.uses_discard)
      return false;

   /* sample_mask can't be used with zs_emit, so lower sample_mask to zs_emit */
   if (shader->info.outputs_written & (BITFIELD64_BIT(FRAG_RESULT_DEPTH) |
                                       BITFIELD64_BIT(FRAG_RESULT_STENCIL))) {
      bool progress = nir_shader_instructions_pass(
         shader, lower_sample_mask_to_zs,
         nir_metadata_block_index | nir_metadata_dominance, NULL);

      /* The lowering requires an unconditional depth write. We mark this after
       * lowering so the lowering knows whether there was already a depth write
       */
      assert(progress && "must have lowered something,given the outputs");
      shader->info.outputs_written |= BITFIELD64_BIT(FRAG_RESULT_DEPTH);

      return true;
   }

   /* Pessimistic lowering: force late depth/stencil test. TODO: Optimize. */
   nir_shader_instructions_pass(
      shader, lower_discard_to_sample_mask_0,
      nir_metadata_block_index | nir_metadata_dominance, NULL);

   /* nir_lower_io_to_temporaries ensures that stores are in the last block. */
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_block *block = nir_impl_last_block(impl);

   nir_builder b;
   nir_builder_init(&b, impl);

   b.cursor = nir_after_block(block);
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      if (intr->intrinsic != nir_intrinsic_store_local_pixel_agx)
         continue;

      b.cursor = nir_before_instr(instr);
      break;
   }

   /* Run depth/stencil tests for all remaining samples */
   nir_sample_mask_agx(&b, nir_imm_intN_t(&b, ALL_SAMPLES, 16),
                       nir_imm_intN_t(&b, ALL_SAMPLES, 16));
   return true;
}
