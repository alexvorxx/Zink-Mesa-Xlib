#include "nvk_physical_device.h"

#include "nvk_bo_sync.h"
#include "nvk_buffer.h"
#include "nvk_entrypoints.h"
#include "nvk_format.h"
#include "nvk_image.h"
#include "nvk_instance.h"
#include "nvk_shader.h"
#include "nvk_wsi.h"
#include "git_sha1.h"
#include "util/mesa-sha1.h"

#include "vulkan/runtime/vk_device.h"
#include "vulkan/runtime/vk_drm_syncobj.h"
#include "vulkan/wsi/wsi_common.h"

#include <sys/stat.h>
#include <sys/sysmacros.h>

#include "cl90c0.h"
#include "cl91c0.h"
#include "cla097.h"
#include "cla0c0.h"
#include "cla1c0.h"
#include "clb097.h"
#include "clb0c0.h"
#include "clb097.h"
#include "clb197.h"
#include "clb1c0.h"
#include "clc0c0.h"
#include "clc1c0.h"
#include "clc3c0.h"
#include "clc597.h"
#include "clc5c0.h"
#include "clc597.h"
#include "clc997.h"

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance _instance, const char *pName)
{
   VK_FROM_HANDLE(nvk_instance, instance, _instance);
   return vk_instance_get_physical_device_proc_addr(&instance->vk, pName);
}

static void
nvk_get_device_extensions(const struct nv_device_info *info,
                          struct vk_device_extension_table *ext)
{
   *ext = (struct vk_device_extension_table) {
      .KHR_bind_memory2 = true,
      .KHR_buffer_device_address = true,
      .KHR_copy_commands2 = true,
      .KHR_create_renderpass2 = true,
      .KHR_dedicated_allocation = true,
      .KHR_depth_stencil_resolve = true,
      .KHR_descriptor_update_template = true,
      .KHR_device_group = true,
      .KHR_draw_indirect_count = info->cls_eng3d >= TURING_A,
      .KHR_driver_properties = true,
      .KHR_dynamic_rendering = true,
      .KHR_external_fence = NVK_NEW_UAPI,
      .KHR_external_fence_fd = NVK_NEW_UAPI,
      .KHR_external_memory = true,
      .KHR_external_memory_fd = true,
      .KHR_external_semaphore = NVK_NEW_UAPI,
      .KHR_external_semaphore_fd = NVK_NEW_UAPI,
      .KHR_format_feature_flags2 = true,
      .KHR_get_memory_requirements2 = true,
      .KHR_image_format_list = true,
      .KHR_imageless_framebuffer = true,
      .KHR_maintenance1 = true,
      .KHR_maintenance2 = true,
      .KHR_maintenance3 = true,
      .KHR_maintenance4 = true,
      .KHR_map_memory2 = true,
      .KHR_multiview = true,
      .KHR_push_descriptor = true,
      .KHR_relaxed_block_layout = true,
      .KHR_sampler_mirror_clamp_to_edge = true,
      .KHR_sampler_ycbcr_conversion = true,
      .KHR_separate_depth_stencil_layouts = true,
      .KHR_shader_clock = true,
      .KHR_shader_draw_parameters = true,
      .KHR_shader_non_semantic_info = true,
      .KHR_spirv_1_4 = true,
      .KHR_storage_buffer_storage_class = true,
#if NVK_NEW_UAPI == 1
      .KHR_timeline_semaphore = true,
#endif
#ifdef NVK_USE_WSI_PLATFORM
      .KHR_swapchain = true,
      .KHR_swapchain_mutable_format = true,
#endif
      .KHR_uniform_buffer_standard_layout = true,
      .KHR_variable_pointers = true,
      .EXT_4444_formats = true,
      .EXT_border_color_swizzle = true,
      .EXT_buffer_device_address = true,
      .EXT_conditional_rendering = true,
      .EXT_custom_border_color = true,
      .EXT_depth_clip_control = true,
      .EXT_depth_clip_enable = true,
      .EXT_descriptor_indexing = true,
      .EXT_extended_dynamic_state = true,
      .EXT_extended_dynamic_state2 = true,
      .EXT_extended_dynamic_state3 = true,
      .EXT_external_memory_dma_buf = true,
      .EXT_host_query_reset = true,
      .EXT_image_2d_view_of_3d = true,
      .EXT_image_robustness = true,
      .EXT_image_view_min_lod = true,
      .EXT_index_type_uint8 = true,
      .EXT_inline_uniform_block = true,
      .EXT_line_rasterization = true,
      .EXT_mutable_descriptor_type = true,
      .EXT_non_seamless_cube_map = true,
      .EXT_pci_bus_info = info->type == NV_DEVICE_TYPE_DIS,
      .EXT_physical_device_drm = true,
      .EXT_private_data = true,
      .EXT_provoking_vertex = true,
      .EXT_robustness2 = true,
      .EXT_sample_locations = info->cls_eng3d >= MAXWELL_B,
      .EXT_sampler_filter_minmax = info->cls_eng3d >= MAXWELL_B,
      .EXT_separate_stencil_usage = true,
      .EXT_shader_demote_to_helper_invocation = true,
      .EXT_shader_viewport_index_layer = info->cls_eng3d >= MAXWELL_B,
      .EXT_tooling_info = true,
      .EXT_transform_feedback = true,
      .EXT_vertex_attribute_divisor = true,
      .EXT_vertex_input_dynamic_state = true,
      .EXT_ycbcr_2plane_444_formats = true,
      .EXT_ycbcr_image_arrays = true,
   };
}

static void
nvk_get_device_features(const struct nv_device_info *info,
                        struct vk_features *features)
{
   *features = (struct vk_features) {
      /* Vulkan 1.0 */
      .robustBufferAccess = true,
      .fullDrawIndexUint32 = true,
      .imageCubeArray = true,
      .independentBlend = true,
      .geometryShader = true,
      .tessellationShader = true,
      .sampleRateShading = true,
      .dualSrcBlend = true,
      .logicOp = true,
      .multiDrawIndirect = true,
      .drawIndirectFirstInstance = true,
      .depthClamp = true,
      .depthBiasClamp = true,
      .fillModeNonSolid = true,
      .depthBounds = true,
      .wideLines = true,
      .largePoints = true,
      .alphaToOne = true,
      .multiViewport = true,
      .samplerAnisotropy = true,
      .textureCompressionETC2 = false,
      .textureCompressionBC = true,
      .textureCompressionASTC_LDR = false,
      .occlusionQueryPrecise = true,
      .pipelineStatisticsQuery = true,
      .vertexPipelineStoresAndAtomics = true,
      .fragmentStoresAndAtomics = true,
      .shaderTessellationAndGeometryPointSize = true,
      .shaderImageGatherExtended = true,
      .shaderStorageImageExtendedFormats = true,
      /* TODO: shaderStorageImageMultisample */
      .shaderStorageImageReadWithoutFormat = info->cls_eng3d >= MAXWELL_A,
      .shaderStorageImageWriteWithoutFormat = true,
      .shaderUniformBufferArrayDynamicIndexing = true,
      .shaderSampledImageArrayDynamicIndexing = true,
      .shaderStorageBufferArrayDynamicIndexing = true,
      .shaderStorageImageArrayDynamicIndexing = true,
      .shaderClipDistance = true,
      .shaderCullDistance = true,
      /* TODO: shaderFloat64 */
      /* TODO: shaderInt64 */
      /* TODO: shaderInt16 */
      /* TODO: shaderResourceResidency */
      .shaderResourceMinLod = true,
#if NVK_NEW_UAPI == 1
      .sparseBinding = true,
      .sparseResidencyBuffer = info->cls_eng3d >= MAXWELL_A,
#endif
      /* TODO: sparseResidency* */
      /* TODO: variableMultisampleRate */
      /* TODO: inheritedQueries */
      .inheritedQueries = true,

      /* Vulkan 1.1 */
      .multiview = true,
      .multiviewGeometryShader = true,
      .multiviewTessellationShader = true,
      .variablePointersStorageBuffer = true,
      .variablePointers = true,
      .shaderDrawParameters = true,
      .samplerYcbcrConversion = true,

      /* Vulkan 1.2 */
      .samplerMirrorClampToEdge = true,
      .shaderInputAttachmentArrayDynamicIndexing = true,
      .shaderUniformTexelBufferArrayDynamicIndexing = true,
      .shaderStorageTexelBufferArrayDynamicIndexing = true,
      .shaderUniformBufferArrayNonUniformIndexing = true,
      .shaderSampledImageArrayNonUniformIndexing = true,
      .shaderStorageBufferArrayNonUniformIndexing = true,
      .shaderStorageImageArrayNonUniformIndexing = true,
      .shaderInputAttachmentArrayNonUniformIndexing = true,
      .shaderUniformTexelBufferArrayNonUniformIndexing = true,
      .shaderStorageTexelBufferArrayNonUniformIndexing = true,
      .descriptorBindingUniformBufferUpdateAfterBind = true,
      .descriptorBindingSampledImageUpdateAfterBind = true,
      .descriptorBindingStorageImageUpdateAfterBind = true,
      .descriptorBindingStorageBufferUpdateAfterBind = true,
      .descriptorBindingUniformTexelBufferUpdateAfterBind = true,
      .descriptorBindingStorageTexelBufferUpdateAfterBind = true,
      .descriptorBindingUpdateUnusedWhilePending = true,
      .descriptorBindingPartiallyBound = true,
      .descriptorBindingVariableDescriptorCount = true,
      .runtimeDescriptorArray = true,
      .imagelessFramebuffer = true,
      .uniformBufferStandardLayout = true,
      .separateDepthStencilLayouts = true,
      .hostQueryReset = true,
#if NVK_NEW_UAPI == 1
      .timelineSemaphore = true,
#endif
      .bufferDeviceAddress = true,
      .bufferDeviceAddressCaptureReplay = false,
      .bufferDeviceAddressMultiDevice = false,
      .drawIndirectCount = info->cls_eng3d >= TURING_A,
      .samplerFilterMinmax = info->cls_eng3d >= MAXWELL_B,
      .conditionalRendering = true,
      .inheritedConditionalRendering = true,

      /* Vulkan 1.3 */
      .robustImageAccess = true,
      .inlineUniformBlock = true,
      .descriptorBindingInlineUniformBlockUpdateAfterBind = true,
      .privateData = true,
      .shaderDemoteToHelperInvocation = true,
      .dynamicRendering = true,
      .maintenance4 = true,

      /* VK_EXT_4444_formats */
      .formatA4R4G4B4 = true,
      .formatA4B4G4R4 = true,

      /* VK_EXT_border_color_swizzle */
      .borderColorSwizzle = true,
      .borderColorSwizzleFromImage = false,

      /* VK_EXT_buffer_device_address */
      .bufferDeviceAddressCaptureReplayEXT = false,

      /* VK_EXT_custom_border_color */
      .customBorderColors = true,
      .customBorderColorWithoutFormat = true,

      /* VK_EXT_depth_clip_control */
      .depthClipControl = true,

      /* VK_EXT_depth_clip_enable */
      .depthClipEnable = true,

      /* VK_EXT_extended_dynamic_state */
      .extendedDynamicState = true,

      /* VK_EXT_extended_dynamic_state2 */
      .extendedDynamicState2 = true,
      .extendedDynamicState2LogicOp = true,
      .extendedDynamicState2PatchControlPoints = true,

      /* VK_EXT_extended_dynamic_state3 */
      .extendedDynamicState3TessellationDomainOrigin = false,
      .extendedDynamicState3DepthClampEnable = false,
      .extendedDynamicState3PolygonMode = true,
      .extendedDynamicState3RasterizationSamples = false,
      .extendedDynamicState3SampleMask = false,
      .extendedDynamicState3AlphaToCoverageEnable = false,
      .extendedDynamicState3AlphaToOneEnable = false,
      .extendedDynamicState3LogicOpEnable = true,
      .extendedDynamicState3ColorBlendEnable = false,
      .extendedDynamicState3ColorBlendEquation = false,
      .extendedDynamicState3ColorWriteMask = false,
      .extendedDynamicState3RasterizationStream = false,
      .extendedDynamicState3ConservativeRasterizationMode = false,
      .extendedDynamicState3ExtraPrimitiveOverestimationSize = false,
      .extendedDynamicState3DepthClipEnable = false,
      .extendedDynamicState3SampleLocationsEnable = info->cls_eng3d >= MAXWELL_B,
      .extendedDynamicState3ColorBlendAdvanced = false,
      .extendedDynamicState3ProvokingVertexMode = true,
      .extendedDynamicState3LineRasterizationMode = true,
      .extendedDynamicState3LineStippleEnable = true,
      .extendedDynamicState3DepthClipNegativeOneToOne = true,
      .extendedDynamicState3ViewportWScalingEnable = false,
      .extendedDynamicState3ViewportSwizzle = false,
      .extendedDynamicState3CoverageToColorEnable = false,
      .extendedDynamicState3CoverageToColorLocation = false,
      .extendedDynamicState3CoverageModulationMode = false,
      .extendedDynamicState3CoverageModulationTableEnable = false,
      .extendedDynamicState3CoverageModulationTable = false,
      .extendedDynamicState3CoverageReductionMode = false,
      .extendedDynamicState3RepresentativeFragmentTestEnable = false,
      .extendedDynamicState3ShadingRateImageEnable = false,

      /* VK_EXT_image_2d_view_of_3d */
      .image2DViewOf3D = true,
      .sampler2DViewOf3D = true,

      /* VK_EXT_image_view_min_lod */
      .minLod = true,

      /* VK_EXT_index_type_uint8 */
      .indexTypeUint8 = true,

      /* VK_EXT_line_rasterization */
      .rectangularLines = true,
      .bresenhamLines = true,
      .smoothLines = true,
      .stippledRectangularLines = true,
      .stippledBresenhamLines = true,
      .stippledSmoothLines = true,

      /* VK_EXT_non_seamless_cube_map */
      .nonSeamlessCubeMap = true,

      /* VK_EXT_provoking_vertex */
      .provokingVertexLast = true,
      .transformFeedbackPreservesProvokingVertex = true,

      /* VK_EXT_robustness2 */
      .robustBufferAccess2 = true,
      .robustImageAccess2 = true,
      .nullDescriptor = true,

      /* VK_EXT_transform_feedback */
      .transformFeedback = true,
      .geometryStreams = true,

      /* VK_EXT_vertex_attribute_divisor */
      .vertexAttributeInstanceRateDivisor = true,
      .vertexAttributeInstanceRateZeroDivisor = true,

      /* VK_EXT_vertex_input_dynamic_state */
      .vertexInputDynamicState = true,

      /* VK_EXT_ycbcr_2plane_444_formats */
      .ycbcr2plane444Formats = true,

      /* VK_EXT_ycbcr_image_arrays */
      .ycbcrImageArrays = true,

      /* VALVE_mutable_descriptor_type */
      .mutableDescriptorType = true,

      /* VK_KHR_shader_clock */
      .shaderSubgroupClock = true,
      .shaderDeviceClock = true,
   };
}

static void
nvk_get_device_properties(const struct nvk_instance *instance,
                          const struct nv_device_info *info,
                          struct vk_properties *properties)
{
   const VkSampleCountFlagBits sample_counts = VK_SAMPLE_COUNT_1_BIT |
                                               VK_SAMPLE_COUNT_2_BIT |
                                               VK_SAMPLE_COUNT_4_BIT |
                                               VK_SAMPLE_COUNT_8_BIT;

   *properties = (struct vk_properties) {
      .apiVersion = VK_MAKE_VERSION(1, 0, VK_HEADER_VERSION),
      .driverVersion = vk_get_driver_version(),
      .vendorID = NVIDIA_VENDOR_ID,
      .deviceID = info->device_id,
      .deviceType = info->type == NV_DEVICE_TYPE_DIS ?
                    VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU :
                    VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,

      /* Vulkan 1.0 limits */
      .maxImageArrayLayers = 2048,
      .maxImageDimension1D = nvk_image_max_dimension(info, VK_IMAGE_TYPE_1D),
      .maxImageDimension2D = nvk_image_max_dimension(info, VK_IMAGE_TYPE_2D),
      .maxImageDimension3D = nvk_image_max_dimension(info, VK_IMAGE_TYPE_3D),
      .maxImageDimensionCube = 0x8000,
      .maxPushConstantsSize = NVK_MAX_PUSH_SIZE,
      .maxMemoryAllocationCount = 1024,
      .bufferImageGranularity = info->chipset >= 0x120 ? 0x400 : 0x10000,
      .maxFramebufferHeight = info->chipset >= 0x130 ? 0x8000 : 0x4000,
      .maxFramebufferWidth = info->chipset >= 0x130 ? 0x8000 : 0x4000,
      .maxFramebufferLayers = 2048,
      .maxColorAttachments = NVK_MAX_RTS,
      .maxClipDistances = 8,
      .maxCullDistances = 8,
      .maxCombinedClipAndCullDistances = 8,
      .maxFragmentCombinedOutputResources = 16,
      .maxFragmentInputComponents = 128,
      .maxFragmentOutputAttachments = NVK_MAX_RTS,
      .maxFragmentDualSrcAttachments = 1,
      .maxSamplerAllocationCount = 4000,
      .maxSamplerLodBias = 15,
      .maxSamplerAnisotropy = 16,
      .maxSampleMaskWords = 1,
      .minTexelGatherOffset = -32,
      .minTexelOffset = -8,
      .maxTexelGatherOffset = 31,
      .maxTexelOffset = 7,
      .minInterpolationOffset = -0.5,
      .maxInterpolationOffset = 0.4375,
      .mipmapPrecisionBits = 8,
      .subPixelInterpolationOffsetBits = 4,
      .subPixelPrecisionBits = 8,
      .subTexelPrecisionBits = 8,
      .viewportSubPixelBits = 8,
      .maxUniformBufferRange = 65536,
      .maxStorageBufferRange = UINT32_MAX,
      .maxTexelBufferElements = 128 * 1024 * 1024,
      .maxBoundDescriptorSets = NVK_MAX_SETS,
      .maxPerStageDescriptorSamplers = UINT32_MAX,
      .maxPerStageDescriptorUniformBuffers = UINT32_MAX,
      .maxPerStageDescriptorStorageBuffers = UINT32_MAX,
      .maxPerStageDescriptorSampledImages = UINT32_MAX,
      .maxPerStageDescriptorStorageImages = UINT32_MAX,
      .maxPerStageDescriptorInputAttachments = UINT32_MAX,
      .maxPerStageResources = UINT32_MAX,
      .maxDescriptorSetSamplers = UINT32_MAX,
      .maxDescriptorSetUniformBuffers = UINT32_MAX,
      .maxDescriptorSetUniformBuffersDynamic = NVK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetStorageBuffers = UINT32_MAX,
      .maxDescriptorSetStorageBuffersDynamic = NVK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetSampledImages = UINT32_MAX,
      .maxDescriptorSetStorageImages = UINT32_MAX,
      .maxDescriptorSetInputAttachments = UINT32_MAX,
      .maxComputeSharedMemorySize = 49152,
      .maxComputeWorkGroupCount = {0x7fffffff, 65535, 65535},
      .maxComputeWorkGroupInvocations = 1024,
      .maxComputeWorkGroupSize = {1024, 1024, 64},
      .maxViewports = NVK_MAX_VIEWPORTS,
      .maxViewportDimensions = { 32768, 32768 },
      .viewportBoundsRange = { -65536, 65536 },
      .pointSizeRange = { 1.0, 2047.94 },
      .pointSizeGranularity = 0.0625,
      .lineWidthRange = { 1, 64 },
      .lineWidthGranularity = 0.0625,
      .nonCoherentAtomSize = 64,
      .minMemoryMapAlignment = 64,
      .minUniformBufferOffsetAlignment =
         nvk_get_buffer_alignment(info, VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR, 0),
      .minTexelBufferOffsetAlignment =
         nvk_get_buffer_alignment(info, VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT_KHR |
                                        VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT_KHR,
                                  0),
      .minStorageBufferOffsetAlignment =
         nvk_get_buffer_alignment(info, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR, 0),
      .maxVertexInputAttributeOffset = 2047,
      .maxVertexInputAttributes = 32,
      .maxVertexInputBindingStride = 2048,
      .maxVertexInputBindings = 32,
      .maxVertexOutputComponents = 128,
      .maxTessellationGenerationLevel = 64,
      .maxTessellationPatchSize = 32,
      .maxTessellationControlPerVertexInputComponents = 128,
      .maxTessellationControlPerVertexOutputComponents = 128,
      .maxTessellationControlPerPatchOutputComponents = 120,
      .maxTessellationControlTotalOutputComponents = 4216,
      .maxTessellationEvaluationInputComponents = 128,
      .maxTessellationEvaluationOutputComponents = 128,
      .maxGeometryShaderInvocations = 32,
      .maxGeometryInputComponents = 128,
      .maxGeometryOutputComponents = 128,
      .maxGeometryOutputVertices = 1024,
      .maxGeometryTotalOutputComponents = 1024,
      .maxDrawIndexedIndexValue = UINT32_MAX,
      .maxDrawIndirectCount = UINT32_MAX,
      .timestampComputeAndGraphics = true,
      .timestampPeriod = 1,
      .framebufferColorSampleCounts = sample_counts,
      .framebufferDepthSampleCounts = sample_counts,
      .framebufferNoAttachmentsSampleCounts = sample_counts,
      .framebufferStencilSampleCounts = sample_counts,
      .sampledImageColorSampleCounts = sample_counts,
      .sampledImageDepthSampleCounts = sample_counts,
      .sampledImageIntegerSampleCounts = sample_counts,
      .sampledImageStencilSampleCounts = sample_counts,
      .storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT,
      .standardSampleLocations = true,
      .strictLines = true,
      .optimalBufferCopyOffsetAlignment = 1,
      .optimalBufferCopyRowPitchAlignment = 1,
      .bufferImageGranularity = 1,
      .sparseAddressSpaceSize = UINT32_MAX,

      /* Vulkan 1.0 sparse properties */
      .sparseResidencyNonResidentStrict = true,

      /* Vulkan 1.1 properties */
      .pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_USER_CLIP_PLANES_ONLY,
      .maxMultiviewViewCount = NVK_MAX_MULTIVIEW_VIEW_COUNT,
      .maxMultiviewInstanceIndex = UINT32_MAX,
      .maxPerSetDescriptors = UINT32_MAX,
      .maxMemoryAllocationSize = (1u << 31),

      /* Vulkan 1.2 properties */
      .supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                    VK_RESOLVE_MODE_AVERAGE_BIT |
                                    VK_RESOLVE_MODE_MIN_BIT |
                                    VK_RESOLVE_MODE_MAX_BIT,
      .supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                      VK_RESOLVE_MODE_MIN_BIT |
                                      VK_RESOLVE_MODE_MAX_BIT,
      .independentResolveNone = true,
      .independentResolve = true,
      .driverID = VK_DRIVER_ID_MESA_NVK,
      .conformanceVersion = (VkConformanceVersion) { /* TODO: conf version */
         .major = 0,
         .minor = 0,
         .subminor = 0,
         .patch = 0,
      },
      .maxUpdateAfterBindDescriptorsInAllPools = UINT32_MAX,
      .shaderUniformBufferArrayNonUniformIndexingNative = false,
      .shaderSampledImageArrayNonUniformIndexingNative = info->cls_eng3d >= TURING_A,
      .shaderStorageBufferArrayNonUniformIndexingNative = true,
      .shaderStorageImageArrayNonUniformIndexingNative = info->cls_eng3d >= TURING_A,
      .shaderInputAttachmentArrayNonUniformIndexingNative = false,
      .robustBufferAccessUpdateAfterBind = true,
      .quadDivergentImplicitLod = info->cls_eng3d >= TURING_A,
      .maxPerStageDescriptorUpdateAfterBindSamplers = UINT32_MAX,
      .maxPerStageDescriptorUpdateAfterBindUniformBuffers = UINT32_MAX,
      .maxPerStageDescriptorUpdateAfterBindStorageBuffers = UINT32_MAX,
      .maxPerStageDescriptorUpdateAfterBindSampledImages = UINT32_MAX,
      .maxPerStageDescriptorUpdateAfterBindStorageImages = UINT32_MAX,
      .maxPerStageDescriptorUpdateAfterBindInputAttachments = UINT32_MAX,
      .maxPerStageUpdateAfterBindResources = UINT32_MAX,
      .maxDescriptorSetUpdateAfterBindSamplers = UINT32_MAX,
      .maxDescriptorSetUpdateAfterBindUniformBuffers = UINT32_MAX,
      .maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = NVK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetUpdateAfterBindStorageBuffers = UINT32_MAX,
      .maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = NVK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetUpdateAfterBindSampledImages = UINT32_MAX,
      .maxDescriptorSetUpdateAfterBindStorageImages = UINT32_MAX,
      .maxDescriptorSetUpdateAfterBindInputAttachments = UINT32_MAX,
      .filterMinmaxSingleComponentFormats = true,
      .filterMinmaxImageComponentMapping = true,
      .maxTimelineSemaphoreValueDifference = UINT64_MAX,

      /* Vulkan 1.3 properties */
      .maxInlineUniformBlockSize = 1 << 16,
      .maxPerStageDescriptorInlineUniformBlocks = 32,
      .maxBufferSize = UINT32_MAX,

      /* VK_KHR_push_descriptor */
      .maxPushDescriptors = NVK_MAX_PUSH_DESCRIPTORS,

      /* VK_EXT_custom_border_color */
      .maxCustomBorderColorSamplers = 4000,

      /* VK_EXT_extended_dynamic_state3 */
      .dynamicPrimitiveTopologyUnrestricted = true,

      /* VK_EXT_line_rasterization */
      .lineSubPixelPrecisionBits = 8,

      /* VK_EXT_pci_bus_info */
      .pciDomain   = info->pci.domain,
      .pciBus      = info->pci.bus,
      .pciDevice   = info->pci.dev,
      .pciFunction = info->pci.func,

      /* VK_EXT_physical_device_drm gets populated later */

      /* VK_EXT_provoking_vertex */
      .provokingVertexModePerPipeline = true,
      .transformFeedbackPreservesTriangleFanProvokingVertex = true,

      /* VK_EXT_robustness2 */
      .robustStorageBufferAccessSizeAlignment = NVK_SSBO_BOUNDS_CHECK_ALIGNMENT,
      .robustUniformBufferAccessSizeAlignment = NVK_MIN_UBO_ALIGNMENT,

      /* VK_EXT_sample_locations */
      .sampleLocationSampleCounts = sample_counts,
      .maxSampleLocationGridSize = (VkExtent2D){ 1, 1 },
      .sampleLocationCoordinateRange[0] = 0.0f,
      .sampleLocationCoordinateRange[1] = 0.9375f,
      .sampleLocationSubPixelBits = 4,
      .variableSampleLocations = true,

      /* VK_EXT_transform_feedback */
      .maxTransformFeedbackStreams = 4,
      .maxTransformFeedbackBuffers = 4,
      .maxTransformFeedbackBufferSize = UINT32_MAX,
      .maxTransformFeedbackStreamDataSize = 2048,
      .maxTransformFeedbackBufferDataSize = 512,
      .maxTransformFeedbackBufferDataStride = 2048,
      .transformFeedbackQueries = true,
      .transformFeedbackStreamsLinesTriangles = false,
      .transformFeedbackRasterizationStreamSelect = true,
      .transformFeedbackDraw = true,

      /* VK_EXT_vertex_attribute_divisor */
      .maxVertexAttribDivisor = UINT32_MAX,
   };

   snprintf(properties->deviceName, sizeof(properties->deviceName),
            "%s", info->device_name);

   const struct {
      uint16_t vendor_id;
      uint16_t device_id;
      uint8_t pad[12];
   } dev_uuid = {
      .vendor_id = NVIDIA_VENDOR_ID,
      .device_id = info->device_id,
   };
   STATIC_ASSERT(sizeof(dev_uuid) == VK_UUID_SIZE);
   memcpy(properties->deviceUUID, &dev_uuid, VK_UUID_SIZE);
   memcpy(properties->driverUUID, instance->driver_uuid, VK_UUID_SIZE);

   snprintf(properties->driverName, VK_MAX_DRIVER_NAME_SIZE, "NVK");
   snprintf(properties->driverInfo, VK_MAX_DRIVER_INFO_SIZE,
            "Mesa " PACKAGE_VERSION MESA_GIT_SHA1);
}

VkResult
nvk_create_drm_physical_device(struct vk_instance *_instance,
                               drmDevicePtr drm_device,
                               struct vk_physical_device **pdev_out)
{
   struct nvk_instance *instance = (struct nvk_instance *)_instance;
   VkResult result;

   if (!(drm_device->available_nodes & (1 << DRM_NODE_RENDER)))
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   switch (drm_device->bustype) {
   case DRM_BUS_PCI:
      if (drm_device->deviceinfo.pci->vendor_id != NVIDIA_VENDOR_ID)
         return VK_ERROR_INCOMPATIBLE_DRIVER;
      break;

   case DRM_BUS_PLATFORM: {
      const char *compat_prefix = "nvidia,";
      bool found = false;
      for (int i = 0; drm_device->deviceinfo.platform->compatible[i] != NULL; i++) {
         if (strncmp(drm_device->deviceinfo.platform->compatible[0], compat_prefix, strlen(compat_prefix)) == 0) {
            found = true;
            break;
         }
      }
      if (!found)
         return VK_ERROR_INCOMPATIBLE_DRIVER;
      break;
   }

   default:
      return VK_ERROR_INCOMPATIBLE_DRIVER;
   }

   struct nouveau_ws_device *ws_dev = nouveau_ws_device_new(drm_device);
   if (!ws_dev)
      return vk_error(instance, VK_ERROR_INCOMPATIBLE_DRIVER);

   const struct nv_device_info info = ws_dev->info;
#if NVK_NEW_UAPI == 1
   const bool has_vm_bind = ws_dev->has_vm_bind;
   const struct vk_sync_type syncobj_sync_type =
      vk_drm_syncobj_get_type(ws_dev->fd);
#endif

   nouveau_ws_device_destroy(ws_dev);

   /* We don't support anything pre-Kepler */
   if (info.cls_eng3d < KEPLER_A)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   if ((info.cls_eng3d < TURING_A || info.cls_eng3d > ADA_A) &&
       !debug_get_bool_option("NVK_I_WANT_A_BROKEN_VULKAN_DRIVER", false)) {
      return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                       "WARNING: NVK is not well-tested on %s, pass "
                       "NVK_I_WANT_A_BROKEN_VULKAN_DRIVER=1 "
                       "if you know what you're doing.",
                       info.device_name);
   }

#if NVK_NEW_UAPI == 1
   if (!has_vm_bind) {
      return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                       "NVK Requires a Linux kernel version 6.6 or later");
   }
#endif

   if (!(drm_device->available_nodes & (1 << DRM_NODE_RENDER))) {
      return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                       "NVK requires a render node");
   }

   struct stat st;
   if (stat(drm_device->nodes[DRM_NODE_RENDER], &st)) {
      return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                       "fstat() failed on %s: %m",
                       drm_device->nodes[DRM_NODE_RENDER]);
   }
   const dev_t render_dev = st.st_rdev;

   vk_warn_non_conformant_implementation("NVK");

   struct nvk_physical_device *pdev =
      vk_zalloc(&instance->vk.alloc, sizeof(*pdev),
                8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

   if (pdev == NULL)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &nvk_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_physical_device_entrypoints, false);

   struct vk_device_extension_table supported_extensions;
   nvk_get_device_extensions(&info, &supported_extensions);

   struct vk_features supported_features;
   nvk_get_device_features(&info, &supported_features);

   struct vk_properties properties;
   nvk_get_device_properties(instance, &info, &properties);

   properties.drmHasRender = true;
   properties.drmRenderMajor = major(render_dev);
   properties.drmRenderMinor = minor(render_dev);

   /* DRM primary is optional */
   if ((drm_device->available_nodes & (1 << DRM_NODE_PRIMARY)) &&
       !stat(drm_device->nodes[DRM_NODE_PRIMARY], &st)) {
      assert(st.st_rdev != 0);
      properties.drmHasPrimary = true;
      properties.drmPrimaryMajor = major(st.st_rdev);
      properties.drmPrimaryMinor = minor(st.st_rdev);
   }

   result = vk_physical_device_init(&pdev->vk, &instance->vk,
                                    &supported_extensions,
                                    &supported_features,
                                    &properties,
                                    &dispatch_table);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   pdev->render_dev = render_dev;
   pdev->info = info;

   pdev->mem_heaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
   pdev->mem_types[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
   pdev->mem_types[0].heapIndex = 0;

   uint64_t sysmem_size_B = 0;
   if (!os_get_available_system_memory(&sysmem_size_B)) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to query available system memory");
      goto fail_init;
   }

   if (pdev->info.vram_size_B) {
      pdev->mem_type_cnt = 2;
      pdev->mem_heap_cnt = 2;

      pdev->mem_heaps[0].size = pdev->info.vram_size_B;
      pdev->mem_heaps[1].size = sysmem_size_B;
      pdev->mem_heaps[1].flags = 0;
      pdev->mem_types[1].heapIndex = 1;
      pdev->mem_types[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   } else {
      pdev->mem_type_cnt = 1;
      pdev->mem_heap_cnt = 1;

      pdev->mem_heaps[0].size = sysmem_size_B;
      pdev->mem_types[0].propertyFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   }

   unsigned st_idx = 0;
#if NVK_NEW_UAPI == 1
   pdev->syncobj_sync_type = syncobj_sync_type;
   pdev->sync_types[st_idx++] = &pdev->syncobj_sync_type;
#else
   pdev->sync_types[st_idx++] = &nvk_bo_sync_type;
#endif
   pdev->sync_types[st_idx++] = NULL;
   assert(st_idx <= ARRAY_SIZE(pdev->sync_types));
   pdev->vk.supported_sync_types = pdev->sync_types;

   result = nvk_init_wsi(pdev);
   if (result != VK_SUCCESS)
      goto fail_init;

   *pdev_out = &pdev->vk;

   return VK_SUCCESS;

fail_init:
   vk_physical_device_finish(&pdev->vk);
fail_alloc:
   vk_free(&instance->vk.alloc, pdev);
   return result;
}

void
nvk_physical_device_destroy(struct vk_physical_device *vk_pdev)
{
   struct nvk_physical_device *pdev =
      container_of(vk_pdev, struct nvk_physical_device, vk);

   nvk_finish_wsi(pdev);
   vk_physical_device_finish(&pdev->vk);
   vk_free(&pdev->vk.instance->alloc, pdev);
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetPhysicalDeviceMemoryProperties2(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   VK_FROM_HANDLE(nvk_physical_device, pdev, physicalDevice);

   pMemoryProperties->memoryProperties.memoryHeapCount = pdev->mem_heap_cnt;
   for (int i = 0; i < pdev->mem_heap_cnt; i++) {
      pMemoryProperties->memoryProperties.memoryHeaps[i] = pdev->mem_heaps[i];
   }

   pMemoryProperties->memoryProperties.memoryTypeCount = pdev->mem_type_cnt;
   for (int i = 0; i < pdev->mem_type_cnt; i++) {
      pMemoryProperties->memoryProperties.memoryTypes[i] = pdev->mem_types[i];
   }

   vk_foreach_struct(ext, pMemoryProperties->pNext)
   {
      switch (ext->sType) {
      default:
         nvk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetPhysicalDeviceQueueFamilyProperties2(
   VkPhysicalDevice physicalDevice,
   uint32_t *pQueueFamilyPropertyCount,
   VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   // VK_FROM_HANDLE(nvk_physical_device, pdev, physicalDevice);
   VK_OUTARRAY_MAKE_TYPED(
      VkQueueFamilyProperties2, out, pQueueFamilyProperties, pQueueFamilyPropertyCount);

   vk_outarray_append_typed(VkQueueFamilyProperties2, &out, p) {
      p->queueFamilyProperties.queueFlags = VK_QUEUE_GRAPHICS_BIT |
                                            VK_QUEUE_COMPUTE_BIT |
                                            VK_QUEUE_TRANSFER_BIT;
#if NVK_NEW_UAPI == 1
      p->queueFamilyProperties.queueFlags |= VK_QUEUE_SPARSE_BINDING_BIT;
#endif
      p->queueFamilyProperties.queueCount = 1;
      p->queueFamilyProperties.timestampValidBits = 64;
      p->queueFamilyProperties.minImageTransferGranularity = (VkExtent3D){1, 1, 1};
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetPhysicalDeviceMultisamplePropertiesEXT(
   VkPhysicalDevice physicalDevice,
   VkSampleCountFlagBits samples,
   VkMultisamplePropertiesEXT *pMultisampleProperties)
{
   VK_FROM_HANDLE(nvk_physical_device, pdev, physicalDevice);

   if (samples & pdev->vk.properties.sampleLocationSampleCounts) {
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){1, 1};
   } else {
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){0, 0};
   }
}
