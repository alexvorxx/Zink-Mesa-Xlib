/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2015-2021 Advanced Micro Devices, Inc.
 * Copyright 2023 Valve Corporation
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "util/macros.h"
#include "util/u_memory.h"
#include "radv_cs.h"
#include "radv_private.h"

struct radv_sdma_linear_info {
   uint64_t va;
   unsigned pitch;
   unsigned slice_pitch;
   unsigned bpp;
   unsigned blk_w;
   unsigned blk_h;
};

struct radv_sdma_tiled_info {
   VkExtent3D extent;
   uint64_t va;
   uint64_t meta_va;
   uint32_t meta_config;
   uint32_t info_dword;
   uint32_t header_dword;
   unsigned bpp;
   unsigned blk_w;
   unsigned blk_h;
};

struct radv_sdma_chunked_copy_info {
   unsigned bpp;
   unsigned blk_w;
   unsigned blk_h;
   unsigned row_pitch_alignment;
   unsigned extent_horizontal_blocks;
   unsigned extent_vertical_blocks;
   unsigned aligned_row_pitch;
   unsigned num_rows_per_copy;
};

ALWAYS_INLINE static void
radv_sdma_check_pitches(const unsigned pitch, const unsigned slice_pitch, const unsigned bpp, const bool uses_depth)
{
   ASSERTED const unsigned pitch_alignment = MAX2(1, 4 / bpp);
   assert(pitch);
   assert(pitch <= (1 << 14));
   assert(radv_is_aligned(pitch, pitch_alignment));

   if (uses_depth) {
      ASSERTED const unsigned slice_pitch_alignment = 4;
      assert(slice_pitch);
      assert(slice_pitch <= (1 << 28));
      assert(radv_is_aligned(slice_pitch, slice_pitch_alignment));
   }
}

ALWAYS_INLINE static enum gfx9_resource_type
radv_sdma_surface_resource_type(const struct radv_device *const device, const struct radeon_surf *const surf)
{
   if (device->physical_device->rad_info.gfx_level >= GFX10) {
      /* Use the 2D resource type for rotated or Z swizzles. */
      if ((surf->u.gfx9.resource_type == RADEON_RESOURCE_1D || surf->u.gfx9.resource_type == RADEON_RESOURCE_3D) &&
          (surf->micro_tile_mode == RADEON_MICRO_MODE_RENDER || surf->micro_tile_mode == RADEON_MICRO_MODE_DEPTH))
         return RADEON_RESOURCE_2D;
   }

   return surf->u.gfx9.resource_type;
}

ALWAYS_INLINE static uint32_t
radv_sdma_surface_type_from_aspect_mask(const VkImageAspectFlags aspectMask)
{
   if (aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
      return 1;
   else if (aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
      return 2;

   return 0;
}

ALWAYS_INLINE static VkOffset3D
radv_sdma_get_img_offset(const struct radv_image *const image, const VkImageSubresourceLayers subresource,
                         VkOffset3D offset)
{
   if (image->vk.image_type != VK_IMAGE_TYPE_3D)
      offset.z = subresource.baseArrayLayer;

   return offset;
}

ALWAYS_INLINE static VkExtent3D
radv_sdma_get_copy_extent(const struct radv_image *const image, const VkImageSubresourceLayers subresource,
                          VkExtent3D extent)
{
   if (image->vk.image_type != VK_IMAGE_TYPE_3D)
      extent.depth = vk_image_subresource_layer_count(&image->vk, &subresource);

   return extent;
}

ALWAYS_INLINE static VkExtent3D
radv_sdma_get_image_extent(const struct radv_image *const image)
{
   VkExtent3D extent = image->vk.extent;
   if (image->vk.image_type != VK_IMAGE_TYPE_3D)
      extent.depth = image->vk.array_layers;

   return extent;
}

ALWAYS_INLINE static VkExtent3D
radv_sdma_pixel_extent_to_blocks(const VkExtent3D extent, const unsigned blk_w, const unsigned blk_h)
{
   const VkExtent3D r = {
      .width = DIV_ROUND_UP(extent.width, blk_w),
      .height = DIV_ROUND_UP(extent.height, blk_h),
      .depth = extent.depth,
   };

   return r;
}

ALWAYS_INLINE static VkOffset3D
radv_sdma_pixel_offset_to_blocks(const VkOffset3D offset, const unsigned blk_w, const unsigned blk_h)
{
   const VkOffset3D r = {
      .x = DIV_ROUND_UP(offset.x, blk_w),
      .y = DIV_ROUND_UP(offset.y, blk_h),
      .z = offset.z,
   };

   return r;
}

ALWAYS_INLINE static unsigned
radv_sdma_pixels_to_blocks(const unsigned linear_pitch, const unsigned blk_w)
{
   return DIV_ROUND_UP(linear_pitch, blk_w);
}

ALWAYS_INLINE static unsigned
radv_sdma_pixel_area_to_blocks(const unsigned linear_slice_pitch, const unsigned blk_w, const unsigned blk_h)
{
   return DIV_ROUND_UP(DIV_ROUND_UP(linear_slice_pitch, blk_w), blk_h);
}

static struct radv_sdma_chunked_copy_info
radv_sdma_get_chunked_copy_info(const struct radv_device *const device, const struct radv_image *const image,
                                const VkExtent3D extent)
{
   const struct radeon_surf *const surf = &image->planes[0].surface;

   const unsigned bpp = surf->bpe;
   const unsigned blk_w = surf->blk_w;
   const unsigned blk_h = surf->blk_h;
   const unsigned row_pitch_alignment = 4;
   const unsigned extent_horizontal_blocks = DIV_ROUND_UP(extent.width, blk_w);
   const unsigned extent_vertical_blocks = DIV_ROUND_UP(extent.height, blk_h);
   const unsigned aligned_row_pitch = ALIGN(extent_horizontal_blocks, row_pitch_alignment);
   const unsigned aligned_row_bytes = aligned_row_pitch * bpp;

   /* Assume that we can always copy at least one full row at a time. */
   const unsigned max_num_rows_per_copy = MIN2(RADV_SDMA_TRANSFER_TEMP_BYTES / aligned_row_bytes, extent.height);
   assert(max_num_rows_per_copy);

   /* Ensure that the number of rows copied at a time is a power of two. */
   const unsigned num_rows_per_copy = MAX2(1, util_next_power_of_two(max_num_rows_per_copy + 1) / 2);

   const struct radv_sdma_chunked_copy_info r = {
      .bpp = bpp,
      .blk_w = blk_w,
      .blk_h = blk_h,
      .row_pitch_alignment = row_pitch_alignment,
      .extent_horizontal_blocks = extent_horizontal_blocks,
      .extent_vertical_blocks = extent_vertical_blocks,
      .aligned_row_pitch = aligned_row_pitch,
      .num_rows_per_copy = num_rows_per_copy,
   };

   return r;
}

static struct radv_sdma_linear_info
radv_sdma_get_linear_buf_info(const struct radv_buffer *const buffer, const struct radv_image *const image,
                              const VkBufferImageCopy2 *const region)
{
   const unsigned pitch = (region->bufferRowLength ? region->bufferRowLength : region->imageExtent.width);
   const unsigned slice_pitch =
      (region->bufferImageHeight ? region->bufferImageHeight : region->imageExtent.height) * pitch;

   const struct radeon_surf *surf = &image->planes[0].surface;
   const struct radv_sdma_linear_info info = {
      .va = radv_buffer_get_va(buffer->bo) + buffer->offset + region->bufferOffset,
      .pitch = pitch,
      .slice_pitch = slice_pitch,
      .bpp = surf->bpe,
      .blk_w = surf->blk_w,
      .blk_h = surf->blk_h,
   };

   return info;
}

static struct radv_sdma_linear_info
radv_sdma_get_linear_img_info(const struct radv_image *const image, const VkImageSubresourceLayers subresource)
{
   const struct radeon_surf *surf = &image->planes[0].surface;

   if (!surf->is_linear) {
      const struct radv_sdma_linear_info empty_info = {0};
      return empty_info;
   }

   const struct radv_sdma_linear_info info = {
      .va = image->bindings[0].bo->va + image->bindings[0].offset + surf->u.gfx9.surf_offset +
            surf->u.gfx9.offset[subresource.mipLevel],
      .pitch = surf->u.gfx9.pitch[subresource.mipLevel],
      .slice_pitch = surf->blk_w * surf->blk_h * surf->u.gfx9.surf_slice_size / surf->bpe,
      .bpp = surf->bpe,
      .blk_w = surf->blk_w,
      .blk_h = surf->blk_h,
   };

   return info;
}

static uint32_t
radv_sdma_get_metadata_config(const struct radv_device *const device, const struct radv_image *const image,
                              const VkImageSubresourceLayers subresource)
{
   /* Only SDMA 5 supports metadata. */
   const bool is_v5 = device->physical_device->rad_info.gfx_level >= GFX10;

   if (!is_v5 || !(radv_dcc_enabled(image, subresource.mipLevel) || radv_image_has_htile(image))) {
      return 0;
   }

   const struct radeon_surf *const surf = &image->planes[0].surface;
   const VkFormat format = vk_format_get_aspect_format(image->vk.format, subresource.aspectMask);
   const struct util_format_description *desc = vk_format_description(format);

   const uint32_t data_format =
      ac_get_cb_format(device->physical_device->rad_info.gfx_level, vk_format_to_pipe_format(format));
   const uint32_t alpha_is_on_msb = vi_alpha_is_on_msb(device, format);
   const uint32_t number_type = radv_translate_buffer_numformat(desc, vk_format_get_first_non_void_channel(format));
   const uint32_t surface_type = radv_sdma_surface_type_from_aspect_mask(subresource.aspectMask);
   const uint32_t max_comp_block_size = surf->u.gfx9.color.dcc.max_compressed_block_size;
   const uint32_t max_uncomp_block_size = radv_get_dcc_max_uncompressed_block_size(device, image);
   const uint32_t pipe_aligned = surf->u.gfx9.color.dcc.pipe_aligned;

   return data_format | alpha_is_on_msb << 8 | number_type << 9 | surface_type << 12 | max_comp_block_size << 24 |
          max_uncomp_block_size << 26 | pipe_aligned << 31;
}

static uint32_t
radv_sdma_get_tiled_info_dword(const struct radv_device *const device, const struct radv_image *const image,
                               const VkImageSubresourceLayers subresource)
{
   const struct radeon_surf *const surf = &image->planes[0].surface;
   const uint32_t element_size = util_logbase2(surf->bpe);
   const uint32_t swizzle_mode = surf->has_stencil ? surf->u.gfx9.zs.stencil_swizzle_mode : surf->u.gfx9.swizzle_mode;
   const enum gfx9_resource_type dimension = radv_sdma_surface_resource_type(device, surf);
   const uint32_t info = element_size | swizzle_mode << 3 | dimension << 9;

   if (device->physical_device->rad_info.gfx_level >= GFX10) {
      const uint32_t mip_max = MAX2(image->vk.mip_levels, 1);
      const uint32_t mip_id = subresource.mipLevel;

      return info | (mip_max - 1) << 16 | mip_id << 20;
   } else if (device->physical_device->rad_info.gfx_level == GFX9) {
      return info | surf->u.gfx9.epitch << 16;
   } else {
      unreachable("unsupported gfx_level");
   }
}

static uint32_t
radv_sdma_get_tiled_header_dword(const struct radv_device *const device, const struct radv_image *const image,
                                 const VkImageSubresourceLayers subresource)
{
   const enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;

   if (gfx_level >= GFX10) {
      return 0;
   } else if (gfx_level == GFX9) {
      const uint32_t mip_max = MAX2(image->vk.mip_levels, 1);
      const uint32_t mip_id = subresource.mipLevel;
      return (mip_max - 1) << 20 | mip_id << 24;
   } else {
      unreachable("unsupported gfx_level");
   }
}

static struct radv_sdma_tiled_info
radv_sdma_get_tiled_img_info(const struct radv_device *const device, const struct radv_image *const image,
                             const VkImageSubresourceLayers subresource)
{
   const struct radeon_surf *const surf = &image->planes[0].surface;

   /* 1D resources should be linear. */
   assert(surf->u.gfx9.resource_type != RADEON_RESOURCE_1D);

   const uint32_t meta_config = radv_sdma_get_metadata_config(device, image, subresource);
   const uint64_t meta_va = image->bindings[0].bo->va + image->bindings[0].offset + surf->meta_offset;

   struct radv_sdma_tiled_info info = {
      .bpp = surf->bpe,
      .va = (image->bindings[0].bo->va + image->bindings[0].offset + surf->u.gfx9.surf_offset) | surf->tile_swizzle
                                                                                                    << 8,
      .meta_va = meta_config ? meta_va : 0,
      .meta_config = meta_config,
      .extent = radv_sdma_get_image_extent(image),
      .info_dword = radv_sdma_get_tiled_info_dword(device, image, subresource),
      .header_dword = radv_sdma_get_tiled_header_dword(device, image, subresource),
      .blk_w = surf->blk_w,
      .blk_h = surf->blk_h,
   };

   return info;
}

static void
radv_sdma_emit_nop(const struct radv_device *device, struct radeon_cmdbuf *cs)
{
   /* SDMA NOP acts as a fence command and causes the SDMA engine to wait for pending copy operations. */
   radeon_check_space(device->ws, cs, 1);
   radeon_emit(cs, CIK_SDMA_PACKET(CIK_SDMA_OPCODE_NOP, 0, 0));
}

void
radv_sdma_copy_buffer(const struct radv_device *device, struct radeon_cmdbuf *cs, uint64_t src_va, uint64_t dst_va,
                      uint64_t size)
{
   if (size == 0)
      return;

   enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;
   unsigned max_size_per_packet = gfx_level >= GFX10_3 ? GFX103_SDMA_COPY_MAX_SIZE : CIK_SDMA_COPY_MAX_SIZE;
   unsigned align = ~0u;
   unsigned ncopy = DIV_ROUND_UP(size, max_size_per_packet);

   assert(gfx_level >= GFX7);

   /* SDMA FW automatically enables a faster dword copy mode when
    * source, destination and size are all dword-aligned.
    *
    * When source and destination are dword-aligned, round down the size to
    * take advantage of faster dword copy, and copy the remaining few bytes
    * with the last copy packet.
    */
   if ((src_va & 0x3) == 0 && (dst_va & 0x3) == 0 && size > 4 && (size & 0x3) != 0) {
      align = ~0x3u;
      ncopy++;
   }

   radeon_check_space(device->ws, cs, ncopy * 7);

   for (unsigned i = 0; i < ncopy; i++) {
      unsigned csize = size >= 4 ? MIN2(size & align, max_size_per_packet) : size;
      radeon_emit(cs, CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY, CIK_SDMA_COPY_SUB_OPCODE_LINEAR, 0));
      radeon_emit(cs, gfx_level >= GFX9 ? csize - 1 : csize);
      radeon_emit(cs, 0); /* src/dst endian swap */
      radeon_emit(cs, src_va);
      radeon_emit(cs, src_va >> 32);
      radeon_emit(cs, dst_va);
      radeon_emit(cs, dst_va >> 32);
      dst_va += csize;
      src_va += csize;
      size -= csize;
   }
}

static void
radv_sdma_emit_copy_linear_sub_window(const struct radv_device *device, struct radeon_cmdbuf *cs,
                                      const struct radv_sdma_linear_info *const src,
                                      const struct radv_sdma_linear_info *const dst, const VkOffset3D src_pix_offset,
                                      const VkOffset3D dst_pix_offset, const VkExtent3D pix_extent)
{
   /* This packet is the same since SDMA v2.4, haven't bothered to check older versions.
    * The main difference is the bitfield sizes:
    *
    * v2.4 - src/dst_pitch: 14 bits, rect_z: 11 bits
    * v4.0 - src/dst_pitch: 19 bits, rect_z: 11 bits
    * v5.0 - src/dst_pitch: 19 bits, rect_z: 13 bits
    *
    * We currently use the smallest limits (from SDMA v2.4).
    */

   const VkOffset3D src_off = radv_sdma_pixel_offset_to_blocks(src_pix_offset, src->blk_w, src->blk_h);
   const VkOffset3D dst_off = radv_sdma_pixel_offset_to_blocks(dst_pix_offset, dst->blk_w, dst->blk_h);
   const VkExtent3D ext = radv_sdma_pixel_extent_to_blocks(pix_extent, src->blk_w, src->blk_h);
   const unsigned src_pitch = radv_sdma_pixels_to_blocks(src->pitch, src->blk_w);
   const unsigned dst_pitch = radv_sdma_pixels_to_blocks(dst->pitch, dst->blk_w);
   const unsigned src_slice_pitch = radv_sdma_pixel_area_to_blocks(src->slice_pitch, src->blk_w, src->blk_h);
   const unsigned dst_slice_pitch = radv_sdma_pixel_area_to_blocks(dst->slice_pitch, dst->blk_w, dst->blk_h);

   assert(src->bpp == dst->bpp);
   assert(util_is_power_of_two_nonzero(src->bpp));
   radv_sdma_check_pitches(src->pitch, src->slice_pitch, src->bpp, false);
   radv_sdma_check_pitches(dst->pitch, dst->slice_pitch, dst->bpp, false);

   ASSERTED unsigned cdw_end = radeon_check_space(device->ws, cs, 13);

   radeon_emit(cs, CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY, CIK_SDMA_COPY_SUB_OPCODE_LINEAR_SUB_WINDOW, 0) |
                      util_logbase2(src->bpp) << 29);
   radeon_emit(cs, src->va);
   radeon_emit(cs, src->va >> 32);
   radeon_emit(cs, src_off.x | src_off.y << 16);
   radeon_emit(cs, src_off.z | (src_pitch - 1) << 13);
   radeon_emit(cs, src_slice_pitch - 1);
   radeon_emit(cs, dst->va);
   radeon_emit(cs, dst->va >> 32);
   radeon_emit(cs, dst_off.x | dst_off.y << 16);
   radeon_emit(cs, dst_off.z | (dst_pitch - 1) << 13);
   radeon_emit(cs, dst_slice_pitch - 1);
   radeon_emit(cs, (ext.width - 1) | (ext.height - 1) << 16);
   radeon_emit(cs, (ext.depth - 1));

   assert(cs->cdw == cdw_end);
}

static void
radv_sdma_emit_copy_tiled_sub_window(const struct radv_device *device, struct radeon_cmdbuf *cs,
                                     const struct radv_sdma_tiled_info *const tiled,
                                     const struct radv_sdma_linear_info *const linear,
                                     const VkOffset3D tiled_pix_offset, const VkOffset3D linear_pix_offset,
                                     const VkExtent3D pix_extent, const bool detile)
{
   if (device->physical_device->rad_info.gfx_level == GFX9) {
      /* SDMA v4 doesn't support any image metadata. */
      assert(!tiled->meta_va);
   }

   const VkOffset3D linear_off = radv_sdma_pixel_offset_to_blocks(linear_pix_offset, linear->blk_w, linear->blk_h);
   const VkOffset3D tiled_off = radv_sdma_pixel_offset_to_blocks(tiled_pix_offset, tiled->blk_w, tiled->blk_h);
   const VkExtent3D tiled_ext = radv_sdma_pixel_extent_to_blocks(tiled->extent, tiled->blk_w, tiled->blk_h);
   const VkExtent3D ext = radv_sdma_pixel_extent_to_blocks(pix_extent, tiled->blk_w, tiled->blk_h);
   const unsigned linear_pitch = radv_sdma_pixels_to_blocks(linear->pitch, tiled->blk_w);
   const unsigned linear_slice_pitch = radv_sdma_pixel_area_to_blocks(linear->slice_pitch, tiled->blk_w, tiled->blk_h);
   const bool dcc = !!tiled->meta_va;
   const bool uses_depth = linear_off.z != 0 || tiled_off.z != 0 || ext.depth != 1;

   assert(util_is_power_of_two_nonzero(tiled->bpp));
   radv_sdma_check_pitches(linear_pitch, linear_slice_pitch, tiled->bpp, uses_depth);

   ASSERTED unsigned cdw_end = radeon_check_space(device->ws, cs, 14 + (dcc ? 3 : 0));

   radeon_emit(cs, CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY, CIK_SDMA_COPY_SUB_OPCODE_TILED_SUB_WINDOW, 0) | dcc << 19 |
                      detile << 31 | tiled->header_dword);
   radeon_emit(cs, tiled->va);
   radeon_emit(cs, tiled->va >> 32);
   radeon_emit(cs, tiled_off.x | tiled_off.y << 16);
   radeon_emit(cs, tiled_off.z | (tiled_ext.width - 1) << 16);
   radeon_emit(cs, (tiled_ext.height - 1) | (tiled_ext.depth - 1) << 16);
   radeon_emit(cs, tiled->info_dword);
   radeon_emit(cs, linear->va);
   radeon_emit(cs, linear->va >> 32);
   radeon_emit(cs, linear_off.x | linear_off.y << 16);
   radeon_emit(cs, linear_off.z | (linear_pitch - 1) << 16);
   radeon_emit(cs, linear_slice_pitch - 1);
   radeon_emit(cs, (ext.width - 1) | (ext.height - 1) << 16);
   radeon_emit(cs, (ext.depth - 1));

   if (tiled->meta_va) {
      const unsigned write_compress_enable = !detile;
      radeon_emit(cs, tiled->meta_va);
      radeon_emit(cs, tiled->meta_va >> 32);
      radeon_emit(cs, tiled->meta_config | write_compress_enable << 28);
   }

   assert(cs->cdw == cdw_end);
}

void
radv_sdma_copy_buffer_image(const struct radv_device *device, struct radeon_cmdbuf *cs, struct radv_image *image,
                            struct radv_buffer *buffer, const VkBufferImageCopy2 *region, bool to_image)
{
   const struct radv_sdma_linear_info buf_info = radv_sdma_get_linear_buf_info(buffer, image, region);
   const VkExtent3D extent = radv_sdma_get_copy_extent(image, region->imageSubresource, region->imageExtent);
   const VkOffset3D img_offset = radv_sdma_get_img_offset(image, region->imageSubresource, region->imageOffset);
   const VkOffset3D zero_offset = {0};

   if (image->planes[0].surface.is_linear) {
      const struct radv_sdma_linear_info linear = radv_sdma_get_linear_img_info(image, region->imageSubresource);

      if (to_image)
         radv_sdma_emit_copy_linear_sub_window(device, cs, &buf_info, &linear, zero_offset, img_offset, extent);
      else
         radv_sdma_emit_copy_linear_sub_window(device, cs, &linear, &buf_info, img_offset, zero_offset, extent);
   } else {
      const struct radv_sdma_tiled_info tiled = radv_sdma_get_tiled_img_info(device, image, region->imageSubresource);
      radv_sdma_emit_copy_tiled_sub_window(device, cs, &tiled, &buf_info, img_offset, zero_offset, extent, !to_image);
   }
}

bool
radv_sdma_use_unaligned_buffer_image_copy(const struct radv_device *device, const struct radv_image *image,
                                          const struct radv_buffer *buffer, const VkBufferImageCopy2 *region)
{
   const struct radeon_surf *const surf = &image->planes[0].surface;
   const enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;
   const unsigned pitch_alignment = gfx_level >= GFX10 ? MAX2(1, 4 / surf->bpe) : 4;
   const unsigned pitch = (region->bufferRowLength ? region->bufferRowLength : region->imageExtent.width);
   const unsigned pitch_blocks = radv_sdma_pixels_to_blocks(pitch, surf->blk_w);

   if (!radv_is_aligned(pitch_blocks, pitch_alignment))
      return true;

   const VkOffset3D off = radv_sdma_get_img_offset(image, region->imageSubresource, region->imageOffset);
   const VkExtent3D ext = radv_sdma_get_copy_extent(image, region->imageSubresource, region->imageExtent);
   const bool uses_depth = off.z != 0 || ext.depth != 1;
   if (!surf->is_linear && uses_depth) {
      const unsigned slice_pitch =
         (region->bufferImageHeight ? region->bufferImageHeight : region->imageExtent.height) * pitch;
      const unsigned slice_pitch_blocks = radv_sdma_pixel_area_to_blocks(slice_pitch, surf->blk_w, surf->blk_h);

      if (!radv_is_aligned(slice_pitch_blocks, 4))
         return true;
   }

   return false;
}

void
radv_sdma_copy_buffer_image_unaligned(const struct radv_device *device, struct radeon_cmdbuf *cs,
                                      struct radv_image *image, struct radv_buffer *buffer,
                                      const VkBufferImageCopy2 *region, struct radeon_winsys_bo *temp_bo, bool to_image)
{
   const bool is_linear = image->planes[0].surface.is_linear;
   const VkOffset3D base_offset = radv_sdma_get_img_offset(image, region->imageSubresource, region->imageOffset);
   const VkExtent3D base_extent = radv_sdma_get_copy_extent(image, region->imageSubresource, region->imageExtent);
   const struct radv_sdma_chunked_copy_info info = radv_sdma_get_chunked_copy_info(device, image, base_extent);
   const struct radv_sdma_linear_info buf = radv_sdma_get_linear_buf_info(buffer, image, region);
   const struct radv_sdma_linear_info linear = radv_sdma_get_linear_img_info(image, region->imageSubresource);
   const struct radv_sdma_tiled_info tiled = radv_sdma_get_tiled_img_info(device, image, region->imageSubresource);

   struct radv_sdma_linear_info tmp = {
      .va = temp_bo->va,
      .bpp = info.bpp,
      .blk_w = info.blk_w,
      .blk_h = info.blk_h,
      .pitch = info.aligned_row_pitch * info.blk_w,
      .slice_pitch = info.aligned_row_pitch * info.blk_w * info.extent_vertical_blocks * info.blk_h,
   };

   const VkOffset3D zero_offset = {0};
   VkExtent3D extent = base_extent;
   VkOffset3D offset = base_offset;
   const unsigned buf_pitch_blocks = DIV_ROUND_UP(buf.pitch, info.blk_w);
   const unsigned buf_slice_pitch_blocks = DIV_ROUND_UP(DIV_ROUND_UP(buf.slice_pitch, info.blk_w), info.blk_h);
   assert(buf_pitch_blocks);
   assert(buf_slice_pitch_blocks);
   extent.depth = 1;

   for (unsigned slice = 0; slice < base_extent.depth; ++slice) {
      for (unsigned row = 0; row < info.extent_vertical_blocks; row += info.num_rows_per_copy) {
         const unsigned rows = MIN2(info.extent_vertical_blocks - row, info.num_rows_per_copy);

         offset.y = base_offset.y + row * info.blk_h;
         offset.z = base_offset.z + slice;
         extent.height = rows * info.blk_h;
         tmp.slice_pitch = tmp.pitch * rows * info.blk_h;

         if (!to_image) {
            /* Copy the rows from the source image to the temporary buffer. */
            if (is_linear)
               radv_sdma_emit_copy_linear_sub_window(device, cs, &linear, &tmp, offset, zero_offset, extent);
            else
               radv_sdma_emit_copy_tiled_sub_window(device, cs, &tiled, &tmp, offset, zero_offset, extent, true);

            /* Wait for the copy to finish. */
            radv_sdma_emit_nop(device, cs);
         }

         /* buffer to image: copy each row from source buffer to temporary buffer.
          * image to buffer: copy each row from temporary buffer to destination buffer.
          */
         for (unsigned r = 0; r < rows; ++r) {
            const uint64_t buf_va =
               buf.va + slice * buf_slice_pitch_blocks * info.bpp + (row + r) * buf_pitch_blocks * info.bpp;
            const uint64_t tmp_va = tmp.va + r * info.aligned_row_pitch * info.bpp;
            radv_sdma_copy_buffer(device, cs, to_image ? buf_va : tmp_va, to_image ? tmp_va : buf_va,
                                  info.extent_horizontal_blocks * info.bpp);
         }

         /* Wait for the copy to finish. */
         radv_sdma_emit_nop(device, cs);

         if (to_image) {
            /* Copy the rows from the temporary buffer to the destination image. */
            if (is_linear)
               radv_sdma_emit_copy_linear_sub_window(device, cs, &tmp, &linear, zero_offset, offset, extent);
            else
               radv_sdma_emit_copy_tiled_sub_window(device, cs, &tiled, &tmp, offset, zero_offset, extent, false);

            /* Wait for the copy to finish. */
            radv_sdma_emit_nop(device, cs);
         }
      }
   }
}
