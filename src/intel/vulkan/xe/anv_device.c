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

#include "xe/anv_device.h"
#include "anv_private.h"

#include "drm-uapi/xe_drm.h"

bool anv_xe_device_destroy_vm(struct anv_device *device)
{
   struct drm_xe_vm_destroy destroy = {
      .vm_id = device->vm_id,
   };
   return intel_ioctl(device->fd, DRM_IOCTL_XE_VM_DESTROY, &destroy) == 0;
}

VkResult anv_xe_device_setup_vm(struct anv_device *device)
{
   struct drm_xe_vm_create create = {
      .flags = DRM_XE_VM_CREATE_SCRATCH_PAGE,
   };
   if (intel_ioctl(device->fd, DRM_IOCTL_XE_VM_CREATE, &create) != 0)
      return vk_errorf(device, VK_ERROR_INITIALIZATION_FAILED,
                       "vm creation failed");

   device->vm_id = create.vm_id;
   return VK_SUCCESS;
}

VkResult
anv_xe_physical_device_get_parameters(struct anv_physical_device *device)
{
   device->has_exec_timeline = true;
   /* max_context_priority will be updated in
    * anv_xe_physical_device_max_priority_update()
    */
   device->max_context_priority = VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR;

   return VK_SUCCESS;
}

/* TODO: include gpu_scheduler.h and spsc_queue.h and replace hard-coded values */
uint64_t
anv_vk_priority_to_xe(VkQueueGlobalPriorityKHR vk_priority)
{
   switch (vk_priority) {
   case VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR:
      return 0;
   case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR:
      return 1;
   case VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR:
      return 2;
   default:
      unreachable("Invalid priority");
      return 0;
   }
}

void
anv_xe_physical_device_max_priority_update(struct anv_physical_device *device)
{
   if (!device->engine_info->num_engines)
      return;

   struct drm_xe_vm_create create_vm = {};
   if (intel_ioctl(device->local_fd, DRM_IOCTL_XE_VM_CREATE, &create_vm))
      return;

   const VkQueueGlobalPriorityKHR priorities[] = {
      VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR,
      VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR,
      VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR,
   };
   struct drm_xe_engine_destroy destroy_engine;
   struct drm_xe_vm_destroy destroy_vm = {
      .vm_id = create_vm.vm_id,
   };
   struct drm_xe_engine_create create_engine = {
      .instances = (uintptr_t)device->engine_info->engines,
      .width = 1,
      .num_placements = 1,
      .vm_id = create_vm.vm_id,
   };
   if (intel_ioctl(device->local_fd, DRM_IOCTL_XE_ENGINE_CREATE,
                   &create_engine))
      goto destroy_vm;

   for (unsigned i = 0; i < ARRAY_SIZE(priorities); i++) {
      struct drm_xe_engine_set_property engine_property = {
         .engine_id = create_engine.engine_id,
         .property = XE_ENGINE_SET_PROPERTY_PRIORITY,
         engine_property.value = anv_vk_priority_to_xe(priorities[i]),
      };
      if (intel_ioctl(device->local_fd, DRM_IOCTL_XE_ENGINE_SET_PROPERTY,
                      &engine_property))
         break;
      device->max_context_priority = priorities[i];
   }

   destroy_engine.engine_id = create_engine.engine_id;
   intel_ioctl(device->local_fd, DRM_IOCTL_XE_ENGINE_DESTROY, &destroy_engine);
destroy_vm:
   intel_ioctl(device->local_fd, DRM_IOCTL_XE_VM_DESTROY, &destroy_vm);
}

VkResult
anv_xe_device_check_status(struct vk_device *vk_device)
{
   struct anv_device *device = container_of(vk_device, struct anv_device, vk);
   VkResult result = VK_SUCCESS;

   for (uint32_t i = 0; i < device->queue_count; i++) {
      struct drm_xe_engine_get_property engine_get_property = {
         .engine_id = device->queues[i].engine_id,
         .property = XE_ENGINE_GET_PROPERTY_BAN,
      };
      int ret = intel_ioctl(device->fd, DRM_IOCTL_XE_ENGINE_GET_PROPERTY,
                            &engine_get_property);

      if (ret || engine_get_property.value) {
         result = vk_device_set_lost(&device->vk, "One or more queues banned");
         break;
      }
   }

   return result;
}
