/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "geometry.h"

static uint
align(uint x, uint y)
{
   return (x + y - 1) & ~(y - 1);
}

/* Compatible with util/u_math.h */
static inline uint
util_logbase2_ceil(uint n)
{
   if (n <= 1)
      return 0;
   else
      return 32 - clz(n - 1);
}

/* Swap the two non-provoking vertices third vert in odd triangles. This
 * generates a vertex ID list with a consistent winding order.
 *
 * With prim and flatshade_first, the map : [0, 1, 2] -> [0, 1, 2] is its own
 * inverse. This lets us reuse it for both vertex fetch and transform feedback.
 */
uint
libagx_map_vertex_in_tri_strip(uint prim, uint vert, bool flatshade_first)
{
   unsigned pv = flatshade_first ? 0 : 2;

   bool even = (prim & 1) == 0;
   bool provoking = vert == pv;

   return (provoking || even) ? vert : ((3 - pv) - vert);
}

uint64_t
libagx_xfb_vertex_address(global struct agx_geometry_params *p, uint base_index,
                          uint vert, uint buffer, uint stride,
                          uint output_offset)
{
   uint index = base_index + vert;
   uint xfb_offset = (index * stride) + output_offset;

   return (uintptr_t)(p->xfb_base[buffer]) + xfb_offset;
}

uint
libagx_vertex_id_for_line_loop(uint prim, uint vert, uint num_prims)
{
   /* (0, 1), (1, 2), (2, 0) */
   if (prim == (num_prims - 1) && vert == 1)
      return 0;
   else
      return prim + vert;
}

uint
libagx_vertex_id_for_line_class(enum mesa_prim mode, uint prim, uint vert,
                                uint num_prims)
{
   /* Line list, line strip, or line loop */
   if (mode == MESA_PRIM_LINE_LOOP && prim == (num_prims - 1) && vert == 1)
      return 0;

   if (mode == MESA_PRIM_LINES)
      prim *= 2;

   return prim + vert;
}

uint
libagx_vertex_id_for_tri_fan(uint prim, uint vert, bool flatshade_first)
{
   /* Vulkan spec section 20.1.7 gives (i + 1, i + 2, 0) for a provoking
    * first. OpenGL instead wants (0, i + 1, i + 2) with a provoking last.
    * Piglit clipflat expects us to switch between these orders depending on
    * provoking vertex, to avoid trivializing the fan.
    *
    * Rotate accordingly.
    */
   if (flatshade_first) {
      vert = vert + 1;
      vert = (vert == 2) ? 0 : vert;
   }

   /* The simpler form assuming last is provoking. */
   return (vert == 0) ? 0 : prim + vert;
}

uint
libagx_vertex_id_for_tri_class(enum mesa_prim mode, uint prim, uint vert,
                               bool flatshade_first)
{
   if (flatshade_first && mode == MESA_PRIM_TRIANGLE_FAN) {
      vert = vert + 1;
      vert = (vert == 3) ? 0 : vert;
   }

   if (mode == MESA_PRIM_TRIANGLE_FAN && vert == 0)
      return 0;

   if (mode == MESA_PRIM_TRIANGLES)
      prim *= 3;

   /* Triangle list, triangle strip, or triangle fan */
   if (mode == MESA_PRIM_TRIANGLE_STRIP) {
      unsigned pv = flatshade_first ? 0 : 2;

      bool even = (prim & 1) == 0;
      bool provoking = vert == pv;

      vert = ((provoking || even) ? vert : ((3 - pv) - vert));
   }

   return prim + vert;
}

uint
libagx_vertex_id_for_line_adj_class(enum mesa_prim mode, uint prim, uint vert)
{
   /* Line list adj or line strip adj */
   if (mode == MESA_PRIM_LINES_ADJACENCY)
      prim *= 4;

   return prim + vert;
}

uint
libagx_vertex_id_for_tri_strip_adj(uint prim, uint vert, uint num_prims,
                                   bool flatshade_first)
{
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

   /* Ensure NIR can see thru the local array */
   uint offset = 0;
   for (uint i = 1; i < 6; ++i) {
      if (i == vert)
         offset = offsets[i];
   }

   /* Finally add to the base of the primitive */
   return (prim * 2) + offset;
}

uint
libagx_vertex_id_for_tri_adj_class(enum mesa_prim mode, uint prim, uint vert,
                                   uint nr, bool flatshade_first)
{
   /* Tri adj list or tri adj strip */
   if (mode == MESA_PRIM_TRIANGLE_STRIP_ADJACENCY) {
      return libagx_vertex_id_for_tri_strip_adj(prim, vert, nr,
                                                flatshade_first);
   } else {
      return (6 * prim) + vert;
   }
}

uint
libagx_vertex_id_for_topology(enum mesa_prim mode, bool flatshade_first,
                              uint prim, uint vert, uint num_prims)
{
   switch (mode) {
   case MESA_PRIM_POINTS:
   case MESA_PRIM_LINES:
   case MESA_PRIM_TRIANGLES:
   case MESA_PRIM_LINES_ADJACENCY:
   case MESA_PRIM_TRIANGLES_ADJACENCY:
      /* Regular primitive: every N vertices defines a primitive */
      return (prim * mesa_vertices_per_prim(mode)) + vert;

   case MESA_PRIM_LINE_LOOP:
      return libagx_vertex_id_for_line_loop(prim, vert, num_prims);

   case MESA_PRIM_LINE_STRIP:
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      /* (i, i + 1) or (i, ..., i + 3) */
      return prim + vert;

   case MESA_PRIM_TRIANGLE_STRIP: {
      /* Order depends on the provoking vert.
       *
       * First: (0, 1, 2), (1, 3, 2), (2, 3, 4).
       * Last:  (0, 1, 2), (2, 1, 3), (2, 3, 4).
       *
       * Pull the (maybe swapped) vert from the corresponding primitive
       */
      return prim + libagx_map_vertex_in_tri_strip(prim, vert, flatshade_first);
   }

   case MESA_PRIM_TRIANGLE_FAN:
      return libagx_vertex_id_for_tri_fan(prim, vert, flatshade_first);

   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return libagx_vertex_id_for_tri_strip_adj(prim, vert, num_prims,
                                                flatshade_first);

   default:
      return 0;
   }
}

/*
 * When unrolling the index buffer for a draw, we translate the old indirect
 * draws to new indirect draws. This routine allocates the new index buffer and
 * sets up most of the new draw descriptor.
 */
static global void *
setup_unroll_for_draw(global struct agx_ia_state *ia, constant uint *in_draw,
                      uint draw, enum mesa_prim mode, uint index_size_B)
{
   /* Determine an upper bound on the memory required for the index buffer.
    * Restarts only decrease the unrolled index buffer size, so the maximum size
    * is the unrolled size when the input has no restarts.
    */
   uint max_prims = u_decomposed_prims_for_vertices(mode, in_draw[0]);
   uint max_verts = max_prims * mesa_vertices_per_prim(mode);
   uint alloc_size = max_verts * index_size_B;

   /* Allocate memory from the heap for the unrolled index buffer. Use an atomic
    * since multiple threads may be running to handle multidraw in parallel.
    */
   global struct agx_geometry_state *heap = ia->heap;
   uint old_heap_bottom = atomic_fetch_add(
      (volatile atomic_uint *)(&heap->heap_bottom), align(alloc_size, 4));

   /* Regardless of the input stride, we use tightly packed output draws */
   global uint *out = &ia->out_draws[5 * draw];

   /* Setup most of the descriptor. Count will be determined after unroll. */
   out[1] = in_draw[1];                     /* instance count */
   out[2] = old_heap_bottom / index_size_B; /* index offset */
   out[3] = in_draw[3];                     /* index bias */
   out[4] = in_draw[4];                     /* base instance */

   /* Return the index buffer we allocated */
   return (global uchar *)heap->heap + (old_heap_bottom * index_size_B);
}

#define UNROLL(INDEX, suffix)                                                  \
   void libagx_unroll_restart_##suffix(global struct agx_ia_state *ia,         \
                                       enum mesa_prim mode, uint draw)         \
   {                                                                           \
      /* For an indirect multidraw, we are dispatched maxDraws times and       \
       * terminate trailing invocations.                                       \
       */                                                                      \
      if (ia->count && draw >= *(ia->count))                                   \
         return;                                                               \
                                                                               \
      constant uint *in_draw =                                                 \
         (constant uint *)(ia->draws + (draw * ia->draw_stride));              \
                                                                               \
      uint count = in_draw[0];                                                 \
      constant INDEX *in = (constant INDEX *)ia->index_buffer;                 \
      in += in_draw[2];                                                        \
                                                                               \
      global INDEX *out =                                                      \
         setup_unroll_for_draw(ia, in_draw, draw, mode, sizeof(INDEX));        \
                                                                               \
      uint out_prims = 0;                                                      \
      INDEX restart_idx = ia->restart_index;                                   \
      bool flatshade_first = ia->flatshade_first;                              \
      uint in_size_el = ia->index_buffer_size_B / sizeof(INDEX);               \
                                                                               \
      uint needle = 0;                                                         \
      uint per_prim = mesa_vertices_per_prim(mode);                            \
      while (needle < count) {                                                 \
         /* Search for next restart or the end */                              \
         uint next_restart = needle;                                           \
         while ((next_restart < count) && in[next_restart] != restart_idx)     \
            ++next_restart;                                                    \
                                                                               \
         /* Emit up to the next restart */                                     \
         uint subcount = next_restart - needle;                                \
         uint subprims = u_decomposed_prims_for_vertices(mode, subcount);      \
         for (uint i = 0; i < subprims; ++i) {                                 \
            for (uint vtx = 0; vtx < per_prim; ++vtx) {                        \
               uint id = libagx_vertex_id_for_topology(mode, flatshade_first,  \
                                                       i, vtx, subprims);      \
               uint offset = needle + id;                                      \
                                                                               \
               out[(out_prims * per_prim) + vtx] =                             \
                  offset < in_size_el ? in[offset] : 0;                        \
            }                                                                  \
                                                                               \
            out_prims++;                                                       \
         }                                                                     \
                                                                               \
         needle = next_restart + 1;                                            \
      }                                                                        \
                                                                               \
      ia->out_draws[(5 * draw) + 0] = out_prims * per_prim;                    \
   }

UNROLL(uchar, u8)
UNROLL(ushort, u16)
UNROLL(uint, u32)

uintptr_t
libagx_index_buffer(constant struct agx_ia_state *p, uint id,
                    uint index_size)
{
   return (uintptr_t)&p->index_buffer[id * index_size];
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
 *
 * For points, we write index buffers without restart, just as a sideband to
 * pass data into the vertex shader.
 */
void
libagx_end_primitive(global int *index_buffer, uint total_verts,
                     uint verts_in_prim, uint total_prims,
                     uint invocation_vertex_base, uint invocation_prim_base,
                     uint geometry_base, bool restart)
{
   /* Previous verts/prims are from previous invocations plus earlier
    * prims in this invocation. For the intra-invocation counts, we
    * subtract the count for this prim from the inclusive sum NIR gives us.
    */
   uint previous_verts_in_invoc = (total_verts - verts_in_prim);
   uint previous_verts = invocation_vertex_base + previous_verts_in_invoc;
   uint previous_prims = restart ? invocation_prim_base + (total_prims - 1) : 0;

   /* The indices are encoded as: (unrolled ID * output vertices) + vertex. */
   uint index_base = geometry_base + previous_verts_in_invoc;

   /* Index buffer contains 1 index for each vertex and 1 for each prim */
   global int *out = &index_buffer[previous_verts + previous_prims];

   /* Write out indices for the strip */
   for (uint i = 0; i < verts_in_prim; ++i) {
      out[i] = index_base + i;
   }

   if (restart)
      out[verts_in_prim] = -1;
}

void
libagx_build_gs_draw(global struct agx_geometry_params *p, bool indexed,
                     uint vertices, uint primitives)
{
   global uint *descriptor = p->indirect_desc;
   global struct agx_geometry_state *state = p->state;

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

   if (state->heap_bottom > 1024 * 1024 * 128) {
      global uint *foo = (global uint *)(uintptr_t)0xdeadbeef;
      *foo = 0x1234;
   }
}

void
libagx_gs_setup_indirect(global struct agx_geometry_params *p,
                         global struct agx_ia_state *ia, enum mesa_prim mode,
                         uint local_id)
{
   global uint *in_draw = (global uint *)ia->draws;

   /* Determine the (primitives, instances) grid size. */
   uint vertex_count = in_draw[0];
   uint instance_count = in_draw[1];

   /* Calculate number of primitives input into the GS */
   uint prim_per_instance = u_decomposed_prims_for_vertices(mode, vertex_count);
   p->input_primitives = prim_per_instance * instance_count;
   p->input_vertices = vertex_count;

   /* Invoke VS as (vertices, instances, 1); GS as (primitives, instances, 1) */
   p->vs_grid[0] = vertex_count;
   p->vs_grid[1] = instance_count;
   p->vs_grid[2] = 1;

   p->gs_grid[0] = prim_per_instance;
   p->gs_grid[1] = instance_count;
   p->gs_grid[2] = 1;

   p->primitives_log2 = util_logbase2_ceil(prim_per_instance);

   /* If indexing is enabled, the third word is the offset into the index buffer
    * in elements. Apply that offset now that we have it. For a hardware
    * indirect draw, the hardware would do this for us, but for software input
    * assembly we need to do it ourselves.
    */
   if (ia->index_buffer) {
      ia->index_buffer += ((constant uint *)ia->draws)[2] * ia->index_size_B;
   }

   /* We may need to allocate VS and GS count buffers, do so now */
   global struct agx_geometry_state *state = p->state;

   uint vertex_buffer_size =
      libagx_tcs_in_size(vertex_count * instance_count, p->vs_outputs);

   p->count_buffer = (global uint *)(state->heap + state->heap_bottom);
   state->heap_bottom +=
      align(p->input_primitives * p->count_buffer_stride, 16);

   p->vertex_buffer = (global uint *)(state->heap + state->heap_bottom);
   state->heap_bottom += align(vertex_buffer_size, 4);
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

bool
libagx_is_provoking_last(global struct agx_ia_state *ia)
{
   return !ia->flatshade_first;
}

uintptr_t
libagx_vertex_output_address(constant struct agx_geometry_params *p, uint vtx,
                             gl_varying_slot location, uint64_t vs_outputs)
{
   return (uintptr_t)p->vertex_buffer +
          libagx_tcs_in_offs(vtx, location, vs_outputs);
}
