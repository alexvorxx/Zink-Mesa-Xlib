/*
 * Copyright © 2023 Intel Corporation
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

#include "anv_private.h"

#include "drm-uapi/i915_drm.h"

static uint32_t
i915_gem_create(struct anv_device *device,
                const struct intel_memory_class_instance **regions,
                uint16_t num_regions, uint64_t size,
                enum anv_bo_alloc_flags alloc_flags)
{
   if (unlikely(!device->info->mem.use_class_instance)) {
      assert(num_regions == 1 &&
             device->physical->sys.region == regions[0]);

      struct drm_i915_gem_create gem_create = {
            .size = size,
      };
      if (intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_CREATE, &gem_create))
         return 0;
      return gem_create.handle;
   }

   struct drm_i915_gem_memory_class_instance i915_regions[2];
   assert(num_regions <= ARRAY_SIZE(i915_regions));

   for (uint16_t i = 0; i < num_regions; i++) {
      i915_regions[i].memory_class = regions[i]->klass;
      i915_regions[i].memory_instance = regions[i]->instance;
   }

   uint32_t flags = 0;
   if (alloc_flags & (ANV_BO_ALLOC_MAPPED | ANV_BO_ALLOC_LOCAL_MEM_CPU_VISIBLE) &&
       !(alloc_flags & ANV_BO_ALLOC_NO_LOCAL_MEM))
      if (device->physical->vram_non_mappable.size > 0)
         flags |= I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS;

   struct drm_i915_gem_create_ext_memory_regions ext_regions = {
      .base = { .name = I915_GEM_CREATE_EXT_MEMORY_REGIONS },
      .num_regions = num_regions,
      .regions = (uintptr_t)i915_regions,
   };
   struct drm_i915_gem_create_ext gem_create = {
      .size = size,
      .extensions = (uintptr_t) &ext_regions,
      .flags = flags,
   };

   if (intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_CREATE_EXT, &gem_create))
      return 0;

   return gem_create.handle;
}

const struct anv_kmd_backend *
anv_i915_kmd_backend_get(void)
{
   static const struct anv_kmd_backend i915_backend = {
      .gem_create = i915_gem_create,
   };
   return &i915_backend;
}
