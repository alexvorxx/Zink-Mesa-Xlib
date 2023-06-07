/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "agx_compiler.h"

/*
 * sample_mask takes two bitmasks as arguments, TARGET and LIVE. Each bit refers
 * to an indexed sample. Roughly, the instruction does:
 *
 *    foreach sample in TARGET {
 *       if sample in LIVE {
 *          mark sample live
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
 *    local_store_pixel instruction targeting that sample.
 *
 * 2. If sample_mask is used anywhere in a shader, the state of every sample
 *    must be set on all execution paths. That is, the union of the TARGET sets
 *    of executed sample_masks must contain all samples.
 *
 * 3. If a sample is killed, future sample_mask instructions will have
 *    no effect. The following code sequence correctly implements a conditional
 *    discard (if there are no other sample_mask instructions in the shader):
 *
 *       sample_mask discarded, 0
 *       sample_mask ~0, ~0
 *
 * 4. If zs_emit is used anywhere in the shader, sample_mask must not be used.
 * Instead, zs_emit with depth = NaN can be emitted.
 *
 * This pass legalizes sample_mask instructions to satisfy these rules.
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

   if (intr->intrinsic != nir_intrinsic_sample_mask_agx)
      return false;

   nir_ssa_def *target = intr->src[0].ssa;
   nir_ssa_def *live = intr->src[1].ssa;
   nir_ssa_def *discard = nir_iand(b, target, nir_inot(b, live));

   /* Write a NaN depth value for discarded samples */
   nir_store_zs_agx(b, discard, nir_imm_float(b, NAN),
                    stencil_written ? nir_imm_intN_t(b, 0, 16)
                                    : nir_ssa_undef(b, 1, 16) /* stencil */,
                    .base = BASE_Z | (stencil_written ? BASE_S : 0));

   nir_instr_remove(instr);
   return true;
}

bool
agx_nir_lower_sample_mask(nir_shader *shader, unsigned nr_samples)
{
   if (!(shader->info.outputs_written &
         (BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK))))
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

   /* nir_lower_io_to_temporaries ensures that stores are in the last block */
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_block *block = nir_impl_last_block(impl);

   nir_builder b;
   nir_builder_init(&b, impl);

   /* Check which samples get a value written in the last block */
   uint8_t samples_set = 0;

   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      if (intr->intrinsic != nir_intrinsic_sample_mask_agx)
         continue;

      if (!nir_src_is_const(intr->src[0]))
         continue;

      samples_set |= nir_src_as_uint(intr->src[0]);
   }

   /* If all samples are set, we're good to go */
   if ((samples_set & BITFIELD_MASK(nr_samples)) == BITFIELD_MASK(nr_samples))
      return false;

   /* Otherwise, at least one sample is not set in the last block and hence may
    * not be set at all. Insert an instruction in the last block to ensure it
    * will be live.
    */
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

   nir_sample_mask_agx(&b, nir_imm_intN_t(&b, ALL_SAMPLES, 16),
                       nir_imm_intN_t(&b, ALL_SAMPLES, 16));
   return true;
}
