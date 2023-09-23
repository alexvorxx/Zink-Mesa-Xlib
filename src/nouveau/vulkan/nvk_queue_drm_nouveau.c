/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_queue.h"

#include "nvk_bo_sync.h"
#include "nvk_cmd_buffer.h"
#include "nvk_cmd_pool.h"
#include "nvk_device.h"
#include "nvk_buffer.h"
#include "nvk_image.h"
#include "nvk_device_memory.h"
#include "nvk_physical_device.h"

#include "nouveau_context.h"

#include "drm-uapi/nouveau_drm.h"

#include "vk_drm_syncobj.h"

#include <xf86drm.h>

#define NVK_PUSH_MAX_SYNCS 16
#define NVK_PUSH_MAX_BINDS 4096
#define NVK_PUSH_MAX_PUSH 4096

struct push_builder {
   struct nvk_device *dev;
#if NVK_NEW_UAPI == 0
   struct drm_nouveau_gem_pushbuf_bo req_bo[NOUVEAU_GEM_MAX_BUFFERS];
   struct drm_nouveau_gem_pushbuf_push req_push[NOUVEAU_GEM_MAX_PUSH];
   struct drm_nouveau_gem_pushbuf req;
#else
   struct drm_nouveau_sync req_wait[NVK_PUSH_MAX_SYNCS];
   struct drm_nouveau_sync req_sig[NVK_PUSH_MAX_SYNCS];
   struct drm_nouveau_exec_push req_push[NVK_PUSH_MAX_PUSH];
   struct drm_nouveau_exec req;
   struct drm_nouveau_vm_bind vmbind;
   struct drm_nouveau_vm_bind_op bind_ops[NVK_PUSH_MAX_BINDS];
   bool is_vmbind;
#endif
};

static void
push_builder_init(struct nvk_device *dev, struct push_builder *pb,
                  bool is_vmbind)
{
   pb->dev = dev;
#if NVK_NEW_UAPI == 0
   assert(!is_vmbind);
   pb->req = (struct drm_nouveau_gem_pushbuf) {
      .channel = dev->ws_ctx->channel,
      .nr_buffers = 0,
      .buffers = (uintptr_t)&pb->req_bo,
      .nr_push = 0,
      .push = (uintptr_t)&pb->req_push,
   };
#else
   pb->req = (struct drm_nouveau_exec) {
      .channel = dev->ws_ctx->channel,
      .push_count = 0,
      .wait_count = 0,
      .sig_count = 0,
      .push_ptr = (uintptr_t)&pb->req_push,
      .wait_ptr = (uintptr_t)&pb->req_wait,
      .sig_ptr = (uintptr_t)&pb->req_sig,
   };
   pb->vmbind = (struct drm_nouveau_vm_bind) {
      .flags = DRM_NOUVEAU_VM_BIND_RUN_ASYNC,
      .op_count = 0,
      .op_ptr = (uintptr_t)&pb->bind_ops,
      .wait_count = 0,
      .sig_count = 0,
      .wait_ptr = (uintptr_t)&pb->req_wait,
      .sig_ptr = (uintptr_t)&pb->req_sig,
   };
   pb->is_vmbind = is_vmbind;
#endif
}

static uint32_t
push_add_bo(struct push_builder *pb,
            struct nouveau_ws_bo *bo,
            enum nouveau_ws_bo_map_flags flags)
{
#if NVK_NEW_UAPI == 0
   const uint32_t domain = (bo->flags & NOUVEAU_WS_BO_GART) ?
                           NOUVEAU_GEM_DOMAIN_GART :
                           pb->dev->ws_dev->local_mem_domain;

   for (uint32_t i = 0; i < pb->req.nr_buffers; i++) {
      if (pb->req_bo[i].handle == bo->handle) {
         assert(pb->req_bo[i].valid_domains == domain);
         if (flags & NOUVEAU_WS_BO_RD)
            pb->req_bo[i].read_domains |= domain;
         if (flags & NOUVEAU_WS_BO_WR)
            pb->req_bo[i].write_domains |= domain;
         return i;
      }
   }

   assert(pb->req.nr_buffers < NOUVEAU_GEM_MAX_BUFFERS);
   const uint32_t i = pb->req.nr_buffers++;

   pb->req_bo[i] = (struct drm_nouveau_gem_pushbuf_bo) {
      .handle = bo->handle,
      .valid_domains = domain,
   };
   if (flags & NOUVEAU_WS_BO_RD)
      pb->req_bo[i].read_domains |= domain;
   if (flags & NOUVEAU_WS_BO_WR)
      pb->req_bo[i].write_domains |= domain;

   return i;
#else
   return 0;
#endif
}

static void
push_add_sync_wait(struct push_builder *pb,
                   struct vk_sync_wait *wait)
{
#if NVK_NEW_UAPI == 1
   struct vk_drm_syncobj *sync  = vk_sync_as_drm_syncobj(wait->sync);
   assert(sync);
   assert(pb->req.wait_count < NVK_PUSH_MAX_SYNCS);
   pb->req_wait[pb->req.wait_count++] = (struct drm_nouveau_sync) {
      .flags = wait->wait_value ? DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ :
                                  DRM_NOUVEAU_SYNC_SYNCOBJ,
      .handle = sync->syncobj,
      .timeline_value = wait->wait_value,
   };
#endif
}

static void
push_add_sync_signal(struct push_builder *pb,
                     struct vk_sync_signal *sig)
{
#if NVK_NEW_UAPI == 0
   struct nvk_bo_sync *bo_sync =
      container_of(sig->sync, struct nvk_bo_sync, sync);

   push_add_bo(pb, bo_sync->bo, NOUVEAU_WS_BO_RDWR);
#else
   struct vk_drm_syncobj *sync  = vk_sync_as_drm_syncobj(sig->sync);
   assert(sync);
   assert(pb->req.sig_count < NVK_PUSH_MAX_SYNCS);
   pb->req_sig[pb->req.sig_count++] = (struct drm_nouveau_sync) {
      .flags = sig->signal_value ? DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ :
                                   DRM_NOUVEAU_SYNC_SYNCOBJ,
      .handle = sync->syncobj,
      .timeline_value = sig->signal_value,
   };
#endif
}

#if NVK_NEW_UAPI == 1
static void
push_add_buffer_bind(struct push_builder *pb,
                     VkSparseBufferMemoryBindInfo *bind_info)
{
   VK_FROM_HANDLE(nvk_buffer, buffer, bind_info->buffer);
   for (unsigned i = 0; i < bind_info->bindCount; i++) {
      const VkSparseMemoryBind *bind = &bind_info->pBinds[i];
      VK_FROM_HANDLE(nvk_device_memory, mem, bind->memory);

      assert(bind->resourceOffset + bind->size <= buffer->vma_size_B);
      assert(!mem || bind->memoryOffset + bind->size <= mem->vk.size);

      assert(pb->vmbind.op_count < NVK_PUSH_MAX_BINDS);
      pb->bind_ops[pb->vmbind.op_count++] = (struct drm_nouveau_vm_bind_op) {
         .op = mem ? DRM_NOUVEAU_VM_BIND_OP_MAP :
                     DRM_NOUVEAU_VM_BIND_OP_UNMAP,
         .handle = mem ? mem->bo->handle : 0,
         .addr = buffer->addr + bind->resourceOffset,
         .bo_offset = bind->memoryOffset,
         .range = bind->size,
      };
   }
}

static void
push_add_image_plane_opaque_bind(struct push_builder *pb,
                                 const struct nvk_image_plane *plane,
                                 const VkSparseMemoryBind *bind,
                                 uint64_t *image_plane_offset_B)
{
   *image_plane_offset_B = ALIGN_POT(*image_plane_offset_B,
                                     plane->nil.align_B);

   /* The offset of the bind range within the image */
   uint64_t image_bind_offset_B = bind->resourceOffset;
   uint64_t mem_bind_offset_B = bind->memoryOffset;
   uint64_t bind_size_B = bind->size;

   /* If the bind starts before the plane, clamp from below */
   if (image_bind_offset_B < *image_plane_offset_B) {
      /* The offset of the plane within the range being bound */
      const uint64_t bind_plane_offset_B =
         *image_plane_offset_B - image_bind_offset_B;

      /* If this plane lies above the bound range, skip this bind */
      if (bind_plane_offset_B >= bind_size_B)
         goto skip;

      image_bind_offset_B += bind_plane_offset_B;
      mem_bind_offset_B += bind_plane_offset_B;
      bind_size_B -= bind_plane_offset_B;

      assert(image_bind_offset_B == *image_plane_offset_B);
   }

   /* The offset of the bind range within the plane */
   const uint64_t plane_bind_offset_B =
      image_bind_offset_B - *image_plane_offset_B;

   /* The bound range lies above the plane */
   if (plane_bind_offset_B >= plane->vma_size_B)
      goto skip;

   /* Clamp the size to fit inside the plane */
   bind_size_B = MIN2(bind_size_B, plane->vma_size_B - plane_bind_offset_B);
   assert(bind_size_B > 0);

   VK_FROM_HANDLE(nvk_device_memory, mem, bind->memory);

   assert(plane_bind_offset_B + bind_size_B <= plane->vma_size_B);
   assert(!mem || mem_bind_offset_B + bind_size_B <= mem->vk.size);

   assert(pb->vmbind.op_count < NVK_PUSH_MAX_BINDS);
   pb->bind_ops[pb->vmbind.op_count++] = (struct drm_nouveau_vm_bind_op) {
      .op = mem ? DRM_NOUVEAU_VM_BIND_OP_MAP :
                  DRM_NOUVEAU_VM_BIND_OP_UNMAP,
      .handle = mem ? mem->bo->handle : 0,
      .addr = plane->addr + plane_bind_offset_B,
      .bo_offset = mem_bind_offset_B,
      .range = bind_size_B,
      .flags = plane->nil.pte_kind,
   };

skip:
   assert(plane->vma_size_B == plane->nil.size_B);
   *image_plane_offset_B += plane->nil.size_B;
}

static void
push_add_image_opaque_bind(struct push_builder *pb,
                           VkSparseImageOpaqueMemoryBindInfo *bind_info)
{
   VK_FROM_HANDLE(nvk_image, image, bind_info->image);
   for (unsigned i = 0; i < bind_info->bindCount; i++) {
      uint64_t image_plane_offset_B = 0;
      for (unsigned plane = 0; plane < image->plane_count; plane++) {
         push_add_image_plane_opaque_bind(pb, &image->planes[plane],
                                          &bind_info->pBinds[i],
                                          &image_plane_offset_B);
      }
      if (image->stencil_copy_temp.nil.size_B > 0) {
         push_add_image_plane_opaque_bind(pb, &image->stencil_copy_temp,
                                          &bind_info->pBinds[i],
                                          &image_plane_offset_B);
      }
   }
}
#endif

#if NVK_NEW_UAPI == 1
static void
push_add_push(struct push_builder *pb, uint64_t addr, uint32_t range,
              bool no_prefetch)
{
   assert((addr % 4) == 0 && (range % 4) == 0);

   if (range == 0)
      return;

   /* This is the hardware limit on all current GPUs */
   assert(range < (1u << 23));

   uint32_t flags = 0;
   if (no_prefetch)
      flags |= DRM_NOUVEAU_EXEC_PUSH_NO_PREFETCH;

   assert(pb->req.push_count < NVK_PUSH_MAX_PUSH);
   pb->req_push[pb->req.push_count++] = (struct drm_nouveau_exec_push) {
      .va = addr,
      .va_len = range,
      .flags = flags,
   };
}
#endif

static void
push_add_push_bo(struct push_builder *pb, struct nouveau_ws_bo *bo,
                 uint32_t offset, uint32_t range, bool no_prefetch)
{
#if NVK_NEW_UAPI == 0
   assert((offset % 4) == 0 && (range % 4) == 0);

   if (range == 0)
      return;

   assert(range < NOUVEAU_GEM_PUSHBUF_NO_PREFETCH);
   if (no_prefetch)
      range |= NOUVEAU_GEM_PUSHBUF_NO_PREFETCH;

   uint32_t bo_index = push_add_bo(pb, bo, NOUVEAU_WS_BO_RD);

   pb->req_push[pb->req.nr_push++] = (struct drm_nouveau_gem_pushbuf_push) {
      .bo_index = bo_index,
      .offset = offset,
      .length = range,
   };
#else
   push_add_push(pb, bo->offset + offset, range, no_prefetch);
#endif
}

#if NVK_NEW_UAPI == 1
static VkResult
bind_submit(struct push_builder *pb, struct nvk_queue *queue, bool sync)
{
   int err;

   pb->vmbind.wait_count = pb->req.wait_count;
   pb->vmbind.sig_count = pb->req.sig_count;
   err = drmCommandWriteRead(pb->dev->ws_dev->fd,
                             DRM_NOUVEAU_VM_BIND,
                             &pb->vmbind, sizeof(pb->vmbind));
   if (err) {
      return vk_errorf(queue, VK_ERROR_UNKNOWN,
                       "DRM_NOUVEAU_VM_BIND failed: %m");
   }
   return VK_SUCCESS;
}
#endif

static VkResult
push_submit(struct push_builder *pb, struct nvk_queue *queue, bool sync)
{
   int err;
#if NVK_NEW_UAPI == 0
   err = drmCommandWriteRead(pb->dev->ws_dev->fd,
                             DRM_NOUVEAU_GEM_PUSHBUF,
                             &pb->req, sizeof(pb->req));
   if (err) {
      return vk_errorf(queue, VK_ERROR_UNKNOWN,
                       "DRM_NOUVEAU_GEM_PUSHBUF failed: %m");
   }

   if (sync && pb->req.nr_buffers > 0) {
      struct drm_nouveau_gem_cpu_prep req = {};
      req.handle = pb->req_bo[0].handle;
      req.flags = NOUVEAU_GEM_CPU_PREP_WRITE;
      err = drmCommandWrite(pb->dev->ws_dev->fd,
                            DRM_NOUVEAU_GEM_CPU_PREP,
                            &req, sizeof(req));
      if (err) {
         return vk_errorf(queue, VK_ERROR_UNKNOWN,
                          "DRM_NOUVEAU_GEM_CPU_PREP failed: %m");
      }
   }
#else
   if (sync) {
      assert(pb->req.sig_count < NVK_PUSH_MAX_SYNCS);
      pb->req_sig[pb->req.sig_count++] = (struct drm_nouveau_sync) {
         .flags = DRM_NOUVEAU_SYNC_SYNCOBJ,
         .handle = queue->syncobj_handle,
         .timeline_value = 0,
      };
   }
   err = drmCommandWriteRead(pb->dev->ws_dev->fd,
                             DRM_NOUVEAU_EXEC,
                             &pb->req, sizeof(pb->req));
   if (err) {
      VkResult result = VK_ERROR_UNKNOWN;
      if (err == -ENODEV)
         result = VK_ERROR_DEVICE_LOST;
      return vk_errorf(queue, result,
                       "DRM_NOUVEAU_EXEC failed: %m");
   }
   if (sync) {
      err = drmSyncobjWait(pb->dev->ws_dev->fd,
                           &queue->syncobj_handle, 1, INT64_MAX,
                           DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
                           NULL);
      if (err) {
         return vk_errorf(queue, VK_ERROR_UNKNOWN,
                          "DRM_SYNCOBJ_WAIT failed: %m");
      }
   }
#endif
   return VK_SUCCESS;
}

VkResult
nvk_queue_submit_simple_drm_nouveau(struct nvk_queue *queue,
                                    uint32_t push_dw_count,
                                    struct nouveau_ws_bo *push_bo,
                                    uint32_t extra_bo_count,
                                    struct nouveau_ws_bo **extra_bos)
{
   struct nvk_device *dev = nvk_queue_device(queue);

   struct push_builder pb;
   push_builder_init(dev, &pb, false);

   push_add_push_bo(&pb, push_bo, 0, push_dw_count * 4, false);
   for (uint32_t i = 0; i < extra_bo_count; i++)
      push_add_bo(&pb, extra_bos[i], NOUVEAU_WS_BO_RDWR);

   return push_submit(&pb, queue, true);
}

static void
push_add_queue_state(struct push_builder *pb, struct nvk_queue_state *qs)
{
   if (qs->images.bo)
      push_add_bo(pb, qs->images.bo, NOUVEAU_WS_BO_RD);
   if (qs->samplers.bo)
      push_add_bo(pb, qs->samplers.bo, NOUVEAU_WS_BO_RD);
   if (qs->slm.bo)
      push_add_bo(pb, qs->slm.bo, NOUVEAU_WS_BO_RDWR);
   if (qs->push.bo)
      push_add_push_bo(pb, qs->push.bo, 0, qs->push.dw_count * 4, false);
}

static void
push_add_heap(struct push_builder *pb, struct nvk_heap *heap)
{
   simple_mtx_lock(&heap->mutex);
   for (uint32_t i = 0; i < heap->bo_count; i++)
      push_add_bo(pb, heap->bos[i].bo, NOUVEAU_WS_BO_RDWR);
   simple_mtx_unlock(&heap->mutex);
}

VkResult
nvk_queue_submit_drm_nouveau(struct nvk_queue *queue,
                             struct vk_queue_submit *submit,
                             bool sync)
{
   struct nvk_device *dev = nvk_queue_device(queue);
   struct push_builder pb;

   const bool is_vmbind = submit->buffer_bind_count > 0 ||
                          submit->image_opaque_bind_count > 0;
   push_builder_init(dev, &pb, is_vmbind);

   for (uint32_t i = 0; i < submit->wait_count; i++) {
      push_add_sync_wait(&pb, &submit->waits[i]);
   }

   pthread_mutex_lock(&dev->mutex);

   for (uint32_t i = 0; i < submit->signal_count; i++) {
      push_add_sync_signal(&pb, &submit->signals[i]);
   }

#if NVK_NEW_UAPI == 1
   for (uint32_t i = 0; i < submit->buffer_bind_count; i++) {
      push_add_buffer_bind(&pb, &submit->buffer_binds[i]);
   }

   for (uint32_t i = 0; i < submit->image_opaque_bind_count; i++) {
      push_add_image_opaque_bind(&pb, &submit->image_opaque_binds[i]);
   }
#else
   assert(submit->buffer_bind_count == 0);
   assert(submit->image_opaque_bind_count == 0);
#endif

   if (is_vmbind) {
      assert(submit->command_buffer_count == 0);
   } else if (submit->command_buffer_count == 0) {
#if NVK_NEW_UAPI == 0
      push_add_push_bo(&pb, queue->empty_push, 0,
                       queue->empty_push_dw_count * 4, false);
#endif
   } else {
      push_add_queue_state(&pb, &queue->state);

      push_add_heap(&pb, &dev->shader_heap);

#if NVK_NEW_UAPI == 0
      list_for_each_entry(struct nvk_device_memory, mem,
                          &dev->memory_objects, link) {
         push_add_bo(&pb, mem->bo, NOUVEAU_WS_BO_RDWR);
      }
#endif

      for (unsigned i = 0; i < submit->command_buffer_count; i++) {
         struct nvk_cmd_buffer *cmd =
            container_of(submit->command_buffers[i], struct nvk_cmd_buffer, vk);

         list_for_each_entry_safe(struct nvk_cmd_bo, bo, &cmd->bos, link)
            push_add_bo(&pb, bo->bo, NOUVEAU_WS_BO_RD);

#if NVK_NEW_UAPI == 1
         util_dynarray_foreach(&cmd->pushes, struct nvk_cmd_push, push)
            push_add_push(&pb, push->addr, push->range, push->no_prefetch);
#else
         util_dynarray_foreach(&cmd->pushes, struct nvk_cmd_push, push) {
            push_add_push_bo(&pb, push->bo, push->bo_offset, push->range,
                             push->no_prefetch);
         }
#endif

         util_dynarray_foreach(&cmd->bo_refs, struct nvk_cmd_bo_ref, ref)
            push_add_bo(&pb, ref->bo, NOUVEAU_WS_BO_RDWR);
      }
   }

   VkResult result;
   if (is_vmbind) {
#if NVK_NEW_UAPI == 1
      result = bind_submit(&pb, queue, sync);
#else
      unreachable("Sparse is not supported on the old uAPI");
#endif
   } else {
      result = push_submit(&pb, queue, sync);
   }

#if NVK_NEW_UAPI == 0
   if (result == VK_SUCCESS) {
      for (uint32_t i = 0; i < submit->wait_count; i++) {
         struct nvk_bo_sync *bo_sync =
            container_of(submit->waits[i].sync, struct nvk_bo_sync, sync);
         bo_sync->state = NVK_BO_SYNC_STATE_RESET;
      }
      for (uint32_t i = 0; i < submit->signal_count; i++) {
         struct nvk_bo_sync *bo_sync =
            container_of(submit->signals[i].sync, struct nvk_bo_sync, sync);
         assert(bo_sync->state == NVK_BO_SYNC_STATE_RESET);
         bo_sync->state = NVK_BO_SYNC_STATE_SUBMITTED;
      }
   }
#endif

   pthread_cond_broadcast(&dev->queue_submit);
   pthread_mutex_unlock(&dev->mutex);

   return result;
}
