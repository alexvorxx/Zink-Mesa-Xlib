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
