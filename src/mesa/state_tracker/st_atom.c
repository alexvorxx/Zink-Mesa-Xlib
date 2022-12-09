/**************************************************************************
 * 
 * Copyright 2003 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/


#include <stdio.h>
#include "main/arrayobj.h"
#include "util/glheader.h"
#include "main/context.h"

#include "pipe/p_defines.h"
#include "st_context.h"
#include "st_atom.h"
#include "st_program.h"
#include "st_manager.h"
#include "st_util.h"

#include "util/u_cpu_detect.h"


typedef void (*update_func_t)(struct st_context *st);

/* The list state update functions. */
static update_func_t update_functions[ST_NUM_ATOMS];

static void
init_atoms_once(void)
{
#define ST_STATE(FLAG, st_update) update_functions[FLAG##_INDEX] = st_update;
#include "st_atom_list.h"
#undef ST_STATE

   if (util_get_cpu_caps()->has_popcnt)
      update_functions[ST_NEW_VERTEX_ARRAYS_INDEX] = st_update_array_with_popcnt;
}

void st_init_atoms( struct st_context *st )
{
   STATIC_ASSERT(ARRAY_SIZE(update_functions) <= 64);

   static once_flag flag = ONCE_FLAG_INIT;
   call_once(&flag, init_atoms_once);
}

void st_destroy_atoms( struct st_context *st )
{
   /* no-op */
}

void st_update_edgeflags(struct st_context *st, bool per_vertex_edgeflags)
{
   bool edgeflags_enabled = st->ctx->Polygon.FrontMode != GL_FILL ||
                            st->ctx->Polygon.BackMode != GL_FILL;
   bool vertdata_edgeflags = edgeflags_enabled && per_vertex_edgeflags;

   if (vertdata_edgeflags != st->vertdata_edgeflags) {
      st->vertdata_edgeflags = vertdata_edgeflags;

      struct gl_program *vp = st->ctx->VertexProgram._Current;
      if (vp)
         st->dirty |= ST_NEW_VERTEX_PROGRAM(st->ctx, vp);
   }

   bool edgeflag_culls_prims = edgeflags_enabled && !vertdata_edgeflags &&
                               !st->ctx->Current.Attrib[VERT_ATTRIB_EDGEFLAG][0];
   if (edgeflag_culls_prims != st->edgeflag_culls_prims) {
      st->edgeflag_culls_prims = edgeflag_culls_prims;
      st->dirty |= ST_NEW_RASTERIZER;
   }
}

static void check_attrib_edgeflag(struct st_context *st)
{
   st_update_edgeflags(st, _mesa_draw_edge_flag_array_enabled(st->ctx));
}

/***********************************************************************
 * Update all derived state:
 */

void st_validate_state( struct st_context *st, enum st_pipeline pipeline )
{
   struct gl_context *ctx = st->ctx;
   uint64_t dirty, pipeline_mask;
   uint32_t dirty_lo, dirty_hi;

   /* Get Mesa driver state.
    *
    * Inactive states are shader states not used by shaders at the moment.
    */
   st->dirty |= ctx->NewDriverState & st->active_states & ST_ALL_STATES_MASK;
   ctx->NewDriverState &= ~st->dirty;

   /* Get pipeline state. */
   switch (pipeline) {
   case ST_PIPELINE_RENDER:
   case ST_PIPELINE_RENDER_NO_VARRAYS:
      if (st->ctx->API == API_OPENGL_COMPAT)
         check_attrib_edgeflag(st);

      if (pipeline == ST_PIPELINE_RENDER)
         pipeline_mask = ST_PIPELINE_RENDER_STATE_MASK;
      else
         pipeline_mask = ST_PIPELINE_RENDER_STATE_MASK_NO_VARRAYS;
      break;

   case ST_PIPELINE_CLEAR:
      pipeline_mask = ST_PIPELINE_CLEAR_STATE_MASK;
      break;

   case ST_PIPELINE_META:
      pipeline_mask = ST_PIPELINE_META_STATE_MASK;
      break;

   case ST_PIPELINE_UPDATE_FRAMEBUFFER:
      pipeline_mask = ST_PIPELINE_UPDATE_FB_STATE_MASK;
      break;

   case ST_PIPELINE_COMPUTE: {
      /*
       * We add the ST_NEW_FB_STATE bit here as well, because glBindFramebuffer
       * acts as a barrier that breaks feedback loops between the framebuffer
       * and textures bound to the framebuffer, even when those textures are
       * accessed by compute shaders; so we must inform the driver of new
       * framebuffer state.
       */
      pipeline_mask = ST_PIPELINE_COMPUTE_STATE_MASK | ST_NEW_FB_STATE;
      break;
   }

   default:
      unreachable("Invalid pipeline specified");
   }

   dirty = st->dirty & pipeline_mask;
   if (!dirty)
      return;

   dirty_lo = dirty;
   dirty_hi = dirty >> 32;

   /* Update states.
    *
    * Don't use u_bit_scan64, it may be slower on 32-bit.
    */
   while (dirty_lo)
      update_functions[u_bit_scan(&dirty_lo)](st);
   while (dirty_hi)
      update_functions[32 + u_bit_scan(&dirty_hi)](st);

   /* Clear the render or compute state bits. */
   st->dirty &= ~pipeline_mask;
}
