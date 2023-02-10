/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "util/bitset.h"
#include "util/u_dynarray.h"
#include "agx_state.h"

/*
 * Lower all system values to uniform loads. This pass tries to compact ranges
 * of contiguous uploaded uniforms to reduce the draw-time overhead of uploading
 * many tiny ranges. To do so, it works in 3 steps:
 *
 * 1. Walk the NIR, converting system values to placeholder load_preambles.
 * 2. Walk the ranges of uniforms needed, compacting into contiguous ranges.
 * 3. Fill in the load_preamble instructions with the real uniforms.
 */
struct state {
   /* Array of load_preamble nir_intrinsic_instr's to fix up at the end */
   struct util_dynarray load_preambles;

   /* Bitset of 16-bit uniforms pushed */
   BITSET_DECLARE(pushed, sizeof(struct agx_draw_uniforms) / 2);

   /* Element size in 16-bit units, so we may split ranges of different sizes
    * to guarantee natural alignment.
    */
   uint8_t element_size[sizeof(struct agx_draw_uniforms) / 2];
};

static bool
pass(struct nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   b->cursor = nir_before_instr(instr);
   struct state *state = data;

   /* For offsetof with dynamic array elements */
   struct agx_draw_uniforms *u = NULL;
   void *ptr = NULL;

   switch (intr->intrinsic) {
   case nir_intrinsic_load_vbo_base_agx:
      ptr = &u->vs.vbo_base[nir_src_as_uint(intr->src[0])];
      break;
   case nir_intrinsic_load_ubo_base_agx:
      ptr = &u->ubo_base[nir_src_as_uint(intr->src[0])];
      break;
   case nir_intrinsic_load_texture_base_agx:
      ptr = &u->texture_base;
      break;
   case nir_intrinsic_load_blend_const_color_r_float:
      ptr = &u->fs.blend_constant[0];
      break;
   case nir_intrinsic_load_blend_const_color_g_float:
      ptr = &u->fs.blend_constant[1];
      break;
   case nir_intrinsic_load_blend_const_color_b_float:
      ptr = &u->fs.blend_constant[2];
      break;
   case nir_intrinsic_load_blend_const_color_a_float:
      ptr = &u->fs.blend_constant[3];
      break;
   case nir_intrinsic_load_ssbo_address:
      ptr = &u->ssbo_base[nir_src_as_uint(intr->src[0])];
      break;
   case nir_intrinsic_get_ssbo_size:
      ptr = &u->ssbo_size[nir_src_as_uint(intr->src[0])];
      break;
   default:
      return false;
   }

   assert(nir_dest_bit_size(intr->dest) >= 16 && "no 8-bit sysvals");

   unsigned dim = nir_dest_num_components(intr->dest);
   unsigned element_size = nir_dest_bit_size(intr->dest) / 16;
   unsigned length = dim * element_size;

   unsigned offset = (uintptr_t)ptr;
   assert((offset % 2) == 0 && "all entries are aligned by ABI");

   nir_ssa_def *value =
      nir_load_preamble(b, dim, nir_dest_bit_size(intr->dest), .base = offset);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, value);

   BITSET_SET_RANGE(state->pushed, (offset / 2), (offset / 2) + length - 1);

   for (unsigned i = 0; i < length; ++i) {
      if (state->element_size[(offset / 2) + i])
         assert((state->element_size[(offset / 2) + i]) == element_size);
      else
         state->element_size[(offset / 2) + i] = element_size;
   }

   util_dynarray_append(&state->load_preambles, nir_intrinsic_instr *,
                        nir_instr_as_intrinsic(value->parent_instr));
   return true;
}

static struct agx_push_range *
find_push_range_containing(struct agx_compiled_shader *shader, unsigned offset)
{
   for (unsigned i = 0; i < shader->push_range_count; ++i) {
      struct agx_push_range *range = &shader->push[i];

      /* range->length is 16-bit words, need to convert. offset is bytes. */
      unsigned length_B = range->length * 2;

      if (range->offset <= offset && offset < (range->offset + length_B))
         return range;
   }

   unreachable("no containing range");
}

static unsigned
lay_out_uniforms(struct agx_compiled_shader *shader, struct state *state)
{
   unsigned uniform = 0;

   unsigned start, end;
   BITSET_FOREACH_RANGE(start, end, state->pushed, sizeof(state->pushed) * 8) {
      unsigned range_start = start;

      do {
         uint8_t size = state->element_size[range_start];

         /* Find a range of constant element size. [range_start, range_end).
          * Ranges may be at most 64 halfs.
          */
         unsigned range_end;
         for (range_end = range_start + 1;
              range_end < end && state->element_size[range_end] == size &&
              range_end < range_start + 64;
              ++range_end)
            ;

         /* Now make the range with the given size (naturally aligned) */
         uniform = ALIGN_POT(uniform, size);

         assert((shader->push_range_count < ARRAY_SIZE(shader->push)) &&
                "AGX_MAX_PUSH_RANGES must be an upper bound");

         /* Offsets must be aligned to 4 bytes, this may require pushing a
          * little more than intended (otherwise we would need extra copies)
          */
         range_start = ROUND_DOWN_TO(range_start, 4 / 2);

         shader->push[shader->push_range_count++] = (struct agx_push_range){
            .uniform = uniform,
            .offset = range_start * 2 /* bytes, not elements */,
            .length = (range_end - range_start),
         };

         uniform += (range_end - range_start);
         range_start = range_end;
      } while (range_start < end);
   }

   util_dynarray_foreach(&state->load_preambles, nir_intrinsic_instr *, intr) {
      unsigned offset = nir_intrinsic_base(*intr);
      struct agx_push_range *range = find_push_range_containing(shader, offset);

      nir_intrinsic_set_base(*intr,
                             range->uniform + ((offset - range->offset) / 2));
   }

   return uniform;
}

bool
agx_nir_lower_sysvals(nir_shader *shader, struct agx_compiled_shader *compiled,
                      unsigned *push_size)
{
   struct state state = {0};

   bool progress = nir_shader_instructions_pass(
      shader, pass, nir_metadata_block_index | nir_metadata_dominance, &state);

   if (progress) {
      *push_size = lay_out_uniforms(compiled, &state);
   } else {
      *push_size = 0;
   }

   return progress;
}
