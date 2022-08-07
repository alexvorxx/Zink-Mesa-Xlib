/*
 * Copyright © 2022 Konstantin Seurer
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

#ifndef BVH_INTERNAL_H
#define BVH_INTERNAL_H

#include "build_helpers.h"

struct internal_kernel_args {
   VOID_REF bvh;
   REF(key_id_pair) src_ids;
   REF(key_id_pair) dst_ids;
   uint32_t dst_offset;
   uint32_t fill_count;
};
TYPE(internal_kernel_args, 32);

void
internal_kernel(internal_kernel_args args, uint32_t global_id)
{
   bool fill_header = (args.fill_count & 0x80000000u) != 0;
   uint32_t src_count = args.fill_count & 0x7FFFFFFFu;

   uint32_t src_index = global_id * 4;
   uint32_t child_count = min(src_count - src_index, 4);

   uint32_t dst_offset = args.dst_offset + global_id * SIZEOF(radv_bvh_box32_node);

   REF(radv_bvh_box32_node) dst_node = REF(radv_bvh_box32_node)(OFFSET(args.bvh, dst_offset));

   AABB total_bounds;
   total_bounds.min = vec3(INFINITY);
   total_bounds.max = vec3(-INFINITY);

   for (uint32_t i = 0; i < 4; i++) {
      AABB bounds;
      bounds.min = vec3(NAN);
      bounds.max = vec3(NAN);

      uint32_t child_id = DEREF(INDEX(key_id_pair, args.src_ids, src_index + i)).id;

      if (i < child_count) {
         DEREF(dst_node).children[i] = child_id;

         bounds = calculate_node_bounds(args.bvh, child_id);
         total_bounds.min = min(total_bounds.min, bounds.min);
         total_bounds.max = max(total_bounds.max, bounds.max);
      }

      DEREF(dst_node).coords[i][0][0] = bounds.min.x;
      DEREF(dst_node).coords[i][0][1] = bounds.min.y;
      DEREF(dst_node).coords[i][0][2] = bounds.min.z;
      DEREF(dst_node).coords[i][1][0] = bounds.max.x;
      DEREF(dst_node).coords[i][1][1] = bounds.max.y;
      DEREF(dst_node).coords[i][1][2] = bounds.max.z;
   }

   uint32_t node_id = pack_node_id(dst_offset, radv_bvh_node_internal);
   DEREF(INDEX(key_id_pair, args.dst_ids, global_id)).id = node_id;

   if (fill_header) {
      REF(radv_accel_struct_header) header = REF(radv_accel_struct_header)(args.bvh);
      DEREF(header).root_node_offset = node_id;

      DEREF(header).aabb[0][0] = total_bounds.min.x;
      DEREF(header).aabb[0][1] = total_bounds.min.y;
      DEREF(header).aabb[0][2] = total_bounds.min.z;
      DEREF(header).aabb[1][0] = total_bounds.max.x;
      DEREF(header).aabb[1][1] = total_bounds.max.y;
      DEREF(header).aabb[1][2] = total_bounds.max.z;
   }
}

#endif
