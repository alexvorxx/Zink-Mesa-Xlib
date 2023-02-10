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

#include "rogue.h"
#include "util/macros.h"
#include "util/register_allocate.h"
#include "util/sparse_array.h"

#include <stdbool.h>

/**
 * \file rogue_regalloc.c
 *
 * \brief Contains the rogue_regalloc pass.
 */

/* TODO: Tweak this value. */
#define ROGUE_SSA_LIVE_RANGE_NODE_SIZE 512

/* TODO: Internal register support for high register pressure regs. */

typedef struct rogue_live_range {
   unsigned start;
   unsigned end;
} rogue_live_range;

PUBLIC
bool rogue_regalloc(rogue_shader *shader)
{
   if (shader->is_grouped)
      return false;

   bool progress = false;

   unsigned num_ssa_regs = list_length(&shader->regs[ROGUE_REG_CLASS_SSA]);
   if (!num_ssa_regs)
      return false;

   /* If we already have some temps in use in the shader, we'll skip using them
    * for allocation. */
   unsigned num_temp_regs = list_length(&shader->regs[ROGUE_REG_CLASS_TEMP]);
   unsigned hw_temps = rogue_reg_infos[ROGUE_REG_CLASS_TEMP].num;

   struct ra_regs *ra_regs = ra_alloc_reg_set(shader, hw_temps, true);

   for (enum rogue_regalloc_class c = 0; c < ROGUE_REGALLOC_CLASS_COUNT; ++c) {
      ASSERTED struct ra_class *ra_class =
         ra_alloc_contig_reg_class(ra_regs, regalloc_info[c].stride);
      assert(c == ra_class_index(ra_class));
   }

   for (unsigned t = num_temp_regs; t < hw_temps; ++t)
      for (enum rogue_regalloc_class c = 0; c < ROGUE_REGALLOC_CLASS_COUNT; ++c)
         if (!(t % regalloc_info[c].stride))
            ra_class_add_reg(ra_get_class_from_index(ra_regs, c), t);

   ra_set_finalize(ra_regs, NULL);

   struct util_sparse_array ssa_live_range;
   util_sparse_array_init(&ssa_live_range,
                          sizeof(rogue_live_range),
                          ROGUE_SSA_LIVE_RANGE_NODE_SIZE);

   /* Populate live ranges. */
   rogue_foreach_reg (reg, shader, ROGUE_REG_CLASS_SSA) {
      rogue_live_range *live_range =
         util_sparse_array_get(&ssa_live_range, reg->index);
      rogue_reg_write *write =
         list_first_entry(&reg->writes, rogue_reg_write, link);

      live_range->start = write->instr->index;
      live_range->end = live_range->start;

      rogue_foreach_reg_use (use, reg)
         live_range->end = MAX2(live_range->end, use->instr->index);

      /* Here dirty represents whether the register has been added to the regset
       * yet or not. */
      reg->dirty = false;
   }

   struct ra_graph *ra_graph =
      ra_alloc_interference_graph(ra_regs, num_ssa_regs);
   ralloc_steal(ra_regs, ra_graph);

   /* Set register class for regarrays/vectors. */
   rogue_foreach_regarray (regarray, shader) {
      enum rogue_reg_class class = regarray->regs[0]->class;
      if (class != ROGUE_REG_CLASS_SSA)
         continue;

      if (regarray->parent)
         continue;

      if (regarray->size != 4)
         unreachable("Unsupported regarray size.");

      ra_set_node_class(ra_graph,
                        regarray->regs[0]->index,
                        ra_get_class_from_index(ra_regs,
                                                ROGUE_REGALLOC_CLASS_TEMP_4));

      for (unsigned u = 0; u < regarray->size; ++u)
         regarray->regs[u]->dirty = true;
   }

   /* Set register class for "standalone" registers. */
   rogue_foreach_reg (reg, shader, ROGUE_REG_CLASS_SSA) {
      if (reg->dirty)
         continue;

      ra_set_node_class(ra_graph,
                        reg->index,
                        ra_get_class_from_index(ra_regs,
                                                ROGUE_REGALLOC_CLASS_TEMP_1));
      reg->dirty = true;
   }

   /* Build interference graph from overlapping live ranges. */
   for (unsigned index0 = 0; index0 < num_ssa_regs; ++index0) {
      rogue_live_range *live_range0 =
         util_sparse_array_get(&ssa_live_range, index0);

      for (unsigned index1 = 0; index1 < num_ssa_regs; ++index1) {
         if (index0 == index1)
            continue;

         rogue_live_range *live_range1 =
            util_sparse_array_get(&ssa_live_range, index1);

         /* If the live ranges overlap, those register nodes interfere. */
         if (!(live_range0->start >= live_range1->end ||
               live_range1->start >= live_range0->end))
            ra_add_node_interference(ra_graph, index0, index1);
      }
   }

   /* Same src/dst interferences are disabled for the moment.
    * This may need to be re-enabled in the future as certain instructions have
    * restrictions on this.
    */
#if 0
   /* Add node interferences such that the same register can't be used for
    * both an instruction's source and destination.
    */
   rogue_foreach_instr_in_shader (instr, shader) {
      switch (instr->type) {
         case ROGUE_INSTR_TYPE_ALU:
         {
            const rogue_alu_instr *alu = rogue_instr_as_alu(instr);
            const rogue_alu_op_info *info = &rogue_alu_op_infos[alu->op];

            if (rogue_ref_get_reg_class(&alu->dst.ref) != ROGUE_REG_CLASS_SSA)
               continue;

            for (unsigned s = 0; s < info->num_srcs; ++s) {
               if (!rogue_ref_is_reg(&alu->src[s].ref))
                  continue;

               if (rogue_ref_get_reg_class(&alu->src[s].ref) != ROGUE_REG_CLASS_SSA)
                  continue;

               ra_add_node_interference(ra_graph, rogue_ref_get_reg_index(&alu->dst.ref), rogue_ref_get_reg_index(&alu->src[s].ref));
            }

            break;
         }

         case ROGUE_INSTR_TYPE_BACKEND:
         {
            const rogue_backend_instr *backend = rogue_instr_as_backend(instr);
            const rogue_backend_op_info *info = &rogue_backend_op_infos[backend->op];

            for (unsigned d = 0; d < info->num_dsts; ++d) {
               if (rogue_ref_get_reg_class(&backend->dst[d].ref) != ROGUE_REG_CLASS_SSA)
                  continue;

               for (unsigned s = 0; s < info->num_srcs; ++s) {
                  if (!rogue_ref_is_reg(&backend->src[s].ref))
                     continue;

                  if (rogue_ref_get_reg_class(&backend->src[s].ref) != ROGUE_REG_CLASS_SSA)
                     continue;

                  ra_add_node_interference(ra_graph, rogue_ref_get_reg_index(&backend->dst[d].ref), rogue_ref_get_reg_index(&backend->src[s].ref));
               }
            }

            break;
         }

         case ROGUE_INSTR_TYPE_CTRL:
         {
            /* TODO: Support control instructions with I/O. */
            break;
         }

         default:
            unreachable("Unsupported instruction type.");
            break;
      }
   }
#endif

   /* TODO: Spilling support. */
   if (!ra_allocate(ra_graph))
      unreachable("Register allocation failed.");

   /* Replace regarray SSA registers with allocated physical registers first.
    * Don't want to be in a situation where the reg is updated before the
    * regarray.
    */
   rogue_foreach_regarray (regarray, shader) {
      enum rogue_reg_class class = regarray->regs[0]->class;
      if (class != ROGUE_REG_CLASS_SSA)
         continue;

      if (regarray->parent)
         continue;

      unsigned start_index = regarray->regs[0]->index;
      unsigned new_base_index = ra_get_node_reg(ra_graph, start_index);
      for (unsigned u = 0; u < regarray->size; ++u) {
         enum rogue_regalloc_class ra_class =
            ra_class_index(ra_get_node_class(ra_graph, start_index + u));
         enum rogue_reg_class new_class = regalloc_info[ra_class].class;

         /* Register should not have already been used. */
         assert(!BITSET_TEST(shader->regs_used[new_class], new_base_index + u));
         progress |= rogue_reg_rewrite(shader,
                                       regarray->regs[u],
                                       new_class,
                                       new_base_index + u);
      }
   }

   /* Replace remaining standalone SSA registers with allocated physical
    * registers. */
   rogue_foreach_reg_safe (reg, shader, ROGUE_REG_CLASS_SSA) {
      assert(!reg->regarray);
      unsigned new_index = ra_get_node_reg(ra_graph, reg->index);

      enum rogue_regalloc_class ra_class =
         ra_class_index(ra_get_node_class(ra_graph, reg->index));
      enum rogue_reg_class new_class = regalloc_info[ra_class].class;

      /* First time using new register, modify in place. */
      if (!BITSET_TEST(shader->regs_used[new_class], new_index)) {
         progress |= rogue_reg_rewrite(shader, reg, new_class, new_index);
      } else {
         /* Register has already been used, replace references and delete. */
         assert(list_is_singular(&reg->writes)); /* SSA reg. */
         rogue_reg *new_reg = rogue_temp_reg(shader, new_index);
         progress |= rogue_reg_replace(reg, new_reg);
      }
   }

   util_sparse_array_finish(&ssa_live_range);
   ralloc_free(ra_regs);
   return progress;
}
