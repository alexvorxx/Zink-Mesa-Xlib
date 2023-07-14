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
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "i915/anv_queue.h"

#include "anv_private.h"

#include "common/i915/intel_engine.h"
#include "common/intel_gem.h"

#include "i915/anv_device.h"

#include "drm-uapi/i915_drm.h"

VkResult
anv_i915_create_engine(struct anv_device *device,
                       struct anv_queue *queue,
                       const VkDeviceQueueCreateInfo *pCreateInfo)
{
   struct anv_physical_device *physical = device->physical;
   struct anv_queue_family *queue_family =
      &physical->queue.families[pCreateInfo->queueFamilyIndex];

   if (device->physical->engine_info == NULL) {
      switch (queue_family->engine_class) {
      case INTEL_ENGINE_CLASS_COPY:
         queue->exec_flags = I915_EXEC_BLT;
         break;
      case INTEL_ENGINE_CLASS_RENDER:
         queue->exec_flags = I915_EXEC_RENDER;
         break;
      case INTEL_ENGINE_CLASS_VIDEO:
         /* We want VCS0 (with ring1) for HW lacking HEVC on VCS1. */
         queue->exec_flags = I915_EXEC_BSD | I915_EXEC_BSD_RING1;
         break;
      default:
         unreachable("Unsupported legacy engine");
      }
   } else {
      /* When using the new engine creation uAPI, the exec_flags value is the
       * index of the engine in the group specified at GEM context creation.
       */
      queue->exec_flags = device->queue_count;
   }

   return VK_SUCCESS;
}

void
anv_i915_destroy_engine(struct anv_device *device, struct anv_queue *queue)
{
   /* NO-OP */
}
