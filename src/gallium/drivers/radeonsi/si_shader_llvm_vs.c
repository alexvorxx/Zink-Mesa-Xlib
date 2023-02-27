/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "si_pipe.h"
#include "si_shader_internal.h"
#include "sid.h"
#include "util/u_memory.h"
#include "ac_nir.h"

static LLVMValueRef unpack_sint16(struct si_shader_context *ctx, LLVMValueRef i32, unsigned index)
{
   assert(index <= 1);

   if (index == 1)
      return LLVMBuildAShr(ctx->ac.builder, i32, LLVMConstInt(ctx->ac.i32, 16, 0), "");

   return LLVMBuildSExt(ctx->ac.builder, LLVMBuildTrunc(ctx->ac.builder, i32, ctx->ac.i16, ""),
                        ctx->ac.i32, "");
}

static LLVMValueRef get_vertex_index(struct si_shader_context *ctx,
                                     struct si_vs_prolog_bits *key, unsigned input_index,
                                     LLVMValueRef instance_divisor_constbuf,
                                     unsigned start_instance, unsigned base_vertex)
{
   LLVMValueRef instance_id = ctx->abi.instance_id_replaced ?
      ctx->abi.instance_id_replaced : ctx->abi.instance_id;
   LLVMValueRef vertex_id = ctx->abi.vertex_id_replaced ?
      ctx->abi.vertex_id_replaced : ctx->abi.vertex_id;

   bool divisor_is_one = key->instance_divisor_is_one & (1u << input_index);
   bool divisor_is_fetched =key->instance_divisor_is_fetched & (1u << input_index);

   LLVMValueRef index = NULL;
   if (divisor_is_one)
      index = instance_id;
   else if (divisor_is_fetched) {
      LLVMValueRef udiv_factors[4];

      for (unsigned j = 0; j < 4; j++) {
         udiv_factors[j] = si_buffer_load_const(
            ctx, instance_divisor_constbuf,
            LLVMConstInt(ctx->ac.i32, input_index * 16 + j * 4, 0));
         udiv_factors[j] = ac_to_integer(&ctx->ac, udiv_factors[j]);
      }

      /* The faster NUW version doesn't work when InstanceID == UINT_MAX.
       * Such InstanceID might not be achievable in a reasonable time though.
       */
      index = ac_build_fast_udiv_nuw(
         &ctx->ac, instance_id, udiv_factors[0],
         udiv_factors[1], udiv_factors[2], udiv_factors[3]);
   }

   if (divisor_is_one || divisor_is_fetched) {
      /* Add StartInstance. */
      index = LLVMBuildAdd(ctx->ac.builder, index,
                           LLVMGetParam(ctx->main_fn.value, start_instance), "");
   } else {
      /* VertexID + BaseVertex */
      index = LLVMBuildAdd(ctx->ac.builder, vertex_id,
                           LLVMGetParam(ctx->main_fn.value, base_vertex), "");
   }

   return index;
}

static void load_input_vs(struct si_shader_context *ctx, unsigned input_index, LLVMValueRef out[4])
{
   const struct si_shader_info *info = &ctx->shader->selector->info;
   unsigned vs_blit_property = info->base.vs.blit_sgprs_amd;

   if (vs_blit_property) {
      LLVMValueRef vertex_id = ctx->abi.vertex_id;
      LLVMValueRef sel_x1 =
         LLVMBuildICmp(ctx->ac.builder, LLVMIntULE, vertex_id, ctx->ac.i32_1, "");
      /* Use LLVMIntNE, because we have 3 vertices and only
       * the middle one should use y2.
       */
      LLVMValueRef sel_y1 = LLVMBuildICmp(ctx->ac.builder, LLVMIntNE, vertex_id, ctx->ac.i32_1, "");

      unsigned param_vs_blit_inputs = ctx->args->vs_blit_inputs.arg_index;
      if (input_index == 0) {
         /* Position: */
         LLVMValueRef x1y1 = LLVMGetParam(ctx->main_fn.value, param_vs_blit_inputs);
         LLVMValueRef x2y2 = LLVMGetParam(ctx->main_fn.value, param_vs_blit_inputs + 1);

         LLVMValueRef x1 = unpack_sint16(ctx, x1y1, 0);
         LLVMValueRef y1 = unpack_sint16(ctx, x1y1, 1);
         LLVMValueRef x2 = unpack_sint16(ctx, x2y2, 0);
         LLVMValueRef y2 = unpack_sint16(ctx, x2y2, 1);

         LLVMValueRef x = LLVMBuildSelect(ctx->ac.builder, sel_x1, x1, x2, "");
         LLVMValueRef y = LLVMBuildSelect(ctx->ac.builder, sel_y1, y1, y2, "");

         out[0] = LLVMBuildSIToFP(ctx->ac.builder, x, ctx->ac.f32, "");
         out[1] = LLVMBuildSIToFP(ctx->ac.builder, y, ctx->ac.f32, "");
         out[2] = LLVMGetParam(ctx->main_fn.value, param_vs_blit_inputs + 2);
         out[3] = ctx->ac.f32_1;
         return;
      }

      /* Color or texture coordinates: */
      assert(input_index == 1);

      if (vs_blit_property == SI_VS_BLIT_SGPRS_POS_COLOR) {
         for (int i = 0; i < 4; i++) {
            out[i] = LLVMGetParam(ctx->main_fn.value, param_vs_blit_inputs + 3 + i);
         }
      } else {
         assert(vs_blit_property == SI_VS_BLIT_SGPRS_POS_TEXCOORD);
         LLVMValueRef x1 = LLVMGetParam(ctx->main_fn.value, param_vs_blit_inputs + 3);
         LLVMValueRef y1 = LLVMGetParam(ctx->main_fn.value, param_vs_blit_inputs + 4);
         LLVMValueRef x2 = LLVMGetParam(ctx->main_fn.value, param_vs_blit_inputs + 5);
         LLVMValueRef y2 = LLVMGetParam(ctx->main_fn.value, param_vs_blit_inputs + 6);

         out[0] = LLVMBuildSelect(ctx->ac.builder, sel_x1, x1, x2, "");
         out[1] = LLVMBuildSelect(ctx->ac.builder, sel_y1, y1, y2, "");
         out[2] = LLVMGetParam(ctx->main_fn.value, param_vs_blit_inputs + 7);
         out[3] = LLVMGetParam(ctx->main_fn.value, param_vs_blit_inputs + 8);
      }
      return;
   }

   /* Set can_speculate=false to help keep all loads grouped together
    * for better latency hiding. If it was true, LLVM could move the loads forward
    * and accidentally double memory latency by doing:
    *
    *    buffer_load_dword_xyzw
    *    s_waitcnt vmcnt(0)
    *    buffer_load_dword_xyzw
    *    s_waitcnt vmcnt(0)
    *
    * ... which is what we must prevent at all cost.
    */
   const bool can_speculate = false;
   unsigned bit_size = info->input[input_index].fp16_lo_hi_valid & 0x1 ? 16 : 32;
   LLVMTypeRef int_type = bit_size == 16 ? ctx->ac.i16 : ctx->ac.i32;
   LLVMTypeRef float_type = bit_size == 16 ? ctx->ac.f16 : ctx->ac.f32;
   unsigned num_vbos_in_user_sgprs = ctx->shader->selector->info.num_vbos_in_user_sgprs;
   union si_vs_fix_fetch fix_fetch;
   LLVMValueRef vb_desc;
   LLVMValueRef vertex_index = NULL;
   LLVMValueRef tmp;

   if (input_index < num_vbos_in_user_sgprs) {
      vb_desc = ac_get_arg(&ctx->ac, ctx->args->vb_descriptors[input_index]);
   } else {
      unsigned index = input_index - num_vbos_in_user_sgprs;
      vb_desc = ac_build_load_to_sgpr(
         &ctx->ac, ac_get_ptr_arg(&ctx->ac, &ctx->args->ac, ctx->args->ac.vertex_buffers),
         LLVMConstInt(ctx->ac.i32, index, 0));
   }

   if (ctx->abi.vertex_id_replaced) {
      /* Only ngg culling will replace vertex_id, and ngg culling is an optimization key
       * field, so the shader must be monolithic.
       */
      assert(ctx->shader->is_monolithic);
      assert(ctx->abi.instance_id_replaced);

      vertex_index = get_vertex_index(ctx, &ctx->shader->key.ge.part.vs.prolog,
                                      input_index, ctx->instance_divisor_constbuf,
                                      ctx->args->ac.start_instance.arg_index,
                                      ctx->args->ac.base_vertex.arg_index);
   } else {
      vertex_index = LLVMGetParam(ctx->main_fn.value,
                                  ctx->args->vertex_index0.arg_index + input_index);
   }

   /* Use the open-coded implementation for all loads of doubles and
    * of dword-sized data that needs fixups. We need to insert conversion
    * code anyway, and the amd/common code does it for us.
    */
   bool opencode = ctx->shader->key.ge.mono.vs_fetch_opencode & (1 << input_index);
   fix_fetch.bits = ctx->shader->key.ge.mono.vs_fix_fetch[input_index].bits;
   if (opencode || (fix_fetch.u.log_size == 3 && fix_fetch.u.format == AC_FETCH_FORMAT_FLOAT) ||
       (fix_fetch.u.log_size == 2)) {
      tmp = ac_build_opencoded_load_format(&ctx->ac, fix_fetch.u.log_size,
                                           fix_fetch.u.num_channels_m1 + 1, fix_fetch.u.format,
                                           fix_fetch.u.reverse, !opencode, vb_desc, vertex_index,
                                           ctx->ac.i32_0, ctx->ac.i32_0, 0, can_speculate);
      for (unsigned i = 0; i < 4; ++i)
         out[i] =
            LLVMBuildExtractElement(ctx->ac.builder, tmp, LLVMConstInt(ctx->ac.i32, i, false), "");

      if (bit_size == 16) {
         if (fix_fetch.u.format == AC_FETCH_FORMAT_UINT ||
             fix_fetch.u.format == AC_FETCH_FORMAT_SINT) {
            for (unsigned i = 0; i < 4; i++)
               out[i] = LLVMBuildTrunc(ctx->ac.builder, out[i], ctx->ac.i16, "");
         } else {
            for (unsigned i = 0; i < 4; i++) {
               out[i] = ac_to_float(&ctx->ac, out[i]);
               out[i] = LLVMBuildFPTrunc(ctx->ac.builder, out[i], ctx->ac.f16, "");
            }
         }
      }
      return;
   }

   unsigned required_channels = util_last_bit(info->input[input_index].usage_mask);
   if (required_channels == 0) {
      for (unsigned i = 0; i < 4; ++i)
         out[i] = LLVMGetUndef(ctx->ac.f32);
      return;
   }

   /* Do multiple loads for special formats. */
   LLVMValueRef fetches[4];
   unsigned num_fetches;
   unsigned fetch_stride;
   unsigned channels_per_fetch;

   if (fix_fetch.u.log_size <= 1 && fix_fetch.u.num_channels_m1 == 2) {
      num_fetches = MIN2(required_channels, 3);
      fetch_stride = 1 << fix_fetch.u.log_size;
      channels_per_fetch = 1;
   } else {
      num_fetches = 1;
      fetch_stride = 0;
      channels_per_fetch = required_channels;
   }

   for (unsigned i = 0; i < num_fetches; ++i) {
      LLVMValueRef voffset = LLVMConstInt(ctx->ac.i32, fetch_stride * i, 0);
      fetches[i] = ac_build_buffer_load_format(&ctx->ac, vb_desc, vertex_index, voffset,
                                               channels_per_fetch, 0, can_speculate,
                                               bit_size == 16, false);
   }

   if (num_fetches == 1 && channels_per_fetch > 1) {
      LLVMValueRef fetch = fetches[0];
      for (unsigned i = 0; i < channels_per_fetch; ++i) {
         tmp = LLVMConstInt(ctx->ac.i32, i, false);
         fetches[i] = LLVMBuildExtractElement(ctx->ac.builder, fetch, tmp, "");
      }
      num_fetches = channels_per_fetch;
      channels_per_fetch = 1;
   }

   for (unsigned i = num_fetches; i < 4; ++i)
      fetches[i] = LLVMGetUndef(float_type);

   if (fix_fetch.u.log_size <= 1 && fix_fetch.u.num_channels_m1 == 2 && required_channels == 4) {
      if (fix_fetch.u.format == AC_FETCH_FORMAT_UINT || fix_fetch.u.format == AC_FETCH_FORMAT_SINT)
         fetches[3] = LLVMConstInt(int_type, 1, 0);
      else
         fetches[3] = LLVMConstReal(float_type, 1);
   } else if (fix_fetch.u.log_size == 3 &&
              (fix_fetch.u.format == AC_FETCH_FORMAT_SNORM ||
               fix_fetch.u.format == AC_FETCH_FORMAT_SSCALED ||
               fix_fetch.u.format == AC_FETCH_FORMAT_SINT) &&
              required_channels == 4) {

      /* For 2_10_10_10, the hardware returns an unsigned value;
       * convert it to a signed one.
       */
      LLVMValueRef tmp = fetches[3];
      LLVMValueRef c30 = LLVMConstInt(int_type, 30, 0);

      /* First, recover the sign-extended signed integer value. */
      if (fix_fetch.u.format == AC_FETCH_FORMAT_SSCALED)
         tmp = LLVMBuildFPToUI(ctx->ac.builder, tmp, int_type, "");
      else
         tmp = ac_to_integer(&ctx->ac, tmp);

      /* For the integer-like cases, do a natural sign extension.
       *
       * For the SNORM case, the values are 0.0, 0.333, 0.666, 1.0
       * and happen to contain 0, 1, 2, 3 as the two LSBs of the
       * exponent.
       */
      tmp = LLVMBuildShl(
         ctx->ac.builder, tmp,
         fix_fetch.u.format == AC_FETCH_FORMAT_SNORM ? LLVMConstInt(int_type, 7, 0) : c30, "");
      tmp = LLVMBuildAShr(ctx->ac.builder, tmp, c30, "");

      /* Convert back to the right type. */
      if (fix_fetch.u.format == AC_FETCH_FORMAT_SNORM) {
         LLVMValueRef clamp;
         LLVMValueRef neg_one = LLVMConstReal(float_type, -1.0);
         tmp = LLVMBuildSIToFP(ctx->ac.builder, tmp, float_type, "");
         clamp = LLVMBuildFCmp(ctx->ac.builder, LLVMRealULT, tmp, neg_one, "");
         tmp = LLVMBuildSelect(ctx->ac.builder, clamp, neg_one, tmp, "");
      } else if (fix_fetch.u.format == AC_FETCH_FORMAT_SSCALED) {
         tmp = LLVMBuildSIToFP(ctx->ac.builder, tmp, float_type, "");
      }

      fetches[3] = tmp;
   }

   for (unsigned i = 0; i < 4; ++i)
      out[i] = ac_to_float(&ctx->ac, fetches[i]);
}

static LLVMValueRef si_load_vs_input(struct ac_shader_abi *abi, unsigned driver_location,
                                     unsigned component, unsigned num_components,
                                     unsigned vertex_index, LLVMTypeRef type)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   LLVMValueRef values[4];

   load_input_vs(ctx, driver_location, values);

   for (unsigned i = 0; i < 4; i++)
      values[i] = LLVMBuildBitCast(ctx->ac.builder, values[i], type, "");

   return ac_build_varying_gather_values(&ctx->ac, values, num_components, component);
}

/**
 * Build the vertex shader prolog function.
 *
 * The inputs are the same as VS (a lot of SGPRs and 4 VGPR system values).
 * All inputs are returned unmodified. The vertex load indices are
 * stored after them, which will be used by the API VS for fetching inputs.
 *
 * For example, the expected outputs for instance_divisors[] = {0, 1, 2} are:
 *   input_v0,
 *   input_v1,
 *   input_v2,
 *   input_v3,
 *   (VertexID + BaseVertex),
 *   (InstanceID + StartInstance),
 *   (InstanceID / 2 + StartInstance)
 */
void si_llvm_build_vs_prolog(struct si_shader_context *ctx, union si_shader_part_key *key,
                             UNUSED bool separate_prolog)
{
   LLVMTypeRef *returns;
   LLVMValueRef ret, func;
   int num_returns, i;
   unsigned first_vs_vgpr = key->vs_prolog.num_merged_next_stage_vgprs;
   unsigned num_input_vgprs =
      key->vs_prolog.num_merged_next_stage_vgprs + 4;
   struct ac_arg input_sgpr_param[key->vs_prolog.num_input_sgprs];
   struct ac_arg input_vgpr_param[10];
   LLVMValueRef input_vgprs[10];
   unsigned num_all_input_regs = key->vs_prolog.num_input_sgprs + num_input_vgprs;
   unsigned user_sgpr_base = key->vs_prolog.num_merged_next_stage_vgprs ? 8 : 0;

   memset(ctx->args, 0, sizeof(*ctx->args));

   /* 4 preloaded VGPRs + vertex load indices as prolog outputs */
   returns = alloca((num_all_input_regs + key->vs_prolog.num_inputs) * sizeof(LLVMTypeRef));
   num_returns = 0;

   /* Declare input and output SGPRs. */
   for (i = 0; i < key->vs_prolog.num_input_sgprs; i++) {
      ac_add_arg(&ctx->args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &input_sgpr_param[i]);
      returns[num_returns++] = ctx->ac.i32;
   }

   /* Preloaded VGPRs (outputs must be floats) */
   for (i = 0; i < num_input_vgprs; i++) {
      ac_add_arg(&ctx->args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &input_vgpr_param[i]);
      returns[num_returns++] = ctx->ac.f32;
   }

   /* Vertex load indices. */
   for (i = 0; i < key->vs_prolog.num_inputs; i++)
      returns[num_returns++] = ctx->ac.f32;

   /* Create the function. */
   si_llvm_create_func(ctx, "vs_prolog", returns, num_returns, 0);
   func = ctx->main_fn.value;

   for (i = 0; i < num_input_vgprs; i++) {
      input_vgprs[i] = ac_get_arg(&ctx->ac, input_vgpr_param[i]);
   }

   if (key->vs_prolog.num_merged_next_stage_vgprs) {
      if (!key->vs_prolog.is_monolithic)
         ac_init_exec_full_mask(&ctx->ac);

      if (key->vs_prolog.as_ls && ctx->screen->info.has_ls_vgpr_init_bug) {
         /* If there are no HS threads, SPI loads the LS VGPRs
          * starting at VGPR 0. Shift them back to where they
          * belong.
          */
         LLVMValueRef has_hs_threads =
            LLVMBuildICmp(ctx->ac.builder, LLVMIntNE,
                          si_unpack_param(ctx, input_sgpr_param[3], 8, 8), ctx->ac.i32_0, "");

         for (i = 4; i > 0; --i) {
            input_vgprs[i + 1] = LLVMBuildSelect(ctx->ac.builder, has_hs_threads,
                                                 input_vgprs[i + 1], input_vgprs[i - 1], "");
         }
      }
   }

   unsigned vertex_id_vgpr = first_vs_vgpr;
   unsigned instance_id_vgpr = ctx->screen->info.gfx_level >= GFX10
                                  ? first_vs_vgpr + 3
                                  : first_vs_vgpr + (key->vs_prolog.as_ls ? 2 : 1);

   ctx->abi.vertex_id = input_vgprs[vertex_id_vgpr];
   ctx->abi.instance_id = input_vgprs[instance_id_vgpr];
   ctx->abi.vertex_id_replaced = NULL;
   ctx->abi.instance_id_replaced = NULL;

   /* Copy inputs to outputs. This should be no-op, as the registers match,
    * but it will prevent the compiler from overwriting them unintentionally.
    */
   ret = ctx->return_value;
   for (i = 0; i < key->vs_prolog.num_input_sgprs; i++) {
      LLVMValueRef p = LLVMGetParam(func, i);
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, p, i, "");
   }
   for (i = 0; i < num_input_vgprs; i++) {
      LLVMValueRef p = input_vgprs[i];

      if (i == vertex_id_vgpr)
         p = ctx->abi.vertex_id;
      else if (i == instance_id_vgpr)
         p = ctx->abi.instance_id;

      p = ac_to_float(&ctx->ac, p);
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, p, key->vs_prolog.num_input_sgprs + i, "");
   }

   /* Compute vertex load indices from instance divisors. */
   LLVMValueRef instance_divisor_constbuf = NULL;

   if (key->vs_prolog.states.instance_divisor_is_fetched) {
      LLVMValueRef list = si_prolog_get_internal_bindings(ctx);
      LLVMValueRef buf_index = LLVMConstInt(ctx->ac.i32, SI_VS_CONST_INSTANCE_DIVISORS, 0);
      instance_divisor_constbuf = ac_build_load_to_sgpr(&ctx->ac,
         (struct ac_llvm_pointer) { .v = list, .t = ctx->ac.v4i32 }, buf_index);
   }

   for (i = 0; i < key->vs_prolog.num_inputs; i++) {
      LLVMValueRef index = get_vertex_index(ctx, &key->vs_prolog.states, i,
                                            instance_divisor_constbuf,
                                            user_sgpr_base + SI_SGPR_START_INSTANCE,
                                            user_sgpr_base + SI_SGPR_BASE_VERTEX);

      index = ac_to_float(&ctx->ac, index);
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, index, ctx->args->ac.arg_count + i, "");
   }

   si_llvm_build_ret(ctx, ret);
}

void si_llvm_init_vs_callbacks(struct si_shader_context *ctx)
{
   ctx->abi.load_inputs = si_load_vs_input;
}
