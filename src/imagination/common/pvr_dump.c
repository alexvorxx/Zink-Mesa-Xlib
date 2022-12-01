/*
 * Copyright © 2022 Imagination Technologies Ltd.
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

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "pvr_dump.h"
#include "pvr_util.h"

const struct pvr_dump_ctx __pvr_dump_ctx_invalid = {
   .active_child = &__pvr_dump_ctx_invalid,
};

/*****************************************************************************
   Hex dumps
*****************************************************************************/

#define HEX_WORD_SIZE ((unsigned)sizeof(uint32_t))
#define HEX_WORD_FMT "%08" PRIx32

/* This must be even, and should probably always be a power of 2. */
#define HEX_LINE_SIZE UINT32_C(8)

struct pvr_dump_hex_ctx {
   struct pvr_dump_ctx base;

   const uint32_t *start_ptr;
   const uint32_t *end_ptr;

   uint64_t nr_words;
   uint32_t offset_digits;

   /* User-modifiable values */
   const uint32_t *line_ptr;

   uint32_t prev_non_zero_trailing_zero_words;
   uint64_t prev_non_zero_leading_zero_lines;
   const uint32_t *prev_non_zero_line;
   uint64_t zero_lines;
};

static bool pvr_dump_hex_ctx_push(struct pvr_dump_hex_ctx *const ctx,
                                  struct pvr_dump_buffer_ctx *const parent_ctx,
                                  const uint64_t nr_words)
{
   const uint64_t real_nr_words =
      nr_words ? nr_words : parent_ctx->remaining_size / HEX_WORD_SIZE;
   const uint64_t nr_bytes = real_nr_words * HEX_WORD_SIZE;
   bool ret;

   if (parent_ctx->remaining_size < nr_bytes ||
       (!nr_words && nr_bytes != parent_ctx->remaining_size) ||
       !ptr_is_aligned(parent_ctx->ptr, HEX_WORD_SIZE)) {
      return false;
   }

   ret = pvr_dump_ctx_push(&ctx->base, &parent_ctx->base);
   if (!ret)
      return false;

   ctx->start_ptr = parent_ctx->ptr;
   ctx->end_ptr = ctx->start_ptr + real_nr_words;
   ctx->nr_words = real_nr_words;
   ctx->offset_digits = u64_hex_digits(nr_bytes);

   ctx->line_ptr = ctx->start_ptr;

   ctx->prev_non_zero_trailing_zero_words = 0;
   ctx->prev_non_zero_leading_zero_lines = 0;
   ctx->prev_non_zero_line = NULL;
   ctx->zero_lines = 0;

   return true;
}

static struct pvr_dump_buffer_ctx *
pvr_dump_hex_ctx_pop(struct pvr_dump_hex_ctx *const ctx)
{
   struct pvr_dump_buffer_ctx *parent;
   struct pvr_dump_ctx *parent_base;

   if (ctx->line_ptr != ctx->end_ptr) {
      ctx->base.ok = false;
      return NULL;
   }

   parent_base = pvr_dump_ctx_pop(&ctx->base);
   if (!parent_base)
      return NULL;

   parent = container_of(parent_base, struct pvr_dump_buffer_ctx, base);

   pvr_dump_buffer_advance(parent, ctx->nr_words * HEX_WORD_SIZE);

   return parent;
}

static inline void pvr_dump_hex_print_prefix(const struct pvr_dump_hex_ctx *ctx,
                                             const uint64_t offset)
{
   pvr_dump_printf(&ctx->base,
                   PVR_DUMP_OFFSET_PREFIX,
                   ctx->offset_digits,
                   offset * HEX_WORD_SIZE);
}

#define pvr_dump_hex_println(ctx, offset, format, args...) \
   pvr_dump_println(&(ctx)->base,                          \
                    PVR_DUMP_OFFSET_PREFIX format,         \
                    (ctx)->offset_digits,                  \
                    offset,                                \
                    ##args);

#define pvr_dump_hex_println_no_prefix(ctx, format, args...) \
   pvr_dump_println(&(ctx)->base,                            \
                    "%*c" format,                            \
                    (ctx)->offset_digits + 3,                \
                    ' ',                                     \
                    ##args);

static void
pvr_dump_hex_print_zero_lines(const struct pvr_dump_hex_ctx *const ctx,
                              const uint64_t zero_lines)
{
   const uint64_t zero_words = zero_lines * HEX_LINE_SIZE;
   const uint64_t zero_bytes = zero_words * HEX_WORD_SIZE;

   if (zero_lines == 0)
      return;

   pvr_dump_hex_println_no_prefix(ctx,
                                  "  + %" PRIu64 " zero line%s (%" PRIu64
                                  " words; %" PRIu64 "/0x%" PRIx64 " bytes)",
                                  zero_lines,
                                  zero_lines == 1 ? "" : "s",
                                  zero_words,
                                  zero_bytes,
                                  zero_bytes);
}

static void
pvr_dump_hex_print_trailing_zeroes(const struct pvr_dump_hex_ctx *const ctx)
{
   const uint64_t zero_words =
      ctx->zero_lines * HEX_LINE_SIZE + ctx->prev_non_zero_trailing_zero_words;
   const uint64_t zero_bytes = zero_words * HEX_WORD_SIZE;

   if (!ctx->prev_non_zero_trailing_zero_words)
      return pvr_dump_hex_print_zero_lines(ctx, ctx->zero_lines);

   if (!ctx->zero_lines)
      return;

   pvr_dump_hex_println_no_prefix(ctx,
                                  "  + %" PRIu64 "+%" PRIu32
                                  " zero lines (%" PRIu64 " words; %" PRIu64
                                  "/0x%" PRIx64 " bytes)",
                                  ctx->zero_lines,
                                  ctx->prev_non_zero_trailing_zero_words,
                                  zero_words,
                                  zero_bytes,
                                  zero_bytes);
}

static void pvr_dump_hex_print_line(struct pvr_dump_hex_ctx *ctx,
                                    const uint32_t *const line_ptr,
                                    const uint32_t truncate)
{
   const uint32_t nr_words =
      MIN2(HEX_LINE_SIZE - truncate, ctx->end_ptr - line_ptr);

   pvr_dump_hex_print_prefix(ctx, line_ptr - ctx->start_ptr);

   for (uint32_t i = 0; i < nr_words; i++) {
      if (i == HEX_LINE_SIZE / 2)
         pvr_dump_printf_cont(&ctx->base, " ");

      pvr_dump_printf_cont(&ctx->base, " " HEX_WORD_FMT, line_ptr[i]);
   }

   pvr_dump_print_eol(&ctx->base);
}

static void pvr_dump_hex_process_line(struct pvr_dump_hex_ctx *const ctx)
{
   uint32_t trailing_zero_words = HEX_LINE_SIZE;

   for (uint32_t i = HEX_LINE_SIZE; i > 0; i--) {
      if (ctx->line_ptr[i - 1]) {
         trailing_zero_words = HEX_LINE_SIZE - i;
         break;
      }
   }

   if (trailing_zero_words == HEX_LINE_SIZE) {
      /* No non-zero words were found in this line; mark it and move on. */
      ctx->zero_lines++;
      return;
   }

   /* We have at least one non-zero word in this line. If we have a previous
    * non-zero line stored, collapse and print any leading zero-only lines
    * before it then print the stored line.
    */
   if (ctx->prev_non_zero_line) {
      pvr_dump_hex_print_zero_lines(ctx, ctx->prev_non_zero_leading_zero_lines);
      pvr_dump_hex_print_line(ctx, ctx->prev_non_zero_line, 0);
   }

   /* Now we store the current non-zero line for printing later. This way we
    * can treat the last non-zero line specially.
    */
   ctx->prev_non_zero_line = ctx->line_ptr;
   ctx->prev_non_zero_leading_zero_lines = ctx->zero_lines;
   ctx->prev_non_zero_trailing_zero_words = trailing_zero_words;
   ctx->zero_lines = 0;
}

static void pvr_dump_hex(struct pvr_dump_hex_ctx *const ctx)
{
   while (ctx->end_ptr - ctx->line_ptr > 0) {
      pvr_dump_hex_process_line(ctx);
      ctx->line_ptr += HEX_LINE_SIZE;
   }

   if (ctx->prev_non_zero_line) {
      pvr_dump_hex_print_zero_lines(ctx, ctx->prev_non_zero_leading_zero_lines);
      pvr_dump_hex_print_line(ctx,
                              ctx->prev_non_zero_line,
                              ctx->prev_non_zero_trailing_zero_words);

      /* Collapse and print any trailing zeroes. */
      pvr_dump_hex_print_trailing_zeroes(ctx);
   } else {
      /* We made it to the end of the buffer without ever encountering a
       * non-zero word. Make this known.
       */
      pvr_dump_hex_println(ctx, UINT64_C(0), " <empty buffer>");
   }

   pvr_dump_hex_println(ctx, ctx->nr_words, " <end of buffer>");
}

bool pvr_dump_buffer_hex(struct pvr_dump_buffer_ctx *const ctx,
                         const uint64_t nr_words)
{
   struct pvr_dump_hex_ctx hex_ctx;

   if (!pvr_dump_hex_ctx_push(&hex_ctx, ctx, nr_words))
      return false;

   pvr_dump_hex(&hex_ctx);

   return !!pvr_dump_hex_ctx_pop(&hex_ctx);
}
