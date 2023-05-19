/**************************************************************************
 *
 * Copyright 2023 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "util/u_handle_table.h"
#include "util/u_video.h"
#include "va_private.h"
#include "util/vl_vlc.h"

#if VA_CHECK_VERSION(1, 16, 0)

#define AV1_SELECT_SCREEN_CONTENT_TOOLS (2)
#define AV1_SELECT_INTEGER_MV           (2)
#define AV1_PRIMARY_REF_NON             (7)
#define AV1_MAXNUM_OPERATING_POINT      (32)
#define AV1_SUPERRES_DENOM_BITS  (8)
#define AV1_MAXNUM_REF_FRAMES    (8)
#define AV1_REFS_PER_FRAME       (7)
#define FRAME_TYPE_KEY_FRAME     (0)
#define FRAME_TYPE_INTER_FRAME   (1)
#define FRAME_TYPE_INTRA_ONLY    (2)
#define FRAME_TYPE_SWITCH        (3)
#define OBU_TYPE_SEQUENCE_HEADER (1)
#define OBU_TYPE_FRAME_HEADER    (3)

static unsigned av1_f(struct vl_vlc *vlc, unsigned n)
{
   unsigned valid = vl_vlc_valid_bits(vlc);

   if (n == 0)
      return 0;

   if (valid < 32)
      vl_vlc_fillbits(vlc);

   return vl_vlc_get_uimsbf(vlc, n);
}

static unsigned av1_uvlc(struct vl_vlc *vlc)
{
   unsigned value;
   unsigned leadingZeros = 0;

   while (1) {
      bool done = av1_f(vlc, 1);
      if (done)
         break;
      leadingZeros++;
   }

   if (leadingZeros >= 32)
      return 0xffffffff;

   value = av1_f(vlc, leadingZeros);

   return value + (1 << leadingZeros) - 1;
}

static unsigned av1_uleb128(struct vl_vlc *vlc)
{
   unsigned value = 0;
   unsigned leb128Bytes = 0;
   unsigned i;

   for (i = 0; i < 8; ++i) {
      leb128Bytes = av1_f(vlc, 8);
      value |= ((leb128Bytes & 0x7f) << (i * 7));
      if (!(leb128Bytes & 0x80))
         break;
   }

   return value;
}

VAStatus vlVaHandleVAEncSequenceParameterBufferTypeAV1(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncSequenceParameterBufferAV1 *av1 = buf->data;

   if (!context->decoder) {
      context->templat.level = av1->seq_level_idx;
      context->decoder = drv->pipe->create_video_codec(drv->pipe, &context->templat);

      if (!context->decoder)
         return VA_STATUS_ERROR_ALLOCATION_FAILED;

      getEncParamPresetAV1(context);
   }

   context->desc.av1enc.seq.tier = av1->seq_tier;
   context->desc.av1enc.seq.level = av1->seq_level_idx;
   context->desc.av1enc.seq.intra_period = av1->intra_period;
   context->desc.av1enc.seq.bit_depth_minus8 = av1->seq_fields.bits.bit_depth_minus8;
   context->desc.av1enc.seq.seq_bits.enable_cdef = av1->seq_fields.bits.enable_cdef;
   context->desc.av1enc.seq.seq_bits.enable_order_hint = av1->seq_fields.bits.enable_order_hint;

   for (int i = 0; i < ARRAY_SIZE(context->desc.av1enc.rc); i++)
      context->desc.av1enc.rc[i].peak_bitrate = av1->bits_per_second;

   return VA_STATUS_SUCCESS;
}
VAStatus vlVaHandleVAEncPictureParameterBufferTypeAV1(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncPictureParameterBufferAV1 *av1 = buf->data;
   vlVaBuffer *coded_buf;
   int i;

   context->desc.av1enc.disable_frame_end_update_cdf = av1->picture_flags.bits.disable_frame_end_update_cdf;
   context->desc.av1enc.error_resilient_mode = av1->picture_flags.bits.error_resilient_mode;
   context->desc.av1enc.disable_cdf_update = av1->picture_flags.bits.disable_cdf_update;
   context->desc.av1enc.enable_frame_obu = av1->picture_flags.bits.enable_frame_obu;
   context->desc.av1enc.allow_high_precision_mv = av1->picture_flags.bits.allow_high_precision_mv;
   context->desc.av1enc.palette_mode_enable = av1->picture_flags.bits.palette_mode_enable;
   context->desc.av1enc.num_tiles_in_pic = av1->tile_cols * av1->tile_rows;

   coded_buf = handle_table_get(drv->htab, av1->coded_buf);
   if (!coded_buf)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   if (!coded_buf->derived_surface.resource)
      coded_buf->derived_surface.resource = pipe_buffer_create(drv->pipe->screen, PIPE_BIND_VERTEX_BUFFER,
                                            PIPE_USAGE_STAGING, coded_buf->size);
   context->coded_buf = coded_buf;

   for (i = 0; i < ARRAY_SIZE(context->desc.av1enc.rc); i++) {
      context->desc.av1enc.rc[i].qp = av1->base_qindex ? av1->base_qindex : 60;
      context->desc.av1enc.rc[i].min_qp = av1->min_base_qindex ? av1->min_base_qindex : 1;
      context->desc.av1enc.rc[i].max_qp = av1->max_base_qindex ? av1->max_base_qindex : 255;
   }

   /* these frame types will need to be seen as force type */
   switch(av1->picture_flags.bits.frame_type)
   {
      case 0:
         context->desc.av1enc.frame_type = PIPE_AV1_ENC_FRAME_TYPE_KEY;
         break;
      case 1:
         context->desc.av1enc.frame_type = PIPE_AV1_ENC_FRAME_TYPE_INTER;
         break;
      case 2:
         context->desc.av1enc.frame_type = PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY;
         break;
      case 3:
         context->desc.av1enc.frame_type = PIPE_AV1_ENC_FRAME_TYPE_SWITCH;
         break;
   };

   return VA_STATUS_SUCCESS;
}

VAStatus vlVaHandleVAEncMiscParameterTypeRateControlAV1(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterRateControl *rc = (VAEncMiscParameterRateControl *)misc->data;
   struct pipe_av1_enc_rate_control *pipe_rc = NULL;

   for (int i = 1; i < ARRAY_SIZE(context->desc.av1enc.rc); i++) {
      pipe_rc = &context->desc.av1enc.rc[i];
      pipe_rc->rate_ctrl_method = context->desc.av1enc.rc[0].rate_ctrl_method;
   }

   for (int i = 0; i < ARRAY_SIZE(context->desc.av1enc.rc); i++)
   {
      pipe_rc = &context->desc.av1enc.rc[i];

      if (pipe_rc->rate_ctrl_method == PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT)
         pipe_rc->target_bitrate = pipe_rc->peak_bitrate;
      else
         pipe_rc->target_bitrate = pipe_rc->peak_bitrate * (rc->target_percentage / 100.0);

      if (pipe_rc->target_bitrate < 2000000)
         pipe_rc->vbv_buffer_size = MIN2((pipe_rc->target_bitrate * 2.75), 2000000);
      else
         pipe_rc->vbv_buffer_size = pipe_rc->target_bitrate;

      pipe_rc->fill_data_enable = !(rc->rc_flags.bits.disable_bit_stuffing);
      pipe_rc->skip_frame_enable = 0;/* !(rc->rc_flags.bits.disable_frame_skip); */
   }

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeQualityLevelAV1(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterBufferQualityLevel *ql = (VAEncMiscParameterBufferQualityLevel *)misc->data;
   vlVaHandleVAEncMiscParameterTypeQualityLevel(&context->desc.av1enc.quality_modes,
                               (vlVaQualityBits *)&ql->quality_level);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeMaxFrameSizeAV1(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterBufferMaxFrameSize *ms = (VAEncMiscParameterBufferMaxFrameSize *)misc->data;
   context->desc.av1enc.rc[0].max_au_size = ms->max_frame_size;
   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeHRDAV1(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterHRD *ms = (VAEncMiscParameterHRD *)misc->data;

   if (ms->buffer_size) {
      context->desc.av1enc.rc[0].vbv_buffer_size = ms->buffer_size;
      context->desc.av1enc.rc[0].vbv_buf_lv = (ms->initial_buffer_fullness << 6 ) / ms->buffer_size;
   }

   return VA_STATUS_SUCCESS;
}

static void av1_color_config(vlVaContext *context, struct vl_vlc *vlc)
{
   unsigned high_bitdepth;
   unsigned bit_depth = 8;
   unsigned mono_chrome;
   unsigned subsampling_x = 0, subsampling_y = 0;

   struct pipe_av1_enc_seq_param *seq = &context->desc.av1enc.seq;

   high_bitdepth = av1_f(vlc, 1);
   if (seq->profile == 2 && high_bitdepth) {
      unsigned twelve_bit = av1_f(vlc, 1);
      bit_depth = twelve_bit ? 12 : 10;
   } else if (seq->profile <= 2)
      bit_depth = high_bitdepth ? 10 : 8;

   context->desc.av1enc.seq.bit_depth_minus8 = bit_depth - 8;

   if (seq->profile == 1)
      mono_chrome = 0;
   else
      mono_chrome = av1_f(vlc, 1);

   seq->seq_bits.color_description_present_flag = av1_f(vlc, 1);
   if (seq->seq_bits.color_description_present_flag) {
      seq->color_config.color_primaries = av1_f(vlc, 8);
      seq->color_config.transfer_characteristics = av1_f(vlc, 8);
      seq->color_config.matrix_coefficients = av1_f(vlc, 8);
   } else {
      seq->color_config.color_primaries = 2;
      seq->color_config.transfer_characteristics = 2;
      seq->color_config.matrix_coefficients = 2;
   }

   if (mono_chrome) {
      seq->color_config.color_range = av1_f(vlc, 1);
      subsampling_x = subsampling_y = 1;
      seq->color_config.chroma_sample_position = 0;
      return;
   } else if (seq->color_config.color_primaries == 1 &&  /* CP_BT_709 */
              seq->color_config.transfer_characteristics == 13 && /* TC_SRGB */
              seq->color_config.matrix_coefficients == 0) { /* MC_IDENTITY */
      seq->color_config.color_range = 1;
      subsampling_x = subsampling_y = 0;
   } else {
      seq->color_config.color_range = av1_f(vlc, 1);
      if (seq->profile == 0)
         subsampling_x = subsampling_y = 1;
      else if (seq->profile == 1)
         subsampling_x = subsampling_y = 0;
      else {
         if (bit_depth == 12) {
            subsampling_x = av1_f(vlc, 1);
            if (subsampling_x)
               subsampling_y = av1_f(vlc, 1);
            else
               subsampling_y = 0;
         }
      }
      if (subsampling_x && subsampling_y)
         seq->color_config.chroma_sample_position = av1_f(vlc, 2);
   }

   av1_f(vlc, 1);
}

static void av1_sequence_header(vlVaContext *context, struct vl_vlc *vlc)
{
   unsigned initial_display_delay_present_flag = 0;
   unsigned layer_minus1 = 0, value = 0;
   unsigned buffer_delay_length_minus1 = 0;
   unsigned still_pic = 0;
   struct pipe_av1_enc_seq_param *seq = &context->desc.av1enc.seq;

   seq->profile = av1_f(vlc, 3);
   still_pic = av1_f(vlc, 1);
   av1_f(vlc, 1);
   assert(!still_pic);

   seq->seq_bits.timing_info_present_flag = av1_f(vlc, 1);
   if (seq->seq_bits.timing_info_present_flag) {
      seq->num_units_in_display_tick = av1_f(vlc, 32);
      seq->time_scale = av1_f(vlc, 32);
      seq->seq_bits.equal_picture_interval = av1_f(vlc, 1);
      if (seq->seq_bits.equal_picture_interval)
         seq->num_tick_per_picture_minus1 = av1_uvlc(vlc);
      seq->seq_bits.decoder_model_info_present_flag = av1_f(vlc, 1);
      if (seq->seq_bits.decoder_model_info_present_flag) {
         struct pipe_av1_enc_decoder_model_info *info = &seq->decoder_model_info;
         info->buffer_delay_length_minus1 = av1_f(vlc, 5);
         info->num_units_in_decoding_tick = av1_f(vlc, 32);
         info->buffer_removal_time_length_minus1 = av1_f(vlc, 5);
         info->frame_presentation_time_length_minus1 = av1_f(vlc, 5);
      }
   }
   initial_display_delay_present_flag = av1_f(vlc, 1);
   layer_minus1 = av1_f(vlc, 5);
   seq->num_temporal_layers = layer_minus1 + 1;
   for (unsigned i = 0; i <= layer_minus1; i++) {
      seq->operating_point_idc[i] = av1_f(vlc, 12);
      value = av1_f(vlc, 5);
      if (value > 7)
         av1_f(vlc, 1);
      if (seq->seq_bits.decoder_model_info_present_flag) {
         seq->decoder_model_present_for_this_op[i] = av1_f(vlc, 1);
         if (seq->decoder_model_present_for_this_op[i]) {
            av1_f(vlc, buffer_delay_length_minus1 + 1);
            av1_f(vlc, buffer_delay_length_minus1 + 1);
            av1_f(vlc, 1);
         } else
            seq->decoder_model_present_for_this_op[i] = 0;
      }
      if (initial_display_delay_present_flag) {
         value = av1_f(vlc, 1);
         if (value)
            av1_f(vlc, 4);
      }
   }
   seq->frame_width_bits_minus1 = av1_f(vlc, 4);
   seq->frame_height_bits_minus1 = av1_f(vlc, 4);
   seq->pic_width_in_luma_samples = av1_f(vlc, seq->frame_width_bits_minus1 + 1) + 1;
   seq->pic_height_in_luma_samples = av1_f(vlc, seq->frame_height_bits_minus1 + 1) + 1;
   seq->seq_bits.frame_id_number_present_flag = av1_f(vlc, 1);
   if (seq->seq_bits.frame_id_number_present_flag) {
      seq->delta_frame_id_length = av1_f(vlc, 4) + 2;
      seq->additional_frame_id_length = av1_f(vlc, 3) + 1;
   }
   av1_f(vlc, 1);
   av1_f(vlc, 1);
   av1_f(vlc, 1);
   /* reduced_still_pictuer_header should be zero */
   av1_f(vlc, 1);
   av1_f(vlc, 1);
   av1_f(vlc, 1);
   av1_f(vlc, 1);
   seq->seq_bits.enable_order_hint = av1_f(vlc, 1);
   if (seq->seq_bits.enable_order_hint) {
      av1_f(vlc, 1);
      seq->seq_bits.enable_ref_frame_mvs = av1_f(vlc, 1);
   } else
      seq->seq_bits.enable_ref_frame_mvs = 0;

   seq->seq_bits.disable_screen_content_tools = av1_f(vlc, 1);
   if (seq->seq_bits.disable_screen_content_tools)
      seq->seq_bits.force_screen_content_tools = AV1_SELECT_SCREEN_CONTENT_TOOLS;
   else
      seq->seq_bits.force_screen_content_tools = av1_f(vlc, 1);

   seq->seq_bits.force_integer_mv = AV1_SELECT_INTEGER_MV;
   if (seq->seq_bits.force_screen_content_tools) {
      value = av1_f(vlc, 1);
      if (!value)
         seq->seq_bits.force_integer_mv = av1_f(vlc, 1);
   }
   if (seq->seq_bits.enable_order_hint)
      seq->order_hint_bits = av1_f(vlc, 3) + 1;
   else
      seq->order_hint_bits = 0;

   seq->seq_bits.enable_superres = av1_f(vlc, 1);
   seq->seq_bits.enable_cdef = av1_f(vlc, 1);
   av1_f(vlc, 1);
   av1_color_config(context, vlc);
}

static void av1_superres_params(vlVaContext *context, struct vl_vlc *vlc)
{
   struct pipe_av1_enc_picture_desc *av1 = &context->desc.av1enc;
   uint8_t use_superres;

   if (av1->seq.seq_bits.enable_superres)
      use_superres = av1_f(vlc, 1);
   else
      use_superres = 0;

  if (use_superres)
     av1_f(vlc, AV1_SUPERRES_DENOM_BITS);

  av1->upscaled_width = av1->frame_width;
}

static void av1_frame_size(vlVaContext *context, struct vl_vlc *vlc)
{
   struct pipe_av1_enc_picture_desc *av1 = &context->desc.av1enc;

   if (av1->frame_size_override_flag) {
      av1->frame_width = av1_f(vlc, av1->seq.frame_width_bits_minus1 + 1) + 1;
      av1_f(vlc, av1->seq.frame_height_bits_minus1 + 1);
   } else
      av1->frame_width = av1->seq.pic_width_in_luma_samples;

   av1_superres_params(context, vlc);
}

static void av1_render_size(vlVaContext *context, struct vl_vlc *vlc)
{
   struct pipe_av1_enc_picture_desc *av1 = &context->desc.av1enc;

   av1->enable_render_size = av1_f(vlc, 1);
   if (av1->enable_render_size) {
      av1->render_width = av1_f(vlc, 16);
      av1->render_height = av1_f(vlc, 16);
   }
}

static void av1_frame_size_with_refs(vlVaContext *context, struct vl_vlc *vlc)
{
   uint8_t found_ref = 0;

   for (int i = 0; i < AV1_REFS_PER_FRAME; i++) {
      found_ref = av1_f(vlc, 1);
      if (found_ref)
         break;
   }

   if (found_ref == 0) {
      av1_frame_size(context, vlc);
      av1_render_size(context, vlc);
   } else
      av1_superres_params(context, vlc);
}

static void av1_read_interpolation_filter(vlVaContext *context, struct vl_vlc *vlc)
{
   uint8_t is_filter_switchable = av1_f(vlc, 1);

   if (!is_filter_switchable)
      av1_f(vlc, 2);
}

static void av1_frame_header(vlVaContext *context, struct vl_vlc *vlc)
{
   struct pipe_av1_enc_picture_desc *av1 = &context->desc.av1enc;
   uint32_t frame_type;
   uint32_t id_len, all_frames, show_frame;
   uint32_t refresh_frame_flags = 0;

   bool frame_is_intra = false;

   if (av1->seq.seq_bits.frame_id_number_present_flag)
      id_len = av1->seq.delta_frame_id_length + av1->seq.additional_frame_id_length;

   all_frames = 255;
   av1->show_existing_frame = av1_f(vlc, 1);
   /* use the last reference frame to show */
   if (av1->show_existing_frame)
      return;

   frame_type = av1_f(vlc, 2);
   frame_is_intra = (frame_type == FRAME_TYPE_KEY_FRAME ||
                     frame_type == FRAME_TYPE_INTRA_ONLY);
   show_frame = av1_f(vlc, 1);
   if (show_frame && av1->seq.seq_bits.decoder_model_info_present_flag
                  && !(av1->seq.seq_bits.equal_picture_interval)) {
      struct pipe_av1_enc_decoder_model_info *info = &av1->seq.decoder_model_info;
      av1_f(vlc, info->frame_presentation_time_length_minus1 + 1);
   }

   if (!show_frame)
      av1_f(vlc, 1); /* showable_frame */

   if (frame_type == FRAME_TYPE_SWITCH ||
         (frame_type == FRAME_TYPE_KEY_FRAME && show_frame))
      av1->error_resilient_mode = 1;
   else
      av1->error_resilient_mode = av1_f(vlc, 1);

   av1->disable_cdf_update = av1_f(vlc, 1);
   if (av1->seq.seq_bits.force_screen_content_tools == AV1_SELECT_SCREEN_CONTENT_TOOLS)
      av1->allow_screen_content_tools = av1_f(vlc, 1);
   else
      av1->allow_screen_content_tools = !!(av1->seq.seq_bits.force_screen_content_tools);

   av1->force_integer_mv = 0;
   if (av1->allow_screen_content_tools) {
      if (av1->seq.seq_bits.force_integer_mv == AV1_SELECT_INTEGER_MV)
         av1->force_integer_mv = av1_f(vlc, 1);
      else
         av1->force_integer_mv = !!(av1->seq.seq_bits.force_integer_mv);
   }

   if (frame_is_intra)
      av1->force_integer_mv = 1;

   if (av1->seq.seq_bits.frame_id_number_present_flag)
      av1_f(vlc, id_len);

   if (frame_type == FRAME_TYPE_SWITCH)
      av1->frame_size_override_flag = 1;
   else
      av1->frame_size_override_flag = av1_f(vlc, 1);

   if (av1->seq.seq_bits.enable_order_hint)
      av1_f(vlc, av1->seq.order_hint_bits);

   if (!(frame_is_intra || av1->error_resilient_mode))
      av1_f(vlc, 3);

   if (av1->seq.seq_bits.decoder_model_info_present_flag) {
      unsigned buffer_removal_time_present_flag = av1_f(vlc, 1);
      if (buffer_removal_time_present_flag) {
         for (int opNum = 0; opNum <= av1->seq.num_temporal_layers - 1; opNum++) {
            if (av1->seq.decoder_model_present_for_this_op[opNum]) {
               uint16_t op_pt_idc = av1->seq.operating_point_idc[opNum];
               uint16_t temporal_layer = (op_pt_idc >> av1->temporal_id) & 1;
               uint16_t spatial_layer = (op_pt_idc >> (av1->spatial_id + 8)) & 1;
               if (op_pt_idc == 0 || (temporal_layer && spatial_layer))
                  av1_f(vlc, av1->seq.decoder_model_info.buffer_removal_time_length_minus1 + 1);
            }
         }
      }
   }

   if (frame_type == FRAME_TYPE_SWITCH ||
       (frame_type == FRAME_TYPE_KEY_FRAME && show_frame))
       refresh_frame_flags = all_frames;
   else
      refresh_frame_flags = av1_f(vlc, 8);

   if ( !frame_is_intra || refresh_frame_flags != all_frames) {
      if (av1->error_resilient_mode && av1->seq.seq_bits.enable_order_hint)
         for (int i = 0; i < AV1_MAXNUM_REF_FRAMES; i++)
            av1_f(vlc, av1->seq.order_hint_bits);
   }

   if ( frame_is_intra) {
      av1_frame_size(context, vlc);
      av1_render_size(context, vlc);
      if (av1->allow_screen_content_tools && av1->upscaled_width == av1->frame_width)
         av1_f(vlc, 1);
   } else {
      unsigned frame_refs_short_signaling = 0;
      if (av1->seq.seq_bits.enable_order_hint) {
         frame_refs_short_signaling = av1_f(vlc, 1);
         if (frame_refs_short_signaling) {
            av1_f(vlc, 3);
            av1_f(vlc, 3);
         }
      }

      for (int i = 0; i < AV1_REFS_PER_FRAME; i++) {
         if (!frame_refs_short_signaling)
            av1_f(vlc, 3);
         if (av1->seq.seq_bits.frame_id_number_present_flag)
            av1_f(vlc, av1->seq.delta_frame_id_length);
      }

      if (av1->frame_size_override_flag && av1->error_resilient_mode)
         av1_frame_size_with_refs(context, vlc);
      else {
         av1_frame_size(context, vlc);
         av1_render_size(context, vlc);
      }

      if (av1->force_integer_mv)
         av1->allow_high_precision_mv = 0;
      else
         av1->allow_high_precision_mv = av1_f(vlc, 1);

      av1_read_interpolation_filter(context, vlc);
      av1_f(vlc, 1);
      if (av1->error_resilient_mode || !av1->seq.seq_bits.enable_ref_frame_mvs)
         av1->use_ref_frame_mvs = 0;
      else
         av1->use_ref_frame_mvs = av1_f(vlc, 1);

      if (av1->disable_cdf_update)
         av1->disable_frame_end_update_cdf = 1;
      else
         av1->disable_frame_end_update_cdf = av1_f(vlc, 1);
   }
}

VAStatus
vlVaHandleVAEncPackedHeaderDataBufferTypeAV1(vlVaContext *context, vlVaBuffer *buf)
{
   struct vl_vlc vlc = {0};
   vl_vlc_init(&vlc, 1, (const void * const*)&buf->data, &buf->size);

   while (vl_vlc_bits_left(&vlc) > 0) {
      unsigned obu_type = 0;
      /* search sequece header in the first 8 bytes */
      for (int i = 0; i < 8 && vl_vlc_bits_left(&vlc) >= 8; ++i) {
         /* then start decoding , first 5 bits has to be 0000 1xxx for sequence header */
         obu_type = vl_vlc_peekbits(&vlc, 5);
         if (obu_type == OBU_TYPE_SEQUENCE_HEADER || obu_type == OBU_TYPE_FRAME_HEADER)
            break;
         vl_vlc_eatbits(&vlc, 8);
         vl_vlc_fillbits(&vlc);
      }

      av1_f(&vlc, 5); /* eat known bits */
      uint32_t extension_flag = av1_f(&vlc, 1);
      uint32_t has_size = av1_f(&vlc, 1);
      av1_f(&vlc, 1);
      if (extension_flag) {
         context->desc.av1enc.temporal_id = av1_f(&vlc, 3);
         context->desc.av1enc.spatial_id = av1_f(&vlc, 2);
         av1_f(&vlc, 3);
      }

      if (has_size)
          av1_uleb128(&vlc);

      if (obu_type == OBU_TYPE_SEQUENCE_HEADER)
         av1_sequence_header(context, &vlc);
      else if (obu_type == OBU_TYPE_FRAME_HEADER)
         av1_frame_header(context, &vlc);
      else
         assert(0);

      break;
   }

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeFrameRateAV1(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterFrameRate *fr = (VAEncMiscParameterFrameRate *)misc->data;
   for (int i = 0; i < ARRAY_SIZE(context->desc.av1enc.rc); i++) {
      if (fr->framerate & 0xffff0000) {
         context->desc.av1enc.rc[i].frame_rate_num = fr->framerate       & 0xffff;
         context->desc.av1enc.rc[i].frame_rate_den = fr->framerate >> 16 & 0xffff;
      } else {
         context->desc.av1enc.rc[i].frame_rate_num = fr->framerate;
         context->desc.av1enc.rc[i].frame_rate_den = 1;
      }
   }

   return VA_STATUS_SUCCESS;
}

void getEncParamPresetAV1(vlVaContext *context)
{
   for (int i = 0; i < ARRAY_SIZE(context->desc.av1enc.rc); i++)  {
      struct pipe_av1_enc_rate_control *rc = &context->desc.av1enc.rc[i];

      rc->vbv_buffer_size = 20000000;
      rc->vbv_buf_lv = 48;
      rc->fill_data_enable = 1;
      rc->enforce_hrd = 1;
      rc->max_qp = 255;
      rc->min_qp = 1;

      if (rc->frame_rate_num == 0 ||
          rc->frame_rate_den == 0) {
         rc->frame_rate_num = 30;
         rc->frame_rate_den = 1;
      }

      if (rc->target_bitrate == 0)
         rc->target_bitrate = 20 * 1000000;

      if (rc->peak_bitrate == 0)
         rc->peak_bitrate = rc->target_bitrate * 3 / 2;

      rc->target_bits_picture = rc->target_bitrate * rc->frame_rate_den /
                                rc->frame_rate_num;

      rc->peak_bits_picture_integer = rc->peak_bitrate * rc->frame_rate_den /
                                rc->frame_rate_num;

      rc->peak_bits_picture_fraction = 0;
   }
}

#endif /* VA_CHECK_VERSION(1, 16, 0) */
