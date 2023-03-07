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
#include "iris/iris_kmd_backend.h"

#include "common/intel_gem.h"

#include "drm-uapi/i915_drm.h"

#include "iris/iris_bufmgr.h"

static uint32_t
i915_gem_create(struct iris_bufmgr *bufmgr,
                const struct intel_memory_class_instance **regions,
                uint16_t regions_count, uint64_t size,
                enum iris_heap heap_flags, unsigned alloc_flags)
{
   if (unlikely(!iris_bufmgr_get_device_info(bufmgr)->mem.use_class_instance)) {
      struct drm_i915_gem_create create_legacy = { .size = size };

      assert(regions_count == 1 &&
             regions[0]->klass == I915_MEMORY_CLASS_SYSTEM);

      /* All new BOs we get from the kernel are zeroed, so we don't need to
       * worry about that here.
       */
      if (intel_ioctl(iris_bufmgr_get_fd(bufmgr), DRM_IOCTL_I915_GEM_CREATE,
                      &create_legacy))
         return 0;

      return create_legacy.handle;
   }

   struct drm_i915_gem_memory_class_instance i915_regions[2];
   assert(regions_count <= ARRAY_SIZE(i915_regions));
   for (uint16_t i = 0; i < regions_count; i++) {
      i915_regions[i].memory_class = regions[i]->klass;
      i915_regions[i].memory_instance = regions[i]->instance;
   }

   struct drm_i915_gem_create_ext create = {
      .size = size,
   };
   struct drm_i915_gem_create_ext_memory_regions ext_regions = {
      .base = { .name = I915_GEM_CREATE_EXT_MEMORY_REGIONS },
      .num_regions = regions_count,
      .regions = (uintptr_t)i915_regions,
   };
   intel_gem_add_ext(&create.extensions,
                     I915_GEM_CREATE_EXT_MEMORY_REGIONS,
                     &ext_regions.base);

   if (iris_bufmgr_vram_size(bufmgr) > 0 &&
       !intel_vram_all_mappable(iris_bufmgr_get_device_info(bufmgr)) &&
       heap_flags == IRIS_HEAP_DEVICE_LOCAL_PREFERRED)
      create.flags |= I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS;

   /* Protected param */
   struct drm_i915_gem_create_ext_protected_content protected_param = {
      .flags = 0,
   };
   if (alloc_flags & BO_ALLOC_PROTECTED) {
      intel_gem_add_ext(&create.extensions,
                        I915_GEM_CREATE_EXT_PROTECTED_CONTENT,
                        &protected_param.base);
   }

   if (intel_ioctl(iris_bufmgr_get_fd(bufmgr), DRM_IOCTL_I915_GEM_CREATE_EXT,
                   &create))
      return 0;

   return create.handle;
}

static bool
i915_bo_madvise(struct iris_bo *bo, enum iris_madvice state)
{
   uint32_t i915_state = state == IRIS_MADVICE_WILL_NEED ?
                                  I915_MADV_WILLNEED : I915_MADV_DONTNEED;
   struct drm_i915_gem_madvise madv = {
      .handle = bo->gem_handle,
      .madv = i915_state,
      .retained = 1,
   };

   intel_ioctl(iris_bufmgr_get_fd(bo->bufmgr), DRM_IOCTL_I915_GEM_MADVISE, &madv);

   return madv.retained;
}

static int
i915_bo_set_caching(struct iris_bo *bo, bool cached)
{
   struct drm_i915_gem_caching arg = {
      .handle = bo->gem_handle,
      .caching = cached ? I915_CACHING_CACHED : I915_CACHING_NONE,
   };
   return intel_ioctl(iris_bufmgr_get_fd(bo->bufmgr),
                      DRM_IOCTL_I915_GEM_SET_CACHING, &arg);
}

const struct iris_kmd_backend *i915_get_backend(void)
{
   static const struct iris_kmd_backend i915_backend = {
      .gem_create = i915_gem_create,
      .bo_madvise = i915_bo_madvise,
      .bo_set_caching = i915_bo_set_caching,
   };
   return &i915_backend;
}
