/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright 2019 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "agx_device.h"
#include <inttypes.h>
#include "agx_bo.h"
#include "decode.h"

unsigned AGX_FAKE_HANDLE = 0;
uint64_t AGX_FAKE_LO = 0;
uint64_t AGX_FAKE_HI = (1ull << 32);

void
agx_bo_free(struct agx_device *dev, struct agx_bo *bo)
{
   free(bo->ptr.cpu);

   /* Reset the handle */
   memset(bo, 0, sizeof(*bo));
}

struct agx_bo *
agx_bo_alloc(struct agx_device *dev, size_t size, enum agx_bo_flags flags)
{
   struct agx_bo *bo;
   unsigned handle = 0;

   /* executable implies low va */
   assert(!(flags & AGX_BO_EXEC) || (flags & AGX_BO_LOW_VA));

   /* Faked software path until we have a DRM driver */
   handle = (++AGX_FAKE_HANDLE);

   pthread_mutex_lock(&dev->bo_map_lock);
   bo = agx_lookup_bo(dev, handle);
   pthread_mutex_unlock(&dev->bo_map_lock);

   /* Fresh handle */
   assert(!memcmp(bo, &((struct agx_bo){}), sizeof(*bo)));

   bo->type = AGX_ALLOC_REGULAR;
   bo->size = size;
   bo->flags = flags;
   bo->dev = dev;
   bo->handle = handle;

   ASSERTED bool lo = (flags & AGX_BO_LOW_VA);

   if (lo) {
      bo->ptr.gpu = AGX_FAKE_LO;
      AGX_FAKE_LO += bo->size;
   } else {
      bo->ptr.gpu = AGX_FAKE_HI;
      AGX_FAKE_HI += bo->size;
   }

   bo->ptr.gpu = (((uint64_t)bo->handle) << (lo ? 16 : 24));
   bo->ptr.cpu = calloc(1, bo->size);

   assert(bo->ptr.gpu < (1ull << (lo ? 32 : 40)));

   return bo;
}

struct agx_bo *
agx_bo_import(struct agx_device *dev, int fd)
{
   unreachable("Linux UAPI not yet upstream");
}

int
agx_bo_export(struct agx_bo *bo)
{
   bo->flags |= AGX_BO_SHARED;

   unreachable("Linux UAPI not yet upstream");
}

static void
agx_get_global_ids(struct agx_device *dev)
{
   dev->next_global_id = 0;
   dev->last_global_id = 0x1000000;
}

uint64_t
agx_get_global_id(struct agx_device *dev)
{
   if (unlikely(dev->next_global_id >= dev->last_global_id)) {
      agx_get_global_ids(dev);
   }

   return dev->next_global_id++;
}

/* Tries to open an AGX device, returns true if successful */

bool
agx_open_device(void *memctx, struct agx_device *dev)
{
   dev->memctx = memctx;
   util_sparse_array_init(&dev->bo_map, sizeof(struct agx_bo), 512);

   simple_mtx_init(&dev->bo_cache.lock, mtx_plain);
   list_inithead(&dev->bo_cache.lru);

   for (unsigned i = 0; i < ARRAY_SIZE(dev->bo_cache.buckets); ++i)
      list_inithead(&dev->bo_cache.buckets[i]);

   dev->queue = agx_create_command_queue(dev);
   agx_get_global_ids(dev);

   return true;
}

void
agx_close_device(struct agx_device *dev)
{
   agx_bo_cache_evict_all(dev);
   util_sparse_array_finish(&dev->bo_map);
}

struct agx_command_queue
agx_create_command_queue(struct agx_device *dev)
{
   return (struct agx_command_queue){};
}

void
agx_submit_cmdbuf(struct agx_device *dev, unsigned cmdbuf, unsigned mappings,
                  uint64_t scalar)
{
}

void
agx_wait_queue(struct agx_command_queue queue)
{
}
