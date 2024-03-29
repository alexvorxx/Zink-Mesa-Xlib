/*
 * Copyright © 2024 Valve Corporation
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

#include "radv_private.h"

VKAPI_ATTR VkResult VKAPI_CALL
ctx_roll_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   RADV_FROM_HANDLE(radv_queue, queue, _queue);

   simple_mtx_lock(&queue->device->ctx_roll_mtx);

   if (queue->device->ctx_roll_file) {
      fclose(queue->device->ctx_roll_file);
      queue->device->ctx_roll_file = NULL;
   }

   simple_mtx_unlock(&queue->device->ctx_roll_mtx);

   return queue->device->layer_dispatch.ctx_roll.QueuePresentKHR(_queue, pPresentInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL
ctx_roll_QueueSubmit2(VkQueue _queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence _fence)
{
   RADV_FROM_HANDLE(radv_queue, queue, _queue);

   simple_mtx_lock(&queue->device->ctx_roll_mtx);

   if (queue->device->ctx_roll_file) {
      for (uint32_t submit_index = 0; submit_index < submitCount; submit_index++) {
         const VkSubmitInfo2 *submit = pSubmits + submit_index;
         for (uint32_t i = 0; i < submit->commandBufferInfoCount; i++) {
            RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, submit->pCommandBufferInfos[i].commandBuffer);
            fprintf(queue->device->ctx_roll_file, "\n%s:\n", vk_object_base_name(&cmd_buffer->vk.base));
            queue->device->ws->cs_dump(cmd_buffer->cs, queue->device->ctx_roll_file, NULL, 0,
                                       RADV_CS_DUMP_TYPE_CTX_ROLLS);
         }
      }
   }

   simple_mtx_unlock(&queue->device->ctx_roll_mtx);

   return queue->device->layer_dispatch.ctx_roll.QueueSubmit2(_queue, submitCount, pSubmits, _fence);
}
