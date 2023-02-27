/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2020 Collabora Ltd.
 * Copyright © 2016 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_builtin_builder.h"
#include "agx_compiler.h"

#define AGX_TEXTURE_DESC_STRIDE 24

static nir_ssa_def *
texture_descriptor_ptr(nir_builder *b, nir_tex_instr *tex)
{
   /* For bindless, we store the descriptor pointer in the texture handle */
   int handle_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);
   if (handle_idx >= 0)
      return tex->src[handle_idx].src.ssa;

   /* For non-bindless, compute from the texture index, offset, and table */
   unsigned base_B = tex->texture_index * AGX_TEXTURE_DESC_STRIDE;
   nir_ssa_def *offs = nir_imm_int(b, base_B);

   int offs_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_offset);
   if (offs_idx >= 0) {
      nir_ssa_def *offset_src = tex->src[offs_idx].src.ssa;
      offs = nir_iadd(b, offs,
                      nir_imul_imm(b, offset_src, AGX_TEXTURE_DESC_STRIDE));
   }

   return nir_iadd(b, nir_load_texture_base_agx(b), nir_u2u64(b, offs));
}

static nir_ssa_def *
steal_tex_src(nir_tex_instr *tex, nir_tex_src_type type_)
{
   int idx = nir_tex_instr_src_index(tex, type_);

   if (idx < 0)
      return NULL;

   nir_ssa_def *ssa = tex->src[idx].src.ssa;
   nir_tex_instr_remove_src(tex, idx);
   return ssa;
}

static nir_ssa_def *
agx_txs(nir_builder *b, nir_tex_instr *tex)
{
   nir_ssa_def *ptr = texture_descriptor_ptr(b, tex);
   nir_ssa_def *comp[4] = {NULL};

   nir_ssa_def *desc = nir_load_global_constant(b, ptr, 8, 4, 32);
   nir_ssa_def *w0 = nir_channel(b, desc, 0);
   nir_ssa_def *w1 = nir_channel(b, desc, 1);
   nir_ssa_def *w3 = nir_channel(b, desc, 3);

   /* Width minus 1: bits [28, 42) */
   nir_ssa_def *width_m1 =
      nir_extr_agx(b, w0, w1, nir_imm_int(b, 28), nir_imm_int(b, 14));

   /* Height minus 1: bits [42, 56) */
   nir_ssa_def *height_m1 =
      nir_iand_imm(b, nir_ushr_imm(b, w1, 42 - 32), BITFIELD_MASK(14));

   /* Depth minus 1: bits [110, 124) */
   nir_ssa_def *depth_m1 =
      nir_iand_imm(b, nir_ushr_imm(b, w3, 110 - 96), BITFIELD_MASK(14));

   /* First level: bits [56, 60) */
   nir_ssa_def *lod =
      nir_iand_imm(b, nir_ushr_imm(b, w1, 56 - 32), BITFIELD_MASK(4));

   /* Add LOD offset to first level to get the interesting LOD */
   int lod_idx = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   if (lod_idx >= 0)
      lod = nir_iadd(
         b, lod, nir_u2u32(b, nir_ssa_for_src(b, tex->src[lod_idx].src, 1)));

   /* Add 1 to width-1, height-1 to get base dimensions */
   nir_ssa_def *width = nir_iadd_imm(b, width_m1, 1);
   nir_ssa_def *height = nir_iadd_imm(b, height_m1, 1);
   nir_ssa_def *depth = nir_iadd_imm(b, depth_m1, 1);

   /* How we finish depends on the size of the result */
   unsigned nr_comps = nir_dest_num_components(tex->dest);
   assert(nr_comps <= 3);

   /* Adjust for LOD, do not adjust array size */
   assert(!(nr_comps <= 1 && tex->is_array));
   width = nir_imax(b, nir_ushr(b, width, lod), nir_imm_int(b, 1));

   if (!(nr_comps == 2 && tex->is_array))
      height = nir_imax(b, nir_ushr(b, height, lod), nir_imm_int(b, 1));

   if (!(nr_comps == 3 && tex->is_array))
      depth = nir_imax(b, nir_ushr(b, depth, lod), nir_imm_int(b, 1));

   /* Cube maps have equal width and height, we save some instructions by only
    * reading one. Dead code elimination will remove the redundant instructions.
    */
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE)
      height = width;

   comp[0] = width;
   comp[1] = height;
   comp[2] = depth;

   return nir_vec(b, comp, nr_comps);
}

static bool
lower_txs(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   b->cursor = nir_before_instr(instr);

   if (tex->op != nir_texop_txs)
      return false;

   nir_ssa_def *res = agx_txs(b, tex);
   nir_ssa_def_rewrite_uses_after(&tex->dest.ssa, res, instr);
   nir_instr_remove(instr);
   return true;
}

/*
 * NIR indexes into array textures with unclamped floats (integer for txf). AGX
 * requires the index to be a clamped integer. Lower tex_src_coord into
 * tex_src_backend1 for array textures by type-converting and clamping.
 */
static bool
lower_regular_texture(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   b->cursor = nir_before_instr(instr);

   if (nir_tex_instr_is_query(tex))
      return false;

   /* Get the coordinates */
   nir_ssa_def *coord = steal_tex_src(tex, nir_tex_src_coord);
   nir_ssa_def *ms_idx = steal_tex_src(tex, nir_tex_src_ms_index);

   /* The layer is always the last component of the NIR coordinate, split it off
    * because we'll need to swizzle.
    */
   nir_ssa_def *layer = NULL;

   if (tex->is_array) {
      unsigned lidx = coord->num_components - 1;
      nir_ssa_def *unclamped_layer = nir_channel(b, coord, lidx);
      coord = nir_trim_vector(b, coord, lidx);

      /* Round layer to nearest even */
      if (tex->op != nir_texop_txf && tex->op != nir_texop_txf_ms)
         unclamped_layer = nir_f2u32(b, nir_fround_even(b, unclamped_layer));

      /* Clamp to max layer = (# of layers - 1) for out-of-bounds handling.
       * Layer must be 16-bits for the hardware, drop top bits after clamping.
       */
      nir_ssa_def *txs = nir_get_texture_size(b, tex);
      nir_ssa_def *nr_layers = nir_channel(b, txs, lidx);
      nir_ssa_def *max_layer = nir_iadd_imm(b, nr_layers, -1);
      layer = nir_u2u16(b, nir_umin(b, unclamped_layer, max_layer));
   }

   /* Combine layer and multisample index into 32-bit so we don't need a vec5 or
    * vec6 16-bit coordinate tuple, which would be inconvenient in NIR for
    * little benefit (a minor optimization, I guess).
    */
   nir_ssa_def *sample_array = (ms_idx && layer)
                                  ? nir_pack_32_2x16_split(b, ms_idx, layer)
                               : ms_idx ? nir_u2u32(b, ms_idx)
                               : layer  ? nir_u2u32(b, layer)
                                        : NULL;

   /* Combine into the final 32-bit tuple */
   if (sample_array != NULL) {
      unsigned end = coord->num_components;
      coord = nir_pad_vector(b, coord, end + 1);
      coord = nir_vector_insert_imm(b, coord, sample_array, end);
   }

   nir_tex_instr_add_src(tex, nir_tex_src_backend1, nir_src_for_ssa(coord));

   /* Furthermore, if there is an offset vector, it must be packed */
   nir_ssa_def *offset = steal_tex_src(tex, nir_tex_src_offset);

   if (offset != NULL) {
      nir_ssa_def *packed = NULL;

      for (unsigned c = 0; c < offset->num_components; ++c) {
         nir_ssa_def *nibble = nir_iand_imm(b, nir_channel(b, offset, c), 0xF);
         nir_ssa_def *shifted = nir_ishl_imm(b, nibble, 4 * c);

         if (packed != NULL)
            packed = nir_ior(b, packed, shifted);
         else
            packed = shifted;
      }

      nir_tex_instr_add_src(tex, nir_tex_src_backend2, nir_src_for_ssa(packed));
   }

   return true;
}

static nir_ssa_def *
bias_for_tex(nir_builder *b, nir_tex_instr *tex)
{
   nir_instr *instr = nir_get_texture_size(b, tex)->parent_instr;
   nir_tex_instr *query = nir_instr_as_tex(instr);

   query->op = nir_texop_lod_bias_agx;
   query->dest_type = nir_type_float16;

   nir_ssa_dest_init(instr, &query->dest, 1, 16, NULL);
   return &query->dest.ssa;
}

static bool
lower_sampler_bias(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   b->cursor = nir_before_instr(instr);

   switch (tex->op) {
   case nir_texop_tex: {
      tex->op = nir_texop_txb;
      nir_tex_instr_add_src(tex, nir_tex_src_bias,
                            nir_src_for_ssa(bias_for_tex(b, tex)));
      return true;
   }

   case nir_texop_txb:
   case nir_texop_txl: {
      nir_tex_src_type src =
         tex->op == nir_texop_txl ? nir_tex_src_lod : nir_tex_src_bias;

      nir_ssa_def *orig = steal_tex_src(tex, src);
      assert(orig != NULL && "invalid NIR");

      if (orig->bit_size != 16)
         orig = nir_f2f16(b, orig);

      nir_tex_instr_add_src(
         tex, src, nir_src_for_ssa(nir_fadd(b, orig, bias_for_tex(b, tex))));
      return true;
   }

   case nir_texop_txd: {
      /* For txd, the computed level-of-detail is log2(rho)
       * where rho should scale proportionally to all
       * derivatives. So scale derivatives by exp2(bias) to
       * get level-of-detail log2(exp2(bias) * rho) = bias + log2(rho).
       */
      nir_ssa_def *scale = nir_fexp2(b, nir_f2f32(b, bias_for_tex(b, tex)));
      nir_tex_src_type src[] = {nir_tex_src_ddx, nir_tex_src_ddy};

      for (unsigned s = 0; s < ARRAY_SIZE(src); ++s) {
         nir_ssa_def *orig = steal_tex_src(tex, src[s]);
         assert(orig != NULL && "invalid");

         nir_ssa_def *scaled = nir_fmul(b, nir_f2f32(b, orig), scale);
         nir_tex_instr_add_src(tex, src[s], nir_src_for_ssa(scaled));
      }

      return true;
   }

   case nir_texop_txf:
   case nir_texop_txf_ms:
   case nir_texop_txs:
   case nir_texop_tg4:
   case nir_texop_texture_samples:
   case nir_texop_samples_identical:
      /* These operations do not use a sampler */
      return false;

   default:
      unreachable("Unhandled texture operation");
   }
}

bool
agx_nir_lower_texture(nir_shader *s, bool support_lod_bias)
{
   bool progress = false;

   nir_lower_tex_options lower_tex_options = {
      .lower_txp = ~0,
      .lower_invalid_implicit_lod = true,
      .lower_tg4_offsets = true,

      /* XXX: Metal seems to handle just like 3D txd, so why doesn't it work?
       * TODO: Stop using this lowering
       */
      .lower_txd_cube_map = true,
   };

   nir_tex_src_type_constraints tex_constraints = {
      [nir_tex_src_lod] = {true, 16},
      [nir_tex_src_bias] = {true, 16},
      [nir_tex_src_ms_index] = {true, 16},
   };

   NIR_PASS(progress, s, nir_lower_tex, &lower_tex_options);

   /* Lower bias after nir_lower_tex (to get rid of txd) but before
    * lower_regular_texture (which will shuffle around the sources)
    */
   if (support_lod_bias) {
      NIR_PASS(progress, s, nir_shader_instructions_pass, lower_sampler_bias,
               nir_metadata_block_index | nir_metadata_dominance, NULL);
   }

   NIR_PASS(progress, s, nir_legalize_16bit_sampler_srcs, tex_constraints);

   /* Lower texture sources after legalizing types (as the lowering depends on
    * 16-bit multisample indices) but before lowering queries (as the lowering
    * generates txs for array textures).
    */
   NIR_PASS(progress, s, nir_shader_instructions_pass, lower_regular_texture,
            nir_metadata_block_index | nir_metadata_dominance, NULL);
   NIR_PASS(progress, s, nir_shader_instructions_pass, lower_txs,
            nir_metadata_block_index | nir_metadata_dominance, NULL);

   return progress;
}
