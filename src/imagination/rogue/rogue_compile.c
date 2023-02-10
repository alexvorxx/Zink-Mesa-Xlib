/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "compiler/spirv/nir_spirv.h"
#include "nir/nir.h"
#include "rogue.h"
#include "rogue_builder.h"
#include "util/macros.h"

/**
 * \file rogue_compile.c
 *
 * \brief Contains NIR to Rogue translation functions, and Rogue passes.
 */

static void trans_nir_jump_return(rogue_builder *b, nir_jump_instr *jump)
{
   rogue_END(b);
}

static void trans_nir_jump(rogue_builder *b, nir_jump_instr *jump)
{
   switch (jump->type) {
   case nir_jump_return:
      return trans_nir_jump_return(b, jump);

   default:
      break;
   }

   unreachable("Unimplemented NIR jump instruction type.");
}

static void trans_nir_load_const(rogue_builder *b,
                                 nir_load_const_instr *load_const)
{
   rogue_reg *dst = rogue_ssa_reg(b->shader, load_const->def.index);
   if (load_const->def.bit_size == 32) {
      uint32_t imm = nir_const_value_as_uint(load_const->value[0], 32);
      rogue_MOV(b, rogue_ref_reg(dst), rogue_ref_imm(imm));
   } else {
      unreachable("Unimplemented NIR load_const bit size.");
   }
}

static void trans_nir_intrinsic_load_input_fs(rogue_builder *b,
                                              nir_intrinsic_instr *intr)
{
   struct rogue_fs_build_data *fs_data = &b->shader->ctx->stage_data.fs;

   unsigned load_size = nir_dest_num_components(intr->dest);
   assert(load_size == 1); /* TODO: We can support larger load sizes. */

   rogue_reg *dst = rogue_ssa_reg(b->shader, intr->dest.ssa.index);

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   unsigned component = nir_intrinsic_component(intr);
   unsigned coeff_index = rogue_coeff_index_fs(&fs_data->iterator_args,
                                               io_semantics.location,
                                               component);
   unsigned wcoeff_index = rogue_coeff_index_fs(&fs_data->iterator_args, ~0, 0);

   rogue_regarray *coeffs = rogue_coeff_regarray(b->shader,
                                                 ROGUE_COEFF_ALIGN * load_size,
                                                 coeff_index);
   rogue_regarray *wcoeffs =
      rogue_coeff_regarray(b->shader, ROGUE_COEFF_ALIGN, wcoeff_index);

   rogue_instr *instr = &rogue_FITRP_PIXEL(b,
                                           rogue_ref_reg(dst),
                                           rogue_ref_drc(0),
                                           rogue_ref_regarray(coeffs),
                                           rogue_ref_regarray(wcoeffs),
                                           rogue_ref_val(load_size))
                            ->instr;
   rogue_add_instr_comment(instr, "load_input_fs");
}

static void trans_nir_intrinsic_load_input_vs(rogue_builder *b,
                                              nir_intrinsic_instr *intr)
{
   ASSERTED unsigned load_size = nir_dest_num_components(intr->dest);
   assert(load_size == 1); /* TODO: We can support larger load sizes. */

   rogue_reg *dst = rogue_ssa_reg(b->shader, intr->dest.ssa.index);

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   unsigned component = nir_intrinsic_component(intr);
   /* TODO: Get these properly with the intrinsic index (ssa argument) */
   unsigned vtxin_index =
      ((io_semantics.location - VERT_ATTRIB_GENERIC0) * 3) + component;

   rogue_reg *src = rogue_vtxin_reg(b->shader, vtxin_index);
   rogue_instr *instr =
      &rogue_MOV(b, rogue_ref_reg(dst), rogue_ref_reg(src))->instr;
   rogue_add_instr_comment(instr, "load_input_vs");
}

static void trans_nir_intrinsic_load_input(rogue_builder *b,
                                           nir_intrinsic_instr *intr)
{
   switch (b->shader->stage) {
   case MESA_SHADER_FRAGMENT:
      return trans_nir_intrinsic_load_input_fs(b, intr);

   case MESA_SHADER_VERTEX:
      return trans_nir_intrinsic_load_input_vs(b, intr);

   default:
      break;
   }

   unreachable("Unimplemented NIR load_input variant.");
}

static void trans_nir_intrinsic_store_output_fs(rogue_builder *b,
                                                nir_intrinsic_instr *intr)
{
   ASSERTED unsigned store_size = nir_src_num_components(intr->src[0]);
   assert(store_size == 1);

   /* TODO: When hoisting I/O allocation to the driver, check if this is
    * correct.
    */
   unsigned pixout_index = nir_src_as_uint(intr->src[1]);

   rogue_reg *dst = rogue_pixout_reg(b->shader, pixout_index);
   rogue_reg *src = rogue_ssa_reg(b->shader, intr->src[0].ssa->index);

   rogue_instr *instr =
      &rogue_MOV(b, rogue_ref_reg(dst), rogue_ref_reg(src))->instr;
   rogue_add_instr_comment(instr, "store_output_fs");
}

static void trans_nir_intrinsic_store_output_vs(rogue_builder *b,
                                                nir_intrinsic_instr *intr)
{
   struct rogue_vs_build_data *vs_data = &b->shader->ctx->stage_data.vs;

   ASSERTED unsigned store_size = nir_src_num_components(intr->src[0]);
   assert(store_size == 1);

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   unsigned component = nir_intrinsic_component(intr);
   unsigned vtxout_index = rogue_output_index_vs(&vs_data->outputs,
                                                 io_semantics.location,
                                                 component);

   rogue_reg *dst = rogue_vtxout_reg(b->shader, vtxout_index);
   rogue_reg *src = rogue_ssa_reg(b->shader, intr->src[0].ssa->index);

   rogue_instr *instr =
      &rogue_MOV(b, rogue_ref_reg(dst), rogue_ref_reg(src))->instr;
   rogue_add_instr_comment(instr, "store_output_vs");
}

static void trans_nir_intrinsic_store_output(rogue_builder *b,
                                             nir_intrinsic_instr *intr)
{
   switch (b->shader->stage) {
   case MESA_SHADER_FRAGMENT:
      return trans_nir_intrinsic_store_output_fs(b, intr);

   case MESA_SHADER_VERTEX:
      return trans_nir_intrinsic_store_output_vs(b, intr);

   default:
      break;
   }

   unreachable("Unimplemented NIR store_output variant.");
}

static void trans_nir_intrinsic_load_ubo(rogue_builder *b,
                                         nir_intrinsic_instr *intr)
{
   struct rogue_ubo_data *ubo_data =
      &b->shader->ctx->common_data[b->shader->stage].ubo_data;

   unsigned desc_set = nir_src_comp_as_uint(intr->src[0], 0);
   unsigned binding = nir_src_comp_as_uint(intr->src[0], 1);
   unsigned offset = nir_intrinsic_range_base(intr);

   unsigned sh_index = rogue_ubo_reg(ubo_data, desc_set, binding, offset);

   rogue_reg *dst = rogue_ssa_reg(b->shader, intr->dest.ssa.index);
   rogue_reg *src = rogue_shared_reg(b->shader, sh_index);
   rogue_instr *instr =
      &rogue_MOV(b, rogue_ref_reg(dst), rogue_ref_reg(src))->instr;
   rogue_add_instr_comment(instr, "load_ubo");
}

static void trans_nir_intrinsic(rogue_builder *b, nir_intrinsic_instr *intr)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_input:
      return trans_nir_intrinsic_load_input(b, intr);

   case nir_intrinsic_store_output:
      return trans_nir_intrinsic_store_output(b, intr);

   case nir_intrinsic_load_ubo:
      return trans_nir_intrinsic_load_ubo(b, intr);

   default:
      break;
   }

   unreachable("Unimplemented NIR intrinsic instruction.");
}

static void trans_nir_alu_pack_unorm_4x8(rogue_builder *b, nir_alu_instr *alu)
{
   rogue_reg *dst = rogue_ssa_reg(b->shader, alu->dest.dest.ssa.index);
   rogue_regarray *src_array =
      rogue_ssa_vec_regarray(b->shader, 4, alu->src[0].src.ssa->index, 0);

   rogue_alu_instr *pck_u8888 =
      rogue_PCK_U8888(b, rogue_ref_reg(dst), rogue_ref_regarray(src_array));
   rogue_set_instr_repeat(&pck_u8888->instr, 4);
   rogue_set_alu_op_mod(pck_u8888, ROGUE_ALU_OP_MOD_SCALE);
}

static void trans_nir_alu_fmul(rogue_builder *b, nir_alu_instr *alu)
{
   rogue_reg *dst = rogue_ssa_reg(b->shader, alu->dest.dest.ssa.index);
   rogue_reg *src0 = rogue_ssa_reg(b->shader, alu->src[0].src.ssa->index);
   rogue_reg *src1 = rogue_ssa_reg(b->shader, alu->src[1].src.ssa->index);

   rogue_FMUL(b, rogue_ref_reg(dst), rogue_ref_reg(src0), rogue_ref_reg(src1));
}

static void trans_nir_alu_ffma(rogue_builder *b, nir_alu_instr *alu)
{
   rogue_reg *dst = rogue_ssa_reg(b->shader, alu->dest.dest.ssa.index);
   rogue_reg *src0 = rogue_ssa_reg(b->shader, alu->src[0].src.ssa->index);
   rogue_reg *src1 = rogue_ssa_reg(b->shader, alu->src[1].src.ssa->index);
   rogue_reg *src2 = rogue_ssa_reg(b->shader, alu->src[2].src.ssa->index);

   rogue_FMAD(b,
              rogue_ref_reg(dst),
              rogue_ref_reg(src0),
              rogue_ref_reg(src1),
              rogue_ref_reg(src2));
}

static void trans_nir_alu_vecN(rogue_builder *b, nir_alu_instr *alu, unsigned n)
{
   unsigned dst_index = alu->dest.dest.ssa.index;
   rogue_reg *dst;
   rogue_reg *src;

   for (unsigned u = 0; u < n; ++u) {
      dst = rogue_ssa_vec_reg(b->shader, dst_index, u);
      src = rogue_ssa_reg(b->shader, alu->src[u].src.ssa->index);
      rogue_MOV(b, rogue_ref_reg(dst), rogue_ref_reg(src));
   }
}

static void trans_nir_alu(rogue_builder *b, nir_alu_instr *alu)
{
   switch (alu->op) {
   case nir_op_pack_unorm_4x8:
      return trans_nir_alu_pack_unorm_4x8(b, alu);
      return;

   case nir_op_fmul:
      return trans_nir_alu_fmul(b, alu);

   case nir_op_ffma:
      return trans_nir_alu_ffma(b, alu);

   case nir_op_vec4:
      return trans_nir_alu_vecN(b, alu, 4);

   default:
      break;
   }

   unreachable("Unimplemented NIR ALU instruction.");
}

static inline void rogue_feedback_used_regs(rogue_build_ctx *ctx,
                                            const rogue_shader *shader)
{
   /* TODO NEXT: Use this counting method elsewhere as well. */
   ctx->common_data[shader->stage].temps =
      __bitset_count(shader->regs_used[ROGUE_REG_CLASS_TEMP],
                     BITSET_WORDS(rogue_reg_infos[ROGUE_REG_CLASS_TEMP].num));
   ctx->common_data[shader->stage].internals = __bitset_count(
      shader->regs_used[ROGUE_REG_CLASS_INTERNAL],
      BITSET_WORDS(rogue_reg_infos[ROGUE_REG_CLASS_INTERNAL].num));
}

/**
 * \brief Translates a NIR shader to Rogue.
 *
 * \param[in] ctx Shared multi-stage build context.
 * \param[in] nir NIR shader.
 * \return A rogue_shader* if successful, or NULL if unsuccessful.
 */
PUBLIC
rogue_shader *rogue_nir_to_rogue(rogue_build_ctx *ctx, const nir_shader *nir)
{
   gl_shader_stage stage = nir->info.stage;
   struct rogue_shader *shader = rogue_shader_create(ctx, stage);
   if (!shader)
      return NULL;

   shader->ctx = ctx;

   /* Make sure we only have a single function. */
   assert(exec_list_length(&nir->functions) == 1);

   rogue_builder b;
   rogue_builder_init(&b, shader);

   /* Translate shader entrypoint. */
   nir_function_impl *entry = nir_shader_get_entrypoint((nir_shader *)nir);
   nir_foreach_block (block, entry) {
      rogue_push_block(&b);

      nir_foreach_instr (instr, block) {
         switch (instr->type) {
         case nir_instr_type_alu:
            trans_nir_alu(&b, nir_instr_as_alu(instr));
            break;

         case nir_instr_type_intrinsic:
            trans_nir_intrinsic(&b, nir_instr_as_intrinsic(instr));
            break;

         case nir_instr_type_load_const:
            trans_nir_load_const(&b, nir_instr_as_load_const(instr));
            break;

         case nir_instr_type_jump:
            trans_nir_jump(&b, nir_instr_as_jump(instr));
            break;

         default:
            unreachable("Unimplemented NIR instruction type.");
         }
      }
   }

   /* Apply passes. */
   rogue_shader_passes(shader);

   rogue_feedback_used_regs(ctx, shader);

   return shader;
}

/**
 * \brief Performs Rogue passes on a shader.
 *
 * \param[in] shader The shader.
 */
PUBLIC
void rogue_shader_passes(rogue_shader *shader)
{
   rogue_validate_shader(shader, "before passes");

   if (ROGUE_DEBUG(IR_PASSES))
      rogue_print_pass_debug(shader, "before passes", stdout);

   /* Passes */
   ROGUE_PASS_V(shader, rogue_constreg);
   ROGUE_PASS_V(shader, rogue_copy_prop);
   ROGUE_PASS_V(shader, rogue_dce);
   ROGUE_PASS_V(shader, rogue_lower_pseudo_ops);
   ROGUE_PASS_V(shader, rogue_schedule_wdf, false);
   ROGUE_PASS_V(shader, rogue_schedule_uvsw, false);
   ROGUE_PASS_V(shader, rogue_trim);
   ROGUE_PASS_V(shader, rogue_regalloc);
   ROGUE_PASS_V(shader, rogue_dce);
   ROGUE_PASS_V(shader, rogue_schedule_instr_groups, false);

   if (ROGUE_DEBUG(IR))
      rogue_print_pass_debug(shader, "after passes", stdout);
}
