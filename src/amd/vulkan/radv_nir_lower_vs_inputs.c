/*
 * Copyright © 2023 Valve Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "ac_nir.h"
#include "nir.h"
#include "nir_builder.h"
#include "radv_constants.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "radv_shader_args.h"

typedef struct {
   const struct radv_shader_args *args;
   const struct radv_shader_info *info;
   const struct radv_pipeline_key *pl_key;
   uint32_t address32_hi;
} lower_vs_inputs_state;

static nir_ssa_def *
lower_load_vs_input_from_prolog(nir_builder *b, nir_intrinsic_instr *intrin,
                                lower_vs_inputs_state *s)
{
   nir_src *offset_src = nir_get_io_offset_src(intrin);
   assert(nir_src_is_const(*offset_src));

   const unsigned base = nir_intrinsic_base(intrin);
   const unsigned base_offset = nir_src_as_uint(*offset_src);
   const unsigned driver_location = base + base_offset - VERT_ATTRIB_GENERIC0;
   const unsigned component = nir_intrinsic_component(intrin);
   const unsigned bit_size = intrin->dest.ssa.bit_size;
   const unsigned num_components = intrin->dest.ssa.num_components;

   /* 64-bit inputs: they occupy twice as many 32-bit components.
    * 16-bit inputs: they occupy a 32-bit component (not packed).
    */
   const unsigned arg_bit_size = MAX2(bit_size, 32);

   unsigned num_input_args = 1;
   nir_ssa_def *input_args[2] = {
      ac_nir_load_arg(b, &s->args->ac, s->args->vs_inputs[driver_location]), NULL};
   if (component * 32 + arg_bit_size * num_components > 128) {
      assert(bit_size == 64);

      num_input_args++;
      input_args[1] = ac_nir_load_arg(b, &s->args->ac, s->args->vs_inputs[driver_location + 1]);
   }

   nir_ssa_def *extracted =
      nir_extract_bits(b, input_args, num_input_args, component * 32, num_components, arg_bit_size);

   if (bit_size < arg_bit_size) {
      assert(bit_size == 16);

      if (nir_alu_type_get_base_type(nir_intrinsic_dest_type(intrin)) == nir_type_float)
         return nir_f2f16(b, extracted);
      else
         return nir_u2u16(b, extracted);
   }

   return extracted;
}

static bool
lower_vs_input_instr(nir_builder *b, nir_instr *instr, void *state)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != nir_intrinsic_load_input)
      return false;

   lower_vs_inputs_state *s = (lower_vs_inputs_state *)state;

   b->cursor = nir_before_instr(instr);

   nir_ssa_def *replacement = NULL;

   if (s->info->vs.dynamic_inputs) {
      replacement = lower_load_vs_input_from_prolog(b, intrin, s);
   } else {
      /* TODO: lower non-dynamic inputs */
      return false;
   }

   nir_ssa_def_rewrite_uses(&intrin->dest.ssa, replacement);
   nir_instr_remove(instr);
   nir_instr_free(instr);

   return true;
}

bool
radv_nir_lower_vs_inputs(nir_shader *shader, const struct radv_pipeline_stage *vs_stage,
                         const struct radv_pipeline_key *pl_key, uint32_t address32_hi)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX);

   lower_vs_inputs_state state = {
      .info = &vs_stage->info,
      .args = &vs_stage->args,
      .pl_key = pl_key,
      .address32_hi = address32_hi,
   };

   return nir_shader_instructions_pass(shader, lower_vs_input_instr,
                                       nir_metadata_dominance | nir_metadata_block_index, &state);
}
