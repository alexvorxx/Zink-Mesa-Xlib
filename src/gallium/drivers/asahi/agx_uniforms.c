/*
 * Copyright 2021 Alyssa Rosenzweig
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
#include <stdio.h>
#include "asahi/lib/agx_pack.h"
#include "agx_state.h"

static uint64_t
agx_const_buffer_ptr(struct agx_batch *batch, struct pipe_constant_buffer *cb)
{
   if (cb->buffer) {
      struct agx_resource *rsrc = agx_resource(cb->buffer);
      agx_batch_reads(batch, rsrc);

      return rsrc->bo->ptr.gpu + cb->buffer_offset;
   } else {
      return agx_pool_upload_aligned(
         &batch->pool, ((uint8_t *)cb->user_buffer) + cb->buffer_offset,
         cb->buffer_size - cb->buffer_offset, 64);
   }
}

static uint64_t
agx_vertex_buffer_ptr(struct agx_batch *batch, unsigned vbo)
{
   struct pipe_vertex_buffer vb = batch->ctx->vertex_buffers[vbo];
   assert(!vb.is_user_buffer);

   if (vb.buffer.resource) {
      struct agx_resource *rsrc = agx_resource(vb.buffer.resource);
      agx_batch_reads(batch, rsrc);

      return rsrc->bo->ptr.gpu + vb.buffer_offset;
   } else {
      return 0;
   }
}

uint64_t
agx_upload_uniforms(struct agx_batch *batch, uint64_t textures,
                    enum pipe_shader_type stage)
{
   struct agx_context *ctx = batch->ctx;
   struct agx_stage *st = &ctx->stage[stage];

   struct agx_draw_uniforms uniforms = {.texture_base = textures};

   u_foreach_bit(cb, st->cb_mask) {
      uniforms.ubo_base[cb] = agx_const_buffer_ptr(batch, &st->cb[cb]);
   }

   if (stage == PIPE_SHADER_VERTEX) {
      u_foreach_bit(vbo, ctx->vb_mask) {
         uniforms.vs.vbo_base[vbo] = agx_vertex_buffer_ptr(batch, vbo);
      }
   } else if (stage == PIPE_SHADER_FRAGMENT) {
      memcpy(uniforms.fs.blend_constant, &ctx->blend_color,
             sizeof(ctx->blend_color));
   }

   return agx_pool_upload(&batch->pool, &uniforms, sizeof(uniforms));
}
