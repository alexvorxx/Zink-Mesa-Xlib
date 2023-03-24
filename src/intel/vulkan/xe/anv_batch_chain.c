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

#include "xe/anv_batch_chain.h"

#include "anv_private.h"

#include <xf86drm.h>

#include "drm-uapi/xe_drm.h"

VkResult
xe_execute_simple_batch(struct anv_queue *queue, struct anv_bo *batch_bo,
                        uint32_t batch_bo_size)
{
   struct anv_device *device = queue->device;
   VkResult result = VK_SUCCESS;
   uint32_t syncobj_handle;

   if (drmSyncobjCreate(device->fd, 0, &syncobj_handle))
      return vk_errorf(device, VK_ERROR_UNKNOWN, "Unable to create sync obj");

   struct drm_xe_sync sync = {
      .flags = DRM_XE_SYNC_SYNCOBJ | DRM_XE_SYNC_SIGNAL,
      .handle = syncobj_handle,
   };
   struct drm_xe_exec exec = {
      .engine_id = queue->engine_id,
      .num_batch_buffer = 1,
      .address = batch_bo->offset,
      .num_syncs = 1,
      .syncs = (uintptr_t)&sync,
   };

   if (intel_ioctl(device->fd, DRM_IOCTL_XE_EXEC, &exec)) {
      result = vk_device_set_lost(&device->vk, "XE_EXEC failed: %m");
      goto exec_error;
   }

   struct drm_syncobj_wait wait = {
      .handles = (uintptr_t)&syncobj_handle,
      .timeout_nsec = INT64_MAX,
      .count_handles = 1,
   };
   if (intel_ioctl(device->fd, DRM_IOCTL_SYNCOBJ_WAIT, &wait))
      result = vk_device_set_lost(&device->vk, "DRM_IOCTL_SYNCOBJ_WAIT failed: %m");

exec_error:
   drmSyncobjDestroy(device->fd, syncobj_handle);

   return result;
}
