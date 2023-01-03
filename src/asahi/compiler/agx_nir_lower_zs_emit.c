/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "agx_compiler.h"

#define ALL_SAMPLES 0xFF
#define BASE_Z      1
#define BASE_S      2

static bool
lower(nir_function_impl *impl, nir_block *block)
{
   nir_intrinsic_instr *zs_emit = NULL;
   bool progress = false;

   nir_foreach_instr_reverse_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      if (intr->intrinsic != nir_intrinsic_store_output)
         continue;

      nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
      if (sem.location != FRAG_RESULT_DEPTH &&
          sem.location != FRAG_RESULT_STENCIL)
         continue;

      if (zs_emit == NULL) {
         nir_builder b;
         nir_builder_init(&b, impl);
         b.cursor = nir_before_instr(instr);

         /* Multisampling will get lowered later if needed, default to broadcast
          */
         nir_ssa_def *sample_mask = nir_imm_intN_t(&b, ALL_SAMPLES, 16);
         zs_emit = nir_store_zs_agx(&b, sample_mask,
                                    nir_ssa_undef(&b, 1, 32) /* depth */,
                                    nir_ssa_undef(&b, 1, 16) /* stencil */);
      }

      nir_ssa_def *value = intr->src[0].ssa;

      bool z = (sem.location == FRAG_RESULT_DEPTH);
      unsigned src_idx = z ? 1 : 2;
      unsigned base = z ? BASE_Z : BASE_S;

      assert((nir_intrinsic_base(zs_emit) & base) == 0 &&
             "each of depth/stencil may only be written once");

      nir_instr_rewrite_src_ssa(&zs_emit->instr, &zs_emit->src[src_idx], value);
      nir_intrinsic_set_base(zs_emit, nir_intrinsic_base(zs_emit) | base);

      nir_instr_remove(instr);
      progress = true;
   }

   return progress;
}

bool
agx_nir_lower_zs_emit(nir_shader *s)
{
   bool any_progress = false;

   nir_foreach_function(function, s) {
      if (!function->impl)
         continue;

      bool progress = false;

      nir_foreach_block(block, function->impl) {
         progress |= lower(function->impl, block);
      }

      if (progress) {
         nir_metadata_preserve(
            function->impl, nir_metadata_block_index | nir_metadata_dominance);
      } else {
         nir_metadata_preserve(function->impl, nir_metadata_all);
      }

      any_progress |= progress;
   }

   return any_progress;
}
