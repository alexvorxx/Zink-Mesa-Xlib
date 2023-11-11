/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/shader_enums.h"
#include "libagx.h"

#ifndef __OPENCL_VERSION__
#include "util/macros.h"
#define GLOBAL(type_) uint64_t
#else
#define PACKED
#define GLOBAL(type_) global type_ *
#endif

#ifndef LIBAGX_GEOMETRY_H
#define LIBAGX_GEOMETRY_H

#define MAX_SO_BUFFERS     4
#define MAX_VERTEX_STREAMS 4

struct agx_ia_key {
   /* The index size (1, 2, 4) or 0 if drawing without an index buffer. */
   uint8_t index_size;

   /* The primitive mode for unrolling the vertex ID */
   enum mesa_prim mode;

   /* Use first vertex as the provoking vertex for flat shading */
   bool flatshade_first;
};

/* Packed geometry state buffer */
struct agx_geometry_state {
   /* Heap to allocate from, in either direction. By convention, the top is used
    * for intra-draw allocations and the bottom is used for full-batch
    * allocations. In the future we could use kernel support to improve this.
    */
   GLOBAL(uchar) heap;
   uint32_t heap_bottom, heap_top, heap_size, padding;
} PACKED;

struct agx_geometry_params {
   /* Persistent (cross-draw) geometry state */
   GLOBAL(struct agx_geometry_state) state;

   /* Address of associated indirect draw buffer */
   GLOBAL(uint) indirect_desc;

   /* Address of count buffer. For an indirect draw, this will be written by the
    * indirect setup kernel.
    */
   GLOBAL(uint) count_buffer;

   /* Address of the primitives generated counters */
   GLOBAL(uint) prims_generated_counter[MAX_VERTEX_STREAMS];
   GLOBAL(uint) xfb_prims_generated_counter[MAX_VERTEX_STREAMS];

   /* Pointers to transform feedback buffer offsets in bytes */
   GLOBAL(uint) xfb_offs_ptrs[MAX_SO_BUFFERS];

   /* Output (vertex) buffer, allocated by pre-GS. */
   GLOBAL(uint) output_buffer;

   /* Output index buffer, allocated by pre-GS. */
   GLOBAL(uint) output_index_buffer;

   /* Address of transform feedback buffer in general, supplied by the CPU. */
   GLOBAL(uchar) xfb_base_original[MAX_SO_BUFFERS];
   uint32_t xfb_size[MAX_SO_BUFFERS];

   /* Address of transform feedback for the current primitive. Written by pre-GS
    * program.
    */
   GLOBAL(uchar) xfb_base[MAX_SO_BUFFERS];

   /* Number of primitives emitted by transform feedback per stream. Written by
    * the pre-GS program.
    */
   uint32_t xfb_prims[MAX_VERTEX_STREAMS];

   /* Address of input index buffer for an indexed draw (this includes
    * tessellation - it's the index buffer coming into the geometry stage).
    */
   GLOBAL(uchar) input_index_buffer;

   /* Address of input indirect buffer for indirect GS draw */
   GLOBAL(uint) input_indirect_desc;

   /* Within an indirect GS draw, the grid used to dispatch the GS written out
    * by the GS indirect setup kernel. Unused for direct GS draws.
    */
   uint32_t gs_grid[3];

   /* Number of input primitives, calculated by the CPU for a direct draw or the
    * GS indirect setup kernel for an indirect draw.
    */
   uint32_t input_primitives;

   /* Number of bytes output by the GS count shader per input primitive (may be
    * 0), written by CPU and consumed by indirect draw setup shader for
    * allocating counts.
    */
   uint32_t count_buffer_stride;

   /* Size of a single input index in bytes, or 0 if indexing is disabled.
    *
    *    index_size_B == 0 <==> input_index_buffer == NULL
    */
   uint32_t index_size_B;
} PACKED;

#endif
