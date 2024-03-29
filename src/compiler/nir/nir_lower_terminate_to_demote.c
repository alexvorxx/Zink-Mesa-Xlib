/*
 * Copyright © 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

static bool
nir_lower_terminate_block(nir_builder *b, nir_block *block)
{
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_terminate: {
         /* Everything after the terminate is dead */
         nir_cf_list dead_cf;
         nir_cf_extract(&dead_cf, nir_after_instr(&intrin->instr),
                                  nir_after_block(block));
         nir_cf_delete(&dead_cf);

         intrin->intrinsic = nir_intrinsic_demote;
         b->cursor = nir_after_instr(&intrin->instr);
         nir_jump(b, nir_jump_halt);

         /* We just removed the remainder of this block.  It's not safe to
          * continue iterating instructions.
          */
         return true;
      }

      case nir_intrinsic_terminate_if:
         b->cursor = nir_before_instr(&intrin->instr);
         nir_push_if(b, intrin->src[0].ssa);
         {
            nir_demote(b);
            nir_jump(b, nir_jump_halt);
         }
         nir_instr_remove(&intrin->instr);
         progress = true;
         break;

      default:
         break;
      }
   }

   return progress;
}

static bool
nir_lower_terminate_impl(nir_function_impl *impl)
{
   bool progress = false;

   nir_builder b = nir_builder_create(impl);

   nir_foreach_block_safe(block, impl)
      progress |= nir_lower_terminate_block(&b, block);

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_none);
   } else {
      nir_metadata_preserve(impl, nir_metadata_all);
   }

   return progress;
}

/** Lowers nir_intrinsic_terminate to demote + halt
 *
 * The semantics of nir_intrinsic_terminate require that threads immediately
 * exit.  In SPIR-V, terminate is branch instruction even though it's only an
 * intrinsic in NIR.  This pass lowers terminate to demote + halt.  Since halt
 * is a jump instruction in NIR, this restores those semantics and NIR can
 * reason about dead threads after a halt.  It allows lets back-ends to only
 * implement nir_intrinsic_demote as long as they also implement nir_jump_halt.
 */
bool
nir_lower_terminate_to_demote(nir_shader *nir)
{
   bool progress = false;

   nir_foreach_function_impl(impl, nir) {
      if (nir_lower_terminate_impl(impl))
         progress = true;
   }

   return progress;
}
