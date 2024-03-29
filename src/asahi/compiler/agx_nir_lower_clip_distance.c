/*
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "agx_nir.h"

static bool
lower(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (sem.location != VARYING_SLOT_CLIP_DIST0)
      return false;

   nir_instr *clone = nir_instr_clone(b->shader, &intr->instr);
   nir_intrinsic_instr *lowered = nir_instr_as_intrinsic(clone);

   b->cursor = nir_after_instr(&intr->instr);
   nir_builder_instr_insert(b, clone);

   nir_io_semantics new_sem = sem;
   new_sem.no_varying = true;
   nir_intrinsic_set_io_semantics(lowered, new_sem);

   sem.no_sysval_output = true;
   nir_intrinsic_set_io_semantics(intr, sem);
   return true;
}

bool
agx_nir_lower_clip_distance(nir_shader *s)
{
   assert(s->info.outputs_written & VARYING_BIT_CLIP_DIST0);

   return nir_shader_intrinsics_pass(
      s, lower, nir_metadata_block_index | nir_metadata_dominance, NULL);
}
