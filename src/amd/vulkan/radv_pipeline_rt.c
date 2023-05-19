/*
 * Copyright © 2021 Google
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

#include "nir/nir.h"

#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"

struct rt_handle_hash_entry {
   uint32_t key;
   char hash[20];
};

static uint32_t
handle_from_stages(struct radv_device *device, const VkPipelineShaderStageCreateInfo *stages,
                   unsigned stage_count, bool replay_namespace)
{
   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);

   radv_hash_rt_stages(&ctx, stages, stage_count);
   unsigned char hash[20];
   _mesa_sha1_final(&ctx, hash);

   uint32_t ret;
   memcpy(&ret, hash, sizeof(ret));

   /* Leave the low half for resume shaders etc. */
   ret |= 1u << 31;

   /* Ensure we have dedicated space for replayable shaders */
   ret &= ~(1u << 30);
   ret |= replay_namespace << 30;

   simple_mtx_lock(&device->rt_handles_mtx);

   struct hash_entry *he = NULL;
   for (;;) {
      he = _mesa_hash_table_search(device->rt_handles, &ret);
      if (!he)
         break;

      if (memcmp(he->data, hash, sizeof(hash)) == 0)
         break;

      ++ret;
   }

   if (!he) {
      struct rt_handle_hash_entry *e = ralloc(device->rt_handles, struct rt_handle_hash_entry);
      e->key = ret;
      memcpy(e->hash, hash, sizeof(e->hash));
      _mesa_hash_table_insert(device->rt_handles, &e->key, &e->hash);
   }

   simple_mtx_unlock(&device->rt_handles_mtx);

   return ret;
}

static VkResult
radv_create_group_handles(struct radv_device *device,
                          const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                          struct radv_ray_tracing_group *groups)
{
   bool capture_replay = pCreateInfo->flags &
                         VK_PIPELINE_CREATE_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR;
   for (unsigned i = 0; i < pCreateInfo->groupCount; ++i) {
      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &pCreateInfo->pGroups[i];
      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:
         if (group_info->generalShader != VK_SHADER_UNUSED_KHR)
            groups[i].handle.general_index = handle_from_stages(
               device, &pCreateInfo->pStages[group_info->generalShader], 1, capture_replay);
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
         if (group_info->closestHitShader != VK_SHADER_UNUSED_KHR)
            groups[i].handle.closest_hit_index = handle_from_stages(
               device, &pCreateInfo->pStages[group_info->closestHitShader], 1, capture_replay);
         if (group_info->intersectionShader != VK_SHADER_UNUSED_KHR) {
            VkPipelineShaderStageCreateInfo stages[2];
            unsigned cnt = 0;
            stages[cnt++] = pCreateInfo->pStages[group_info->intersectionShader];
            if (group_info->anyHitShader != VK_SHADER_UNUSED_KHR)
               stages[cnt++] = pCreateInfo->pStages[group_info->anyHitShader];
            groups[i].handle.intersection_index =
               handle_from_stages(device, stages, cnt, capture_replay);
         }
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         if (group_info->closestHitShader != VK_SHADER_UNUSED_KHR)
            groups[i].handle.closest_hit_index = handle_from_stages(
               device, &pCreateInfo->pStages[group_info->closestHitShader], 1, capture_replay);
         if (group_info->anyHitShader != VK_SHADER_UNUSED_KHR)
            groups[i].handle.any_hit_index = handle_from_stages(
               device, &pCreateInfo->pStages[group_info->anyHitShader], 1, capture_replay);
         break;
      case VK_SHADER_GROUP_SHADER_MAX_ENUM_KHR:
         unreachable("VK_SHADER_GROUP_SHADER_MAX_ENUM_KHR");
      }

      if (capture_replay) {
         if (group_info->pShaderGroupCaptureReplayHandle &&
             memcmp(group_info->pShaderGroupCaptureReplayHandle, &groups[i].handle,
                    sizeof(groups[i].handle)) != 0) {
            return VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS;
         }
      }
   }

   return VK_SUCCESS;
}

static VkResult
radv_rt_fill_group_info(struct radv_device *device,
                        const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                        struct radv_ray_tracing_group *groups)
{
   VkResult result = radv_create_group_handles(device, pCreateInfo, groups);

   uint32_t idx;
   for (idx = 0; idx < pCreateInfo->groupCount; idx++) {
      groups[idx].type = pCreateInfo->pGroups[idx].type;
      if (groups[idx].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR)
         groups[idx].recursive_shader = pCreateInfo->pGroups[idx].generalShader;
      else
         groups[idx].recursive_shader = pCreateInfo->pGroups[idx].closestHitShader;
      groups[idx].any_hit_shader = pCreateInfo->pGroups[idx].anyHitShader;
      groups[idx].intersection_shader = pCreateInfo->pGroups[idx].intersectionShader;
   }

   /* copy and adjust library groups (incl. handles) */
   if (pCreateInfo->pLibraryInfo) {
      unsigned stage_count = pCreateInfo->stageCount;
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         RADV_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_ray_tracing_pipeline *library_pipeline =
            radv_pipeline_to_ray_tracing(pipeline);

         for (unsigned j = 0; j < library_pipeline->group_count; ++j) {
            struct radv_ray_tracing_group *dst = &groups[idx + j];
            *dst = library_pipeline->groups[j];
            if (dst->recursive_shader != VK_SHADER_UNUSED_KHR)
               dst->recursive_shader += stage_count;
            if (dst->any_hit_shader != VK_SHADER_UNUSED_KHR)
               dst->any_hit_shader += stage_count;
            if (dst->intersection_shader != VK_SHADER_UNUSED_KHR)
               dst->intersection_shader += stage_count;
         }
         idx += library_pipeline->group_count;
         stage_count += library_pipeline->stage_count;
      }
   }

   return result;
}

static void
radv_rt_fill_stage_info(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                        struct radv_ray_tracing_stage *stages)
{
   uint32_t idx;
   for (idx = 0; idx < pCreateInfo->stageCount; idx++)
      stages[idx].stage = vk_to_mesa_shader_stage(pCreateInfo->pStages[idx].stage);

   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         RADV_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_ray_tracing_pipeline *library_pipeline =
            radv_pipeline_to_ray_tracing(pipeline);
         for (unsigned j = 0; j < library_pipeline->stage_count; ++j)
            stages[idx++].stage = library_pipeline->stages[j].stage;
      }
   }
}

static VkRayTracingPipelineCreateInfoKHR
radv_create_merged_rt_create_info(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo)
{
   VkRayTracingPipelineCreateInfoKHR local_create_info = *pCreateInfo;
   uint32_t total_stages = pCreateInfo->stageCount;
   uint32_t total_groups = pCreateInfo->groupCount;

   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         RADV_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_ray_tracing_pipeline *library_pipeline =
            radv_pipeline_to_ray_tracing(pipeline);

         total_stages += library_pipeline->stage_count;
         total_groups += library_pipeline->group_count;
      }
   }
   local_create_info.stageCount = total_stages;
   local_create_info.groupCount = total_groups;

   return local_create_info;
}

static VkResult
radv_rt_precompile_shaders(struct radv_device *device, struct vk_pipeline_cache *cache,
                           const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                           const VkPipelineCreationFeedbackCreateInfo *creation_feedback,
                           const struct radv_pipeline_key *key,
                           struct radv_ray_tracing_stage *stages)
{
   uint32_t idx;

   for (idx = 0; idx < pCreateInfo->stageCount; idx++) {
      int64_t stage_start = os_time_get_nano();
      struct radv_pipeline_stage stage;
      radv_pipeline_stage_init(&pCreateInfo->pStages[idx], &stage, stages[idx].stage);

      uint8_t shader_sha1[SHA1_DIGEST_LENGTH];
      radv_hash_shaders(shader_sha1, &stage, 1, NULL, key, radv_get_hash_flags(device, false));

      /* lookup the stage in cache */
      bool found_in_application_cache = false;
      stages[idx].shader =
         radv_pipeline_cache_search_nir(device, cache, shader_sha1, &found_in_application_cache);

      if (stages[idx].shader) {
         if (found_in_application_cache)
            stage.feedback.flags |=
               VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
         goto feedback;
      }

      if (pCreateInfo->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
         return VK_PIPELINE_COMPILE_REQUIRED;

      /* precompile the shader */
      struct nir_shader *nir = radv_parse_rt_stage(device, &pCreateInfo->pStages[idx], key);
      stages[idx].shader = radv_pipeline_cache_nir_to_handle(device, cache, nir, shader_sha1,
                                                             !key->optimisations_disabled);
      ralloc_free(nir);

      if (!stages[idx].shader)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

   feedback:
      if (creation_feedback && creation_feedback->pipelineStageCreationFeedbackCount) {
         assert(idx < creation_feedback->pipelineStageCreationFeedbackCount);
         stage.feedback.duration = os_time_get_nano() - stage_start;
         creation_feedback->pPipelineStageCreationFeedbacks[idx] = stage.feedback;
      }
   }

   /* reference library shaders */
   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         RADV_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_ray_tracing_pipeline *library = radv_pipeline_to_ray_tracing(pipeline);

         for (unsigned j = 0; j < library->stage_count; ++j)
            stages[idx++].shader = vk_pipeline_cache_object_ref(library->stages[j].shader);
      }
   }

   return VK_SUCCESS;
}

static VkResult
radv_rt_pipeline_compile(struct radv_ray_tracing_pipeline *pipeline,
                         struct radv_pipeline_layout *pipeline_layout, struct radv_device *device,
                         struct vk_pipeline_cache *cache,
                         const struct radv_pipeline_key *pipeline_key,
                         const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                         const VkPipelineCreationFeedbackCreateInfo *creation_feedback)
{
   struct radv_shader_binary *binaries[MESA_VULKAN_SHADER_STAGES] = {NULL};
   bool keep_executable_info = radv_pipeline_capture_shaders(device, pCreateInfo->flags);
   bool keep_statistic_info = radv_pipeline_capture_shader_stats(device, pCreateInfo->flags);
   struct radv_pipeline_stage rt_stage = {0};

   /* First check if we can get things from the cache before we take the expensive step of
    * generating the nir. */
   struct vk_shader_module module = {.base.type = VK_OBJECT_TYPE_SHADER_MODULE};
   VkPipelineShaderStageCreateInfo stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = NULL,
      .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
      .module = vk_shader_module_to_handle(&module),
      .pName = "main",
   };

   radv_pipeline_stage_init(&stage, &rt_stage, vk_to_mesa_shader_stage(stage.stage));

   bool found_in_application_cache = true;
   if (!keep_executable_info &&
       radv_pipeline_cache_search(device, cache, &pipeline->base.base, pipeline->sha1,
                                  &found_in_application_cache)) {
      if (found_in_application_cache && creation_feedback)
         creation_feedback->pPipelineCreationFeedback->flags |=
            VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      return VK_SUCCESS;
   }

   if (pCreateInfo->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
      return VK_PIPELINE_COMPILE_REQUIRED;

   VkResult result = radv_rt_precompile_shaders(device, cache, pCreateInfo, creation_feedback,
                                                pipeline_key, pipeline->stages);
   if (result != VK_SUCCESS)
      return result;

   VkRayTracingPipelineCreateInfoKHR local_create_info =
      radv_create_merged_rt_create_info(pCreateInfo);

   rt_stage.internal_nir = create_rt_shader(device, &local_create_info, pipeline->stages,
                                            pipeline->groups, pipeline_key);

   /* Compile SPIR-V shader to NIR. */
   rt_stage.nir =
      radv_shader_spirv_to_nir(device, &rt_stage, pipeline_key, pipeline->base.base.is_internal);

   radv_optimize_nir(rt_stage.nir, pipeline_key->optimisations_disabled);

   /* Gather info again, information such as outputs_read can be out-of-date. */
   nir_shader_gather_info(rt_stage.nir, nir_shader_get_entrypoint(rt_stage.nir));

   /* Run the shader info pass. */
   radv_nir_shader_info_init(&rt_stage.info);
   radv_nir_shader_info_pass(device, rt_stage.nir, MESA_SHADER_NONE, pipeline_layout, pipeline_key,
                             pipeline->base.base.type, false, &rt_stage.info);

   radv_declare_shader_args(device, pipeline_key, &rt_stage.info, rt_stage.stage, MESA_SHADER_NONE,
                            RADV_SHADER_TYPE_DEFAULT, &rt_stage.args);

   rt_stage.info.user_sgprs_locs = rt_stage.args.user_sgprs_locs;
   rt_stage.info.inline_push_constant_mask = rt_stage.args.ac.inline_push_const_mask;

   /* Postprocess NIR. */
   radv_postprocess_nir(device, pipeline_layout, pipeline_key, MESA_SHADER_NONE, &rt_stage);

   if (radv_can_dump_shader(device, rt_stage.nir, false))
      nir_print_shader(rt_stage.nir, stderr);

   /* Compile NIR shader to AMD assembly. */
   pipeline->base.base.shaders[rt_stage.stage] =
      radv_shader_nir_to_asm(device, cache, &rt_stage, &rt_stage.nir, 1, pipeline_key,
                             keep_executable_info, keep_statistic_info, &binaries[rt_stage.stage]);

   if (!keep_executable_info) {
      radv_pipeline_cache_insert(device, cache, &pipeline->base.base, NULL, pipeline->sha1);
   }

   free(binaries[rt_stage.stage]);
   if (radv_can_dump_shader_stats(device, rt_stage.nir)) {
      radv_dump_shader_stats(device, &pipeline->base.base,
                             pipeline->base.base.shaders[rt_stage.stage], rt_stage.stage, stderr);
   }

   ralloc_free(rt_stage.internal_nir);
   ralloc_free(rt_stage.nir);

   return VK_SUCCESS;
}

static bool
radv_rt_pipeline_has_dynamic_stack_size(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo)
{
   if (!pCreateInfo->pDynamicState)
      return false;

   for (unsigned i = 0; i < pCreateInfo->pDynamicState->dynamicStateCount; ++i) {
      if (pCreateInfo->pDynamicState->pDynamicStates[i] ==
          VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR)
         return true;
   }

   return false;
}

static void
compute_rt_stack_size(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                      struct radv_ray_tracing_pipeline *pipeline)
{
   if (radv_rt_pipeline_has_dynamic_stack_size(pCreateInfo)) {
      pipeline->stack_size = -1u;
      return;
   }

   unsigned raygen_size = 0;
   unsigned callable_size = 0;
   unsigned chit_miss_size = 0;
   unsigned intersection_size = 0;
   unsigned any_hit_size = 0;

   for (unsigned i = 0; i < pipeline->stage_count; ++i) {
      uint32_t size = pipeline->stages[i].stack_size;
      switch (pipeline->stages[i].stage) {
      case MESA_SHADER_RAYGEN:
         raygen_size = MAX2(raygen_size, size);
         break;
      case MESA_SHADER_CLOSEST_HIT:
      case MESA_SHADER_MISS:
         chit_miss_size = MAX2(chit_miss_size, size);
         break;
      case MESA_SHADER_CALLABLE:
         callable_size = MAX2(callable_size, size);
         break;
      case MESA_SHADER_INTERSECTION:
         intersection_size = MAX2(intersection_size, size);
         break;
      case MESA_SHADER_ANY_HIT:
         any_hit_size = MAX2(any_hit_size, size);
         break;
      default:
         unreachable("Invalid stage type in RT shader");
      }
   }
   pipeline->stack_size =
      raygen_size +
      MIN2(pCreateInfo->maxPipelineRayRecursionDepth, 1) *
         MAX2(chit_miss_size, intersection_size + any_hit_size) +
      MAX2(0, (int)(pCreateInfo->maxPipelineRayRecursionDepth) - 1) * chit_miss_size +
      2 * callable_size;
}

static struct radv_pipeline_key
radv_generate_rt_pipeline_key(const struct radv_device *device,
                              const struct radv_ray_tracing_pipeline *pipeline,
                              VkPipelineCreateFlags flags)
{
   struct radv_pipeline_key key = radv_generate_pipeline_key(device, &pipeline->base.base, flags);
   key.cs.compute_subgroup_size = device->physical_device->rt_wave_size;

   return key;
}

static void
combine_config(struct ac_shader_config *config, struct ac_shader_config *other)
{
   config->num_sgprs = MAX2(config->num_sgprs, other->num_sgprs);
   config->num_vgprs = MAX2(config->num_vgprs, other->num_vgprs);
   config->num_shared_vgprs = MAX2(config->num_shared_vgprs, other->num_shared_vgprs);
   config->spilled_sgprs = MAX2(config->spilled_sgprs, other->spilled_sgprs);
   config->spilled_vgprs = MAX2(config->spilled_vgprs, other->spilled_vgprs);
   config->lds_size = MAX2(config->lds_size, other->lds_size);
   config->scratch_bytes_per_wave =
      MAX2(config->scratch_bytes_per_wave, other->scratch_bytes_per_wave);

   assert(config->float_mode == other->float_mode);
}

static void
postprocess_rt_config(struct ac_shader_config *config, enum amd_gfx_level gfx_level,
                      unsigned wave_size)
{
   config->rsrc1 = (config->rsrc1 & C_00B848_VGPRS) |
                   S_00B848_VGPRS((config->num_vgprs - 1) / (wave_size == 32 ? 8 : 4));
   if (gfx_level < GFX10)
      config->rsrc1 =
         (config->rsrc1 & C_00B848_SGPRS) | S_00B848_SGPRS((config->num_sgprs - 1) / 8);

   config->rsrc2 = (config->rsrc2 & C_00B84C_LDS_SIZE) | S_00B84C_LDS_SIZE(config->lds_size);
   config->rsrc3 = (config->rsrc3 & C_00B8A0_SHARED_VGPR_CNT) |
                   S_00B8A0_SHARED_VGPR_CNT(config->num_shared_vgprs / 8);
}

static VkResult
radv_rt_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                        const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(vk_pipeline_cache, cache, _cache);
   RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   VkResult result;
   bool keep_statistic_info = radv_pipeline_capture_shader_stats(device, pCreateInfo->flags);
   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);
   if (creation_feedback)
      creation_feedback->pPipelineCreationFeedback->flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;

   int64_t pipeline_start = os_time_get_nano();

   VkRayTracingPipelineCreateInfoKHR local_create_info =
      radv_create_merged_rt_create_info(pCreateInfo);

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct radv_ray_tracing_pipeline, pipeline, 1);
   VK_MULTIALLOC_DECL(&ma, struct radv_ray_tracing_stage, stages, local_create_info.stageCount);
   VK_MULTIALLOC_DECL(&ma, struct radv_ray_tracing_group, groups, local_create_info.groupCount);
   if (!vk_multialloc_zalloc2(&ma, &device->vk.alloc, pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   radv_pipeline_init(device, &pipeline->base.base, RADV_PIPELINE_RAY_TRACING);
   pipeline->stage_count = local_create_info.stageCount;
   pipeline->group_count = local_create_info.groupCount;
   pipeline->stages = stages;
   pipeline->groups = groups;

   radv_rt_fill_stage_info(pCreateInfo, stages);
   result = radv_rt_fill_group_info(device, pCreateInfo, pipeline->groups);
   if (result != VK_SUCCESS)
      goto done;

   struct radv_pipeline_key key =
      radv_generate_rt_pipeline_key(device, pipeline, pCreateInfo->flags);

   radv_hash_rt_shaders(pipeline->sha1, pCreateInfo, &key, pipeline->groups,
                        radv_get_hash_flags(device, keep_statistic_info));
   pipeline->base.base.pipeline_hash = *(uint64_t *)pipeline->sha1;

   if (pCreateInfo->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) {
      result = radv_rt_precompile_shaders(device, cache, pCreateInfo, creation_feedback, &key,
                                          pipeline->stages);
      goto done;
   }

   result = radv_rt_pipeline_compile(pipeline, pipeline_layout, device, cache, &key, pCreateInfo,
                                     creation_feedback);

   if (result != VK_SUCCESS)
      goto done;

   compute_rt_stack_size(pCreateInfo, pipeline);
   pipeline->base.base.shaders[MESA_SHADER_COMPUTE] = radv_create_rt_prolog(device);

   combine_config(&pipeline->base.base.shaders[MESA_SHADER_COMPUTE]->config,
                  &pipeline->base.base.shaders[MESA_SHADER_RAYGEN]->config);

   postprocess_rt_config(&pipeline->base.base.shaders[MESA_SHADER_COMPUTE]->config,
                         device->physical_device->rad_info.gfx_level,
                         device->physical_device->rt_wave_size);

   radv_compute_pipeline_init(device, &pipeline->base, pipeline_layout);

   radv_rmv_log_compute_pipeline_create(device, pCreateInfo->flags, &pipeline->base.base, false);

done:
   if (creation_feedback)
      creation_feedback->pPipelineCreationFeedback->duration = os_time_get_nano() - pipeline_start;

   if (result == VK_SUCCESS)
      *pPipeline = radv_pipeline_to_handle(&pipeline->base.base);
   else
      radv_pipeline_destroy(device, &pipeline->base.base, pAllocator);
   return result;
}

void
radv_destroy_ray_tracing_pipeline(struct radv_device *device,
                                  struct radv_ray_tracing_pipeline *pipeline)
{
   for (unsigned i = 0; i < pipeline->stage_count; i++) {
      if (pipeline->stages[i].shader)
         vk_pipeline_cache_object_unref(&device->vk, pipeline->stages[i].shader);
   }

   if (pipeline->base.base.shaders[MESA_SHADER_COMPUTE])
      radv_shader_unref(device, pipeline->base.base.shaders[MESA_SHADER_COMPUTE]);
   if (pipeline->base.base.shaders[MESA_SHADER_RAYGEN])
      radv_shader_unref(device, pipeline->base.base.shaders[MESA_SHADER_RAYGEN]);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateRayTracingPipelinesKHR(VkDevice _device, VkDeferredOperationKHR deferredOperation,
                                  VkPipelineCache pipelineCache, uint32_t count,
                                  const VkRayTracingPipelineCreateInfoKHR *pCreateInfos,
                                  const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;

   unsigned i = 0;
   for (; i < count; i++) {
      VkResult r;
      r = radv_rt_pipeline_create(_device, pipelineCache, &pCreateInfos[i], pAllocator,
                                  &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; ++i)
      pPipelines[i] = VK_NULL_HANDLE;

   if (result != VK_SUCCESS)
      return result;

   /* Work around Portal RTX not handling VK_OPERATION_NOT_DEFERRED_KHR correctly. */
   if (deferredOperation != VK_NULL_HANDLE)
      return VK_OPERATION_DEFERRED_KHR;

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline _pipeline, uint32_t firstGroup,
                                        uint32_t groupCount, size_t dataSize, void *pData)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
   struct radv_ray_tracing_group *groups = radv_pipeline_to_ray_tracing(pipeline)->groups;
   char *data = pData;

   STATIC_ASSERT(sizeof(struct radv_pipeline_group_handle) <= RADV_RT_HANDLE_SIZE);

   memset(data, 0, groupCount * RADV_RT_HANDLE_SIZE);

   for (uint32_t i = 0; i < groupCount; ++i) {
      memcpy(data + i * RADV_RT_HANDLE_SIZE, &groups[firstGroup + i].handle,
             sizeof(struct radv_pipeline_group_handle));
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkDeviceSize VKAPI_CALL
radv_GetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline _pipeline, uint32_t group,
                                          VkShaderGroupShaderKHR groupShader)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
   struct radv_ray_tracing_pipeline *rt_pipeline = radv_pipeline_to_ray_tracing(pipeline);
   struct radv_ray_tracing_group *rt_group = &rt_pipeline->groups[group];
   switch (groupShader) {
   case VK_SHADER_GROUP_SHADER_GENERAL_KHR:
   case VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR:
      return rt_pipeline->stages[rt_group->recursive_shader].stack_size;
   case VK_SHADER_GROUP_SHADER_ANY_HIT_KHR:
      return rt_pipeline->stages[rt_group->any_hit_shader].stack_size;
   case VK_SHADER_GROUP_SHADER_INTERSECTION_KHR:
      return rt_pipeline->stages[rt_group->intersection_shader].stack_size;
   default:
      return 0;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetRayTracingCaptureReplayShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline,
                                                     uint32_t firstGroup, uint32_t groupCount,
                                                     size_t dataSize, void *pData)
{
   return radv_GetRayTracingShaderGroupHandlesKHR(device, pipeline, firstGroup, groupCount,
                                                  dataSize, pData);
}
