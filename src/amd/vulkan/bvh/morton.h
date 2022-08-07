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

#ifndef BVH_MORTON_H
#define BVH_MORTON_H

#include "build_helpers.h"

uint32_t
morton_component(uint32_t x)
{
   x = (x * 0x00000101u) & 0x0F00F00Fu;
   x = (x * 0x00000011u) & 0xC30C30C3u;
   x = (x * 0x00000005u) & 0x49249249u;
   return x;
}

uint32_t
morton_code(uint32_t x, uint32_t y, uint32_t z)
{
   return (morton_component(x) << 2) | (morton_component(y) << 1) | morton_component(z);
}

uint32_t
lbvh_key(float x01, float y01, float z01)
{
   return morton_code(uint32_t(x01 * 255.0), uint32_t(y01 * 255.0), uint32_t(z01 * 255.0)) << 8;
}

struct morton_kernel_args {
   VOID_REF bvh;
   REF(AABB) bounds;
   REF(key_id_pair) ids;
};
TYPE(morton_kernel_args, 24);

void
morton_kernel(morton_kernel_args args, uint32_t global_id)
{
   REF(key_id_pair) key_id = INDEX(key_id_pair, args.ids, global_id);

   uint32_t id = DEREF(key_id).id;
   AABB bounds = calculate_node_bounds(args.bvh, id);
   vec3 center = (bounds.min + bounds.max) * 0.5;

   AABB bvh_bounds;
   bvh_bounds.min.x = load_minmax_float_emulated(VOID_REF(args.bounds));
   bvh_bounds.min.y = load_minmax_float_emulated(OFFSET(args.bounds, 4));
   bvh_bounds.min.z = load_minmax_float_emulated(OFFSET(args.bounds, 8));
   bvh_bounds.max.x = load_minmax_float_emulated(OFFSET(args.bounds, 12));
   bvh_bounds.max.y = load_minmax_float_emulated(OFFSET(args.bounds, 16));
   bvh_bounds.max.z = load_minmax_float_emulated(OFFSET(args.bounds, 20));

   vec3 normalized_center = (center - bvh_bounds.min) / (bvh_bounds.max - bvh_bounds.min);

   DEREF(key_id).key = lbvh_key(normalized_center.x, normalized_center.y, normalized_center.z);
}

#endif
