/*
 * Copyright © 2023 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdint.h>
#include <stddef.h>
#include <vulkan/vulkan_core.h>

#include "c11/threads.h"
#include "hwdef/rogue_hw_utils.h"
#include "pvr_bo.h"
#include "pvr_private.h"
#include "pvr_spm.h"
#include "util/simple_mtx.h"
#include "util/u_atomic.h"
#include "vk_alloc.h"
#include "vk_log.h"

struct pvr_spm_scratch_buffer {
   uint32_t ref_count;
   struct pvr_bo *bo;
   uint64_t size;
};

void pvr_spm_init_scratch_buffer_store(struct pvr_device *device)
{
   struct pvr_spm_scratch_buffer_store *store =
      &device->spm_scratch_buffer_store;

   simple_mtx_init(&store->mtx, mtx_plain);
   store->head_ref = NULL;
}

void pvr_spm_finish_scratch_buffer_store(struct pvr_device *device)
{
   struct pvr_spm_scratch_buffer_store *store =
      &device->spm_scratch_buffer_store;

   /* Either a framebuffer was never created so no scratch buffer was ever
    * created or all framebuffers have been freed so only the store's reference
    * remains.
    */
   assert(!store->head_ref || p_atomic_read(&store->head_ref->ref_count) == 1);

   simple_mtx_destroy(&store->mtx);

   if (store->head_ref) {
      pvr_bo_free(device, store->head_ref->bo);
      vk_free(&device->vk.alloc, store->head_ref);
   }
}

uint64_t
pvr_spm_scratch_buffer_calc_required_size(const struct pvr_render_pass *pass,
                                          uint32_t framebuffer_width,
                                          uint32_t framebuffer_height)
{
   uint64_t dwords_per_pixel;
   uint64_t buffer_size;

   /* If we're allocating an SPM scratch buffer we'll have a minimum of 1 output
    * reg and/or tile_buffer.
    */
   uint32_t nr_tile_buffers = 1;
   uint32_t nr_output_regs = 1;

   for (uint32_t i = 0; i < pass->hw_setup->render_count; i++) {
      const struct pvr_renderpass_hwsetup_render *hw_render =
         &pass->hw_setup->renders[i];

      nr_tile_buffers = MAX2(nr_tile_buffers, hw_render->tile_buffers_count);
      nr_output_regs = MAX2(nr_output_regs, hw_render->output_regs_count);
   }

   dwords_per_pixel =
      (uint64_t)pass->max_sample_count * nr_output_regs * nr_tile_buffers;

   buffer_size = ALIGN_POT((uint64_t)framebuffer_width,
                           PVRX(CR_PBE_WORD0_MRT0_LINESTRIDE_ALIGNMENT));
   buffer_size *= (uint64_t)framebuffer_height * dwords_per_pixel * 4;

   return buffer_size;
}

static VkResult
pvr_spm_scratch_buffer_alloc(struct pvr_device *device,
                             uint64_t size,
                             struct pvr_spm_scratch_buffer **const buffer_out)
{
   const uint32_t cache_line_size =
      rogue_get_slc_cache_line_size(&device->pdevice->dev_info);
   struct pvr_spm_scratch_buffer *scratch_buffer;
   struct pvr_bo *bo;
   VkResult result;

   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         size,
                         cache_line_size,
                         0,
                         &bo);
   if (result != VK_SUCCESS) {
      *buffer_out = NULL;
      return result;
   }

   scratch_buffer = vk_alloc(&device->vk.alloc,
                             sizeof(*scratch_buffer),
                             4,
                             VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!scratch_buffer) {
      pvr_bo_free(device, bo);
      *buffer_out = NULL;
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   *scratch_buffer = (struct pvr_spm_scratch_buffer){
      .bo = bo,
      .size = size,
   };

   *buffer_out = scratch_buffer;

   return VK_SUCCESS;
}

static void
pvr_spm_scratch_buffer_release_locked(struct pvr_device *device,
                                      struct pvr_spm_scratch_buffer *buffer)
{
   struct pvr_spm_scratch_buffer_store *store =
      &device->spm_scratch_buffer_store;

   simple_mtx_assert_locked(&store->mtx);

   if (p_atomic_dec_zero(&buffer->ref_count)) {
      pvr_bo_free(device, buffer->bo);
      vk_free(&device->vk.alloc, buffer);
   }
}

void pvr_spm_scratch_buffer_release(struct pvr_device *device,
                                    struct pvr_spm_scratch_buffer *buffer)
{
   struct pvr_spm_scratch_buffer_store *store =
      &device->spm_scratch_buffer_store;

   simple_mtx_lock(&store->mtx);

   pvr_spm_scratch_buffer_release_locked(device, buffer);

   simple_mtx_unlock(&store->mtx);
}

static void pvr_spm_scratch_buffer_store_set_head_ref_locked(
   struct pvr_spm_scratch_buffer_store *store,
   struct pvr_spm_scratch_buffer *buffer)
{
   simple_mtx_assert_locked(&store->mtx);
   assert(!store->head_ref);

   p_atomic_inc(&buffer->ref_count);
   store->head_ref = buffer;
}

static void pvr_spm_scratch_buffer_store_release_head_ref_locked(
   struct pvr_device *device,
   struct pvr_spm_scratch_buffer_store *store)
{
   simple_mtx_assert_locked(&store->mtx);

   pvr_spm_scratch_buffer_release_locked(device, store->head_ref);

   store->head_ref = NULL;
}

VkResult pvr_spm_scratch_buffer_get_buffer(
   struct pvr_device *device,
   uint64_t size,
   struct pvr_spm_scratch_buffer **const buffer_out)
{
   struct pvr_spm_scratch_buffer_store *store =
      &device->spm_scratch_buffer_store;
   struct pvr_spm_scratch_buffer *buffer;

   simple_mtx_lock(&store->mtx);

   /* When a render requires a PR the fw will wait for other renders to end,
    * free the PB space, unschedule any other vert/frag jobs and solely run the
    * PR on the whole device until completion.
    * Thus we can safely use the same scratch buffer across multiple
    * framebuffers as the scratch buffer is only used during PRs and only one PR
    * can ever be executed at any one time.
    */
   if (store->head_ref && store->head_ref->size <= size) {
      buffer = store->head_ref;
   } else {
      VkResult result;

      if (store->head_ref)
         pvr_spm_scratch_buffer_store_release_head_ref_locked(device, store);

      result = pvr_spm_scratch_buffer_alloc(device, size, &buffer);
      if (result != VK_SUCCESS) {
         simple_mtx_unlock(&store->mtx);
         *buffer_out = NULL;

         return result;
      }

      pvr_spm_scratch_buffer_store_set_head_ref_locked(store, buffer);
   }

   p_atomic_inc(&buffer->ref_count);
   simple_mtx_unlock(&store->mtx);
   *buffer_out = buffer;

   return VK_SUCCESS;
}
