/*
 * Copyright © 2018 Intel Corporation
 * Copyright © 2023 Collabora, Ltd.
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

#include "nir_builder.h"
#include "util/u_math.h"
#include "util/bitscan.h"

static nir_intrinsic_instr *
dup_mem_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin,
                  nir_ssa_def *offset,
                  unsigned align_mul, unsigned align_offset,
                  nir_ssa_def *data,
                  unsigned num_components, unsigned bit_size)
{
   const nir_intrinsic_info *info = &nir_intrinsic_infos[intrin->intrinsic];

   nir_intrinsic_instr *dup =
      nir_intrinsic_instr_create(b->shader, intrin->intrinsic);

   nir_src *intrin_offset_src = nir_get_io_offset_src(intrin);
   for (unsigned i = 0; i < info->num_srcs; i++) {
      assert(intrin->src[i].is_ssa);
      if (i == 0 && data != NULL) {
         assert(!info->has_dest);
         assert(&intrin->src[i] != intrin_offset_src);
         dup->src[i] = nir_src_for_ssa(data);
      } else if (&intrin->src[i] == intrin_offset_src) {
         dup->src[i] = nir_src_for_ssa(offset);
      } else {
         dup->src[i] = nir_src_for_ssa(intrin->src[i].ssa);
      }
   }

   dup->num_components = num_components;
   for (unsigned i = 0; i < info->num_indices; i++)
      dup->const_index[i] = intrin->const_index[i];

   nir_intrinsic_set_align(dup, align_mul, align_offset);

   if (info->has_dest) {
      assert(intrin->dest.is_ssa);
      nir_ssa_dest_init(&dup->instr, &dup->dest,
                        num_components, bit_size, NULL);
   } else {
      nir_intrinsic_set_write_mask(dup, (1 << num_components) - 1);
   }

   nir_builder_instr_insert(b, &dup->instr);

   return dup;
}

static bool
lower_mem_load(nir_builder *b, nir_intrinsic_instr *intrin,
               nir_lower_mem_access_bit_sizes_cb mem_access_size_align_cb,
               const void *cb_data)
{
   assert(intrin->dest.is_ssa);
   const unsigned bit_size = intrin->dest.ssa.bit_size;
   const unsigned num_components = intrin->dest.ssa.num_components;
   const unsigned bytes_read = num_components * (bit_size / 8);
   const uint32_t align_mul = nir_intrinsic_align_mul(intrin);
   const uint32_t align_offset = nir_intrinsic_align_offset(intrin);
   nir_src *offset_src = nir_get_io_offset_src(intrin);
   const bool offset_is_const = nir_src_is_const(*offset_src);
   assert(offset_src->is_ssa);
   nir_ssa_def *offset = offset_src->ssa;

   nir_mem_access_size_align requested =
      mem_access_size_align_cb(intrin->intrinsic, bytes_read,
                               align_mul, align_offset,
                               offset_is_const, cb_data);

   assert(util_is_power_of_two_nonzero(align_mul));
   assert(util_is_power_of_two_nonzero(requested.align_mul));
   if (requested.num_components == num_components &&
       requested.bit_size == bit_size &&
       requested.align_mul <= align_mul)
      return false;

   /* Otherwise, we have to break it into chunks.  We could end up with as
    * many as 32 chunks if we're loading a u64vec16 as individual dwords.
    */
   nir_ssa_def *chunks[32];
   unsigned num_chunks = 0;
   unsigned chunk_start = 0;
   while (chunk_start < bytes_read) {
      const unsigned bytes_left = bytes_read - chunk_start;
      uint32_t chunk_align_offset =
         (align_offset + chunk_start) % align_mul;
      requested = mem_access_size_align_cb(intrin->intrinsic, bytes_left,
                                           align_mul, chunk_align_offset,
                                           offset_is_const, cb_data);

      unsigned chunk_bytes;
      assert(util_is_power_of_two_nonzero(requested.align_mul));
      if (align_mul < requested.align_mul) {
         /* For this case, we need to be able to shift the value so we assume
          * there's at most one component.
          */
         assert(requested.num_components == 1);
         assert(requested.bit_size >= requested.align_mul * 8);

         uint64_t align_mask = requested.align_mul - 1;
         nir_ssa_def *chunk_offset = nir_iadd_imm(b, offset, chunk_start);
         nir_ssa_def *pad = nir_iand_imm(b, chunk_offset, align_mask);
         chunk_offset = nir_iand_imm(b, chunk_offset, ~align_mask);

         nir_intrinsic_instr *load =
            dup_mem_intrinsic(b, intrin, chunk_offset,
                              requested.align_mul, 0, NULL,
                              requested.num_components, requested.bit_size);

         nir_ssa_def *shifted =
            nir_ushr(b, &load->dest.ssa, nir_imul_imm(b, pad, 8));

         chunk_bytes = MIN2(bytes_left, align_mul);
         assert(num_chunks < ARRAY_SIZE(chunks));
         chunks[num_chunks++] = nir_u2uN(b, shifted, chunk_bytes * 8);
      } else if (chunk_align_offset % requested.align_mul) {
         /* In this case, we know how much to adjust the offset */
         uint32_t delta = chunk_align_offset % requested.align_mul;
         nir_ssa_def *chunk_offset =
            nir_iadd_imm(b, offset, chunk_start - (int)delta);

         chunk_align_offset = (chunk_align_offset - delta) % align_mul;

         nir_intrinsic_instr *load =
            dup_mem_intrinsic(b, intrin, chunk_offset,
                              align_mul, chunk_align_offset, NULL,
                              requested.num_components, requested.bit_size);

         assert(requested.bit_size >= 8);
         chunk_bytes = requested.num_components * (requested.bit_size / 8);
         assert(chunk_bytes > delta);
         chunk_bytes -= delta;

         unsigned chunk_bit_size = MIN2(8 << (ffs(chunk_bytes) - 1), bit_size);
         unsigned chunk_num_components = chunk_bytes / (chunk_bit_size / 8);

         /* There's no guarantee that chunk_num_components is a valid NIR
          * vector size, so just loop one chunk component at a time
          */
         nir_ssa_def *chunk_data = &load->dest.ssa;
         for (unsigned i = 0; i < chunk_num_components; i++) {
            assert(num_chunks < ARRAY_SIZE(chunks));
            chunks[num_chunks++] =
               nir_extract_bits(b, &chunk_data, 1,
                                delta * 8 + i * chunk_bit_size,
                                1, chunk_bit_size);
         }
      } else {
         nir_ssa_def *chunk_offset = nir_iadd_imm(b, offset, chunk_start);
         nir_intrinsic_instr *load =
            dup_mem_intrinsic(b, intrin, chunk_offset,
                              align_mul, chunk_align_offset, NULL,
                              requested.num_components, requested.bit_size);

         chunk_bytes = requested.num_components * (requested.bit_size / 8);
         assert(num_chunks < ARRAY_SIZE(chunks));
         chunks[num_chunks++] = &load->dest.ssa;
      }

      chunk_start += chunk_bytes;
   }

   nir_ssa_def *result = nir_extract_bits(b, chunks, num_chunks, 0,
                                          num_components, bit_size);
   nir_ssa_def_rewrite_uses(&intrin->dest.ssa, result);
   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
lower_mem_store(nir_builder *b, nir_intrinsic_instr *intrin,
               nir_lower_mem_access_bit_sizes_cb mem_access_size_align_cb,
               const void *cb_data)
{
   assert(intrin->src[0].is_ssa);
   nir_ssa_def *value = intrin->src[0].ssa;

   assert(intrin->num_components == value->num_components);
   const unsigned bit_size = value->bit_size;
   const unsigned byte_size = bit_size / 8;
   const unsigned num_components = intrin->num_components;
   const unsigned bytes_written = num_components * byte_size;
   const uint32_t align_mul = nir_intrinsic_align_mul(intrin);
   const uint32_t align_offset = nir_intrinsic_align_offset(intrin);
   nir_src *offset_src = nir_get_io_offset_src(intrin);
   const bool offset_is_const = nir_src_is_const(*offset_src);
   assert(offset_src->is_ssa);
   nir_ssa_def *offset = offset_src->ssa;

   nir_component_mask_t writemask = nir_intrinsic_write_mask(intrin);
   assert(writemask < (1 << num_components));

   nir_mem_access_size_align requested =
      mem_access_size_align_cb(intrin->intrinsic, bytes_written,
                               align_mul, align_offset,
                               offset_is_const, cb_data);

   assert(util_is_power_of_two_nonzero(align_mul));
   assert(util_is_power_of_two_nonzero(requested.align_mul));
   if (requested.num_components == num_components &&
       requested.bit_size == bit_size &&
       requested.align_mul <= align_mul &&
       writemask == BITFIELD_MASK(num_components))
      return false;

   assert(byte_size <= sizeof(uint64_t));
   BITSET_DECLARE(mask, NIR_MAX_VEC_COMPONENTS * sizeof(uint64_t));
   BITSET_ZERO(mask);

   for (unsigned i = 0; i < num_components; i++) {
      if (writemask & (1u << i)) {
         BITSET_SET_RANGE_INSIDE_WORD(mask, i * byte_size,
                                      ((i + 1) * byte_size) - 1);
      }
   }

   while (BITSET_FFS(mask) != 0) {
      const uint32_t chunk_start = BITSET_FFS(mask) - 1;

      uint32_t end;
      for (end = chunk_start + 1; end < bytes_written; end++) {
         if (!(BITSET_TEST(mask, end)))
            break;
      }
      /* The size of the current contiguous chunk in bytes */
      const uint32_t max_chunk_bytes = end - chunk_start;
      const uint32_t chunk_align_offset =
         (align_offset + chunk_start) % align_mul;

      requested = mem_access_size_align_cb(intrin->intrinsic, max_chunk_bytes,
                                           align_mul, chunk_align_offset,
                                           offset_is_const, cb_data);

      const uint32_t chunk_bytes =
         requested.num_components * (requested.bit_size / 8);
      assert(chunk_bytes <= max_chunk_bytes);

      assert(util_is_power_of_two_nonzero(requested.align_mul));
      assert(requested.align_mul <= align_mul);
      assert((chunk_align_offset % requested.align_mul) == 0);

      nir_ssa_def *packed = nir_extract_bits(b, &value, 1, chunk_start * 8,
                                             requested.num_components,
                                             requested.bit_size);

      nir_ssa_def *chunk_offset = nir_iadd_imm(b, offset, chunk_start);
      dup_mem_intrinsic(b, intrin, chunk_offset,
                        align_mul, chunk_align_offset, packed,
                        requested.num_components, requested.bit_size);

      BITSET_CLEAR_RANGE(mask, chunk_start, (chunk_start + chunk_bytes - 1));
   }

   nir_instr_remove(&intrin->instr);

   return true;
}

struct lower_mem_access_state {
   nir_lower_mem_access_bit_sizes_cb cb;
   const void *cb_data;
};

static bool
lower_mem_access_instr(nir_builder *b, nir_instr *instr, void *_data)
{
   struct lower_mem_access_state *state = _data;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   b->cursor = nir_after_instr(instr);

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_load_task_payload:
      return lower_mem_load(b, intrin, state->cb, state->cb_data);

   case nir_intrinsic_store_global:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_store_scratch:
   case nir_intrinsic_store_task_payload:
      return lower_mem_store(b, intrin, state->cb, state->cb_data);

   default:
      return false;
   }
}

bool
nir_lower_mem_access_bit_sizes(nir_shader *shader,
                               nir_lower_mem_access_bit_sizes_cb cb,
                               const void *cb_data)
{
   struct lower_mem_access_state state = {
      .cb = cb,
      .cb_data = cb_data
   };

   return nir_shader_instructions_pass(shader, lower_mem_access_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance,
                                       (void *)&state);
}
