/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "geometry.h"

/* TODO: Primitive restart */
uint
libagx_vertex_id_for_topology(enum mesa_prim mode, bool flatshade_first,
                              uint prim, uint vert, uint num_prims)
{
   switch (mode) {
   case MESA_PRIM_POINTS:
   case MESA_PRIM_LINES:
   case MESA_PRIM_TRIANGLES:
   case MESA_PRIM_LINES_ADJACENCY:
   case MESA_PRIM_TRIANGLES_ADJACENCY: {
      /* Regular primitive: every N vertices defines a primitive */
      return (prim * mesa_vertices_per_prim(mode)) + vert;
   }

   case MESA_PRIM_LINE_LOOP: {
      /* (0, 1), (1, 2), (2, 0) */
      if (prim == (num_prims - 1) && vert == 1)
         return 0;
      else
         return prim + vert;
   }

   case MESA_PRIM_TRIANGLE_STRIP: {
      /* Order depends on the provoking vert.
       *
       * First: (0, 1, 2), (1, 3, 2), (2, 3, 4).
       * Last:  (0, 1, 2), (2, 1, 3), (2, 3, 4).
       */
      unsigned pv = flatshade_first ? 0 : 2;

      /* Swap the two non-provoking vertices third vert in odd triangles */
      bool even = (prim & 1) == 0;
      bool provoking = vert == pv;
      uint off = (provoking || even) ? vert : ((3 - pv) - vert);

      /* Pull the (maybe swapped) vert from the corresponding primitive */
      return prim + off;
   }

   case MESA_PRIM_TRIANGLE_FAN: {
      if (vert == 0)
         return 0;
      else
         return prim + vert;
   }

   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY: {
      /* See Vulkan spec section 20.1.11 "Triangle Strips With Adjancency".
       *
       * There are different cases for first/middle/last/only primitives and for
       * odd/even primitives.  Determine which case we're in.
       */
      bool last = prim == (num_prims - 1);
      bool first = prim == 0;
      bool even = (prim & 1) == 0;
      bool even_or_first = even || first;

      /* When the last vertex is provoking, we rotate the primitives
       * accordingly. This seems required for OpenGL.
       */
      if (!flatshade_first && !even_or_first) {
         vert = (vert + 4u) % 6u;
      }

      /* Offsets per the spec. The spec lists 6 cases with 6 offsets. Luckily,
       * there are lots of patterns we can exploit, avoiding a full 6x6 LUT.
       *
       * Here we assume the first vertex is provoking, the Vulkan default.
       */
      uint offsets[6] = {
         0,
         first ? 1 : (even ? -2 : 3),
         even_or_first ? 2 : 4,
         last ? 5 : 6,
         even_or_first ? 4 : 2,
         even_or_first ? 3 : -2,
      };

      /* Finally add to the base of the primitive */
      return (prim * 2) + offsets[vert];
   }

   default:
      /* Invalid */
      return 0;
   }
}

uint
libagx_setup_xfb_buffer(global struct agx_geometry_params *p, uint i)
{
   global uint *off_ptr = p->xfb_offs_ptrs[i];
   if (!off_ptr)
      return 0;

   uint off = *off_ptr;
   p->xfb_base[i] = p->xfb_base_original[i] + off;
   return off;
}

/*
 * Translate EndPrimitive for LINE_STRIP or TRIANGLE_STRIP output prims into
 * writes into the 32-bit output index buffer. We write the sequence (b, b + 1,
 * b + 2, ..., b + n - 1, -1), where b (base) is the first vertex in the prim, n
 * (count) is the number of verts in the prims, and -1 is the prim restart index
 * used to signal the end of the prim.
 */
void
libagx_end_primitive(global int *index_buffer, uint total_verts,
                     uint verts_in_prim, uint total_prims,
                     uint invocation_vertex_base, uint invocation_prim_base)
{
   /* Previous verts/prims are from previous invocations plus earlier
    * prims in this invocation. For the intra-invocation counts, we
    * subtract the count for this prim from the inclusive sum NIR gives us.
    */
   uint previous_verts = invocation_vertex_base + (total_verts - verts_in_prim);
   uint previous_prims = invocation_prim_base + (total_prims - 1);

   /* Index buffer contains 1 index for each vertex and 1 for each prim */
   global int *out = &index_buffer[previous_verts + previous_prims];

   /* Write out indices for the strip */
   for (uint i = 0; i < verts_in_prim; ++i) {
      out[i] = previous_verts + i;
   }

   out[verts_in_prim] = -1;
}

static uint
align(uint x, uint y)
{
   return (x + 1) & ~(y - 1);
}

void
libagx_build_gs_draw(global struct agx_geometry_params *p, bool indexed,
                     uint vertices, uint primitives, uint output_stride_B)
{
   global uint *descriptor = p->indirect_desc;
   global struct agx_geometry_state *state = p->state;

   /* Allocate the output buffer (per vertex) */
   p->output_buffer = (global uint *)(state->heap + state->heap_bottom);
   state->heap_bottom += align(vertices * output_stride_B, 4);

   /* Setup the indirect draw descriptor */
   if (indexed) {
      uint indices = vertices + primitives; /* includes restart indices */

      /* Allocate the index buffer */
      uint index_buffer_offset_B = state->heap_bottom;
      p->output_index_buffer =
         (global uint *)(state->heap + index_buffer_offset_B);
      state->heap_bottom += (indices * 4);

      descriptor[0] = indices;                   /* count */
      descriptor[1] = 1;                         /* instance count */
      descriptor[2] = index_buffer_offset_B / 4; /* start */
      descriptor[3] = 0;                         /* index bias */
      descriptor[4] = 0;                         /* start instance */
   } else {
      descriptor[0] = vertices; /* count */
      descriptor[1] = 1;        /* instance count */
      descriptor[2] = 0;        /* start */
      descriptor[3] = 0;        /* start instance */
   }

   if (state->heap_bottom > 1024 * 1024) {
      global uint *foo = (global uint *)(uintptr_t)0xdeadbeef;
      *foo = 0x1234;
   }
}

void
libagx_gs_setup_indirect(global struct agx_geometry_params *p,
                         enum mesa_prim mode)
{
   /* Regardless of indexing being enabled, this holds */
   uint vertex_count = p->input_indirect_desc[0];
   uint instance_count = p->input_indirect_desc[1];

   uint prim_per_instance = u_decomposed_prims_for_vertices(mode, vertex_count);
   p->input_primitives = prim_per_instance * instance_count;

   p->gs_grid[0] = prim_per_instance;
   p->gs_grid[1] = instance_count;
   p->gs_grid[2] = 1;

   /* If indexing is enabled, the third word is the offset into the index buffer
    * in elements. Apply that offset now that we have it. For a hardware
    * indirect draw, the hardware would do this for us, but for software input
    * assembly we need to do it ourselves.
    */
   if (p->input_index_buffer) {
      p->input_index_buffer += p->input_indirect_desc[2] * p->index_size_B;
   }

   /* We may need to allocate a GS count buffer, do so now */
   global struct agx_geometry_state *state = p->state;
   p->count_buffer = (global uint *)(state->heap + state->heap_bottom);
   state->heap_bottom += align(p->input_primitives * p->count_buffer_stride, 4);
}

void
libagx_prefix_sum(global uint *buffer, uint len, uint words, uint2 local_id)
{
   /* Main loop: complete subgroups processing 32 values at once
    *
    * TODO: Don't do a serial bottleneck! This is bad!
    */
   uint i, count = 0;
   uint len_remainder = len % 32;
   uint len_rounded_down = len - len_remainder;

   for (i = local_id.x; i < len_rounded_down; i += 32) {
      global uint *ptr = &buffer[(i * words) + local_id.y];
      uint value = *ptr;

      /* TODO: use inclusive once that's wired up */
      uint value_prefix_sum = sub_group_scan_exclusive_add(value) + value;
      *ptr = count + value_prefix_sum;

      /* Advance count by the reduction sum of all processed values. We already
       * have that sum calculated in the last lane. We know that lane is active,
       * since all control flow is uniform except in the last iteration.
       */
      count += sub_group_broadcast(value_prefix_sum, 31);
   }

   /* The last iteration is special since we won't have a full subgroup unless
    * the length is divisible by the subgroup size, and we don't advance count.
    */
   if (local_id.x < len_remainder) {
      global uint *ptr = &buffer[(i * words) + local_id.y];
      uint value = *ptr;

      /* TODO: use inclusive once that's wired up */
      *ptr = count + sub_group_scan_exclusive_add(value) + value;
   }
}
