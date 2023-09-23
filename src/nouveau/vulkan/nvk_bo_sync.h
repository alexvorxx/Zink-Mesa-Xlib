/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_BO_SYNC_H
#define NVK_BO_SYNC_H 1

#include "nvk_private.h"

#include "vulkan/runtime/vk_sync.h"

extern const struct vk_sync_type nvk_bo_sync_type;

enum nvk_bo_sync_state {
   NVK_BO_SYNC_STATE_RESET,
   NVK_BO_SYNC_STATE_SUBMITTED,
   NVK_BO_SYNC_STATE_SIGNALED,
};

struct nvk_bo_sync {
   struct vk_sync sync;

   enum nvk_bo_sync_state state;
   struct nouveau_ws_bo *bo;
   int dmabuf_fd;
};

VKAPI_ATTR VkResult VKAPI_CALL
nvk_create_sync_for_memory(struct vk_device *device,
                           VkDeviceMemory memory,
                           bool signal_memory,
                           struct vk_sync **sync_out);

#endif /* NVK_BO_SYNC_H */
