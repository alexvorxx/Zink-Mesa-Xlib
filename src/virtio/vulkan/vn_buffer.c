/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_buffer.h"

#include "venus-protocol/vn_protocol_driver_buffer.h"
#include "venus-protocol/vn_protocol_driver_buffer_view.h"

#include "vn_android.h"
#include "vn_device.h"
#include "vn_device_memory.h"
#include "vn_physical_device.h"

/* buffer commands */

static inline bool
vn_buffer_create_info_can_be_cached(const VkBufferCreateInfo *create_info,
                                    struct vn_buffer_cache *cache)
{
   /* cache only VK_SHARING_MODE_EXCLUSIVE and without pNext for simplicity */
   return (create_info->size <= cache->max_buffer_size) &&
          (create_info->pNext == NULL) &&
          (create_info->sharingMode == VK_SHARING_MODE_EXCLUSIVE);
}

static VkResult
vn_buffer_get_max_buffer_size(struct vn_device *dev,
                              uint64_t *out_max_buffer_size)
{
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;
   struct vn_physical_device *pdev = dev->physical_device;
   VkDevice dev_handle = vn_device_to_handle(dev);
   VkBuffer buf_handle;
   VkBufferCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   uint64_t max_buffer_size = 0;
   uint8_t begin = 0;
   uint8_t end = 64;

   if (pdev->features.vulkan_1_3.maintenance4) {
      *out_max_buffer_size = pdev->properties.vulkan_1_3.maxBufferSize;
      return VK_SUCCESS;
   }

   /* For drivers that don't support VK_KHR_maintenance4, we try to estimate
    * the maxBufferSize using binary search.
    * TODO remove all the search code after VK_KHR_maintenance4 becomes
    * a requirement.
    */
   while (begin < end) {
      uint8_t mid = (begin + end) >> 1;
      create_info.size = 1ull << mid;
      if (vn_CreateBuffer(dev_handle, &create_info, alloc, &buf_handle) ==
          VK_SUCCESS) {
         vn_DestroyBuffer(dev_handle, buf_handle, alloc);
         max_buffer_size = create_info.size;
         begin = mid + 1;
      } else {
         end = mid;
      }
   }

   *out_max_buffer_size = max_buffer_size;
   return VK_SUCCESS;
}

VkResult
vn_buffer_cache_init(struct vn_device *dev)
{
   uint32_t ahb_mem_type_bits = 0;
   uint64_t max_buffer_size = 0;
   VkResult result;

   if (dev->base.base.enabled_extensions
          .ANDROID_external_memory_android_hardware_buffer) {
      result =
         vn_android_get_ahb_buffer_memory_type_bits(dev, &ahb_mem_type_bits);
      if (result != VK_SUCCESS)
         return result;
   }

   if (!VN_PERF(NO_ASYNC_BUFFER_CREATE)) {
      result = vn_buffer_get_max_buffer_size(dev, &max_buffer_size);
      if (result != VK_SUCCESS)
         return result;
   }

   dev->buffer_cache.ahb_mem_type_bits = ahb_mem_type_bits;
   dev->buffer_cache.max_buffer_size = max_buffer_size;

   simple_mtx_init(&dev->buffer_cache.mutex, mtx_plain);
   util_sparse_array_init(&dev->buffer_cache.entries,
                          sizeof(struct vn_buffer_cache_entry), 64);

   return VK_SUCCESS;
}

static void
vn_buffer_cache_debug_dump(struct vn_buffer_cache *cache)
{
   vn_log(NULL, "dumping buffer cache statistics");
   vn_log(NULL, "  cache hit: %d", cache->debug.cache_hit_count);
   vn_log(NULL, "  cache miss: %d", cache->debug.cache_miss_count);
   vn_log(NULL, "  cache skip: %d", cache->debug.cache_skip_count);
}

void
vn_buffer_cache_fini(struct vn_device *dev)
{
   util_sparse_array_finish(&dev->buffer_cache.entries);
   simple_mtx_destroy(&dev->buffer_cache.mutex);

   if (VN_DEBUG(CACHE))
      vn_buffer_cache_debug_dump(&dev->buffer_cache);
}

static struct vn_buffer_cache_entry *
vn_buffer_get_cached_memory_requirements(
   struct vn_buffer_cache *cache,
   const VkBufferCreateInfo *create_info,
   struct vn_buffer_memory_requirements *out)
{
   if (VN_PERF(NO_ASYNC_BUFFER_CREATE))
      return NULL;

   /* 12.7. Resource Memory Association
    *
    * The memoryTypeBits member is identical for all VkBuffer objects created
    * with the same value for the flags and usage members in the
    * VkBufferCreateInfo structure and the handleTypes member of the
    * VkExternalMemoryBufferCreateInfo structure passed to vkCreateBuffer.
    */
   if (vn_buffer_create_info_can_be_cached(create_info, cache)) {
      /* Combine flags and usage bits to form a unique index. */
      const uint64_t idx =
         (uint64_t)create_info->flags << 32 | create_info->usage;

      struct vn_buffer_cache_entry *entry =
         util_sparse_array_get(&cache->entries, idx);

      if (entry->valid) {
         *out = entry->requirements;

         /* TODO remove comment after mandating VK_KHR_maintenance4
          *
          * This is based on below implementation defined behavior:
          *    req.size <= align64(info.size, req.alignment)
          */
         out->memory.memoryRequirements.size = align64(
            create_info->size, out->memory.memoryRequirements.alignment);

         p_atomic_inc(&cache->debug.cache_hit_count);
      } else {
         p_atomic_inc(&cache->debug.cache_miss_count);
      }

      return entry;
   }

   p_atomic_inc(&cache->debug.cache_skip_count);

   return NULL;
}

static void
vn_buffer_cache_entry_init(struct vn_buffer_cache *cache,
                           struct vn_buffer_cache_entry *entry,
                           VkMemoryRequirements2 *req)
{
   simple_mtx_lock(&cache->mutex);

   /* Entry might have already been initialized by another thread
    * before the lock
    */
   if (entry->valid)
      goto unlock;

   entry->requirements.memory = *req;

   const VkMemoryDedicatedRequirements *dedicated_req =
      vk_find_struct_const(req->pNext, MEMORY_DEDICATED_REQUIREMENTS);
   if (dedicated_req)
      entry->requirements.dedicated = *dedicated_req;

   entry->valid = true;

unlock:
   simple_mtx_unlock(&cache->mutex);
}

static void
vn_copy_cached_memory_requirements(
   const struct vn_buffer_memory_requirements *cached,
   VkMemoryRequirements2 *out_mem_req)
{
   union {
      VkBaseOutStructure *pnext;
      VkMemoryRequirements2 *two;
      VkMemoryDedicatedRequirements *dedicated;
   } u = { .two = out_mem_req };

   while (u.pnext) {
      switch (u.pnext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2:
         u.two->memoryRequirements = cached->memory.memoryRequirements;
         break;
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS:
         u.dedicated->prefersDedicatedAllocation =
            cached->dedicated.prefersDedicatedAllocation;
         u.dedicated->requiresDedicatedAllocation =
            cached->dedicated.requiresDedicatedAllocation;
         break;
      default:
         break;
      }
      u.pnext = u.pnext->pNext;
   }
}

static VkResult
vn_buffer_init(struct vn_device *dev,
               const VkBufferCreateInfo *create_info,
               struct vn_buffer *buf)
{
   VkDevice dev_handle = vn_device_to_handle(dev);
   VkBuffer buf_handle = vn_buffer_to_handle(buf);
   struct vn_buffer_cache *cache = &dev->buffer_cache;
   VkResult result;

   /* If cacheable and mem requirements found in cache, make async call */
   struct vn_buffer_cache_entry *entry =
      vn_buffer_get_cached_memory_requirements(cache, create_info,
                                               &buf->requirements);

   /* Check size instead of entry->valid to be lock free */
   if (buf->requirements.memory.memoryRequirements.size) {
      vn_async_vkCreateBuffer(dev->instance, dev_handle, create_info, NULL,
                              &buf_handle);
      return VK_SUCCESS;
   }

   /* If cache miss or not cacheable, make synchronous call */
   result = vn_call_vkCreateBuffer(dev->instance, dev_handle, create_info,
                                   NULL, &buf_handle);
   if (result != VK_SUCCESS)
      return result;

   buf->requirements.memory.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
   buf->requirements.memory.pNext = &buf->requirements.dedicated;
   buf->requirements.dedicated.sType =
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
   buf->requirements.dedicated.pNext = NULL;

   vn_call_vkGetBufferMemoryRequirements2(
      dev->instance, dev_handle,
      &(VkBufferMemoryRequirementsInfo2){
         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
         .buffer = buf_handle,
      },
      &buf->requirements.memory);

   /* If cacheable, store mem requirements from the synchronous call */
   if (entry)
      vn_buffer_cache_entry_init(cache, entry, &buf->requirements.memory);

   return VK_SUCCESS;
}

VkResult
vn_buffer_create(struct vn_device *dev,
                 const VkBufferCreateInfo *create_info,
                 const VkAllocationCallbacks *alloc,
                 struct vn_buffer **out_buf)
{
   struct vn_buffer *buf = NULL;
   VkResult result;

   buf = vk_zalloc(alloc, sizeof(*buf), VN_DEFAULT_ALIGN,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!buf)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_object_base_init(&buf->base, VK_OBJECT_TYPE_BUFFER, &dev->base);

   result = vn_buffer_init(dev, create_info, buf);
   if (result != VK_SUCCESS) {
      vn_object_base_fini(&buf->base);
      vk_free(alloc, buf);
      return result;
   }

   *out_buf = buf;

   return VK_SUCCESS;
}

VkResult
vn_CreateBuffer(VkDevice device,
                const VkBufferCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator,
                VkBuffer *pBuffer)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;
   struct vn_buffer *buf = NULL;
   VkResult result;

   const VkExternalMemoryBufferCreateInfo *external_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           EXTERNAL_MEMORY_BUFFER_CREATE_INFO);
   const bool ahb_info =
      external_info &&
      external_info->handleTypes ==
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

   if (ahb_info)
      result = vn_android_buffer_from_ahb(dev, pCreateInfo, alloc, &buf);
   else
      result = vn_buffer_create(dev, pCreateInfo, alloc, &buf);

   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   *pBuffer = vn_buffer_to_handle(buf);

   return VK_SUCCESS;
}

void
vn_DestroyBuffer(VkDevice device,
                 VkBuffer buffer,
                 const VkAllocationCallbacks *pAllocator)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_buffer *buf = vn_buffer_from_handle(buffer);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!buf)
      return;

   vn_async_vkDestroyBuffer(dev->instance, device, buffer, NULL);

   vn_object_base_fini(&buf->base);
   vk_free(alloc, buf);
}

VkDeviceAddress
vn_GetBufferDeviceAddress(VkDevice device,
                          const VkBufferDeviceAddressInfo *pInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);

   return vn_call_vkGetBufferDeviceAddress(dev->instance, device, pInfo);
}

uint64_t
vn_GetBufferOpaqueCaptureAddress(VkDevice device,
                                 const VkBufferDeviceAddressInfo *pInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);

   return vn_call_vkGetBufferOpaqueCaptureAddress(dev->instance, device,
                                                  pInfo);
}

void
vn_GetBufferMemoryRequirements2(VkDevice device,
                                const VkBufferMemoryRequirementsInfo2 *pInfo,
                                VkMemoryRequirements2 *pMemoryRequirements)
{
   const struct vn_buffer *buf = vn_buffer_from_handle(pInfo->buffer);

   vn_copy_cached_memory_requirements(&buf->requirements,
                                      pMemoryRequirements);
}

VkResult
vn_BindBufferMemory2(VkDevice device,
                     uint32_t bindInfoCount,
                     const VkBindBufferMemoryInfo *pBindInfos)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   VkBindBufferMemoryInfo *local_infos = NULL;
   for (uint32_t i = 0; i < bindInfoCount; i++) {
      const VkBindBufferMemoryInfo *info = &pBindInfos[i];
      struct vn_device_memory *mem =
         vn_device_memory_from_handle(info->memory);
      if (!mem->base_memory)
         continue;

      if (!local_infos) {
         const size_t size = sizeof(*local_infos) * bindInfoCount;
         local_infos = vk_alloc(alloc, size, VN_DEFAULT_ALIGN,
                                VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
         if (!local_infos)
            return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

         memcpy(local_infos, pBindInfos, size);
      }

      local_infos[i].memory = vn_device_memory_to_handle(mem->base_memory);
      local_infos[i].memoryOffset += mem->base_offset;
   }
   if (local_infos)
      pBindInfos = local_infos;

   vn_async_vkBindBufferMemory2(dev->instance, device, bindInfoCount,
                                pBindInfos);

   vk_free(alloc, local_infos);

   return VK_SUCCESS;
}

/* buffer view commands */

VkResult
vn_CreateBufferView(VkDevice device,
                    const VkBufferViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkBufferView *pView)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_buffer_view *view =
      vk_zalloc(alloc, sizeof(*view), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&view->base, VK_OBJECT_TYPE_BUFFER_VIEW, &dev->base);

   VkBufferView view_handle = vn_buffer_view_to_handle(view);
   vn_async_vkCreateBufferView(dev->instance, device, pCreateInfo, NULL,
                               &view_handle);

   *pView = view_handle;

   return VK_SUCCESS;
}

void
vn_DestroyBufferView(VkDevice device,
                     VkBufferView bufferView,
                     const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_buffer_view *view = vn_buffer_view_from_handle(bufferView);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!view)
      return;

   vn_async_vkDestroyBufferView(dev->instance, device, bufferView, NULL);

   vn_object_base_fini(&view->base);
   vk_free(alloc, view);
}

void
vn_GetDeviceBufferMemoryRequirements(
   VkDevice device,
   const VkDeviceBufferMemoryRequirements *pInfo,
   VkMemoryRequirements2 *pMemoryRequirements)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_buffer_cache *cache = &dev->buffer_cache;
   struct vn_buffer_memory_requirements reqs = { 0 };

   /* If cacheable and mem requirements found in cache, skip host call */
   struct vn_buffer_cache_entry *entry =
      vn_buffer_get_cached_memory_requirements(cache, pInfo->pCreateInfo,
                                               &reqs);

   /* Check size instead of entry->valid to be lock free */
   if (reqs.memory.memoryRequirements.size) {
      vn_copy_cached_memory_requirements(&reqs, pMemoryRequirements);
      return;
   }

   /* Make the host call if not found in cache or not cacheable */
   vn_call_vkGetDeviceBufferMemoryRequirements(dev->instance, device, pInfo,
                                               pMemoryRequirements);

   /* If cacheable, store mem requirements from the host call */
   if (entry)
      vn_buffer_cache_entry_init(cache, entry, pMemoryRequirements);
}
