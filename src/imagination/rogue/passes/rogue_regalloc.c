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
#include "util/ralloc.h"
#include "util/register_allocate.h"

#include <stdbool.h>

/**
 * \file rogue_regalloc.c
 *
 * \brief Contains the rogue_regalloc pass.
 */

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

   /* Ensure that ssa regs are continuous from zero, and have no gaps. */
   rogue_foreach_reg (reg, shader, ROGUE_REG_CLASS_SSA) {
      assert(reg->index < num_ssa_regs);
   }

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

   rogue_live_range *ssa_live_range =
      rzalloc_array_size(shader, sizeof(*ssa_live_range), num_ssa_regs);
   for (unsigned u = 0; u < num_ssa_regs; ++u)
      ssa_live_range[u].start = ~0U;

   /* Populate live ranges for register arrays. */
   rogue_foreach_regarray (regarray, shader) {
      enum rogue_reg_class class = regarray->regs[0]->class;
      if (class != ROGUE_REG_CLASS_SSA)
         continue;

      for (unsigned u = 0; u < regarray->size; ++u) {
         rogue_reg *reg = regarray->regs[u];
         rogue_live_range *live_range = &ssa_live_range[reg->index];

         assert(list_is_singular(&regarray->writes) ||
                list_is_empty(&regarray->writes));
         if (!list_is_empty(&regarray->writes)) {
            rogue_regarray_write *write =
               list_first_entry(&regarray->writes, rogue_regarray_write, link);
            live_range->start = MIN2(live_range->start, write->instr->index);
         }

         rogue_foreach_regarray_use (use, regarray) {
            live_range->end = MAX2(live_range->end, use->instr->index);
         }

         /* Here dirty represents whether the register has been added to the
          * regset yet or not. */
         reg->dirty = false;
      }
   }

   /* Populate live ranges for registers. */
   rogue_foreach_reg (reg, shader, ROGUE_REG_CLASS_SSA) {
      if (reg->regarray)
         continue;

      rogue_live_range *live_range = &ssa_live_range[reg->index];

      assert(list_is_singular(&reg->writes) || list_is_empty(&reg->writes));
      if (!list_is_empty(&reg->writes)) {
         rogue_reg_write *write =
            list_first_entry(&reg->writes, rogue_reg_write, link);
         live_range->start = MIN2(live_range->start, write->instr->index);
      }

      rogue_foreach_reg_use (use, reg) {
         live_range->end = MAX2(live_range->end, use->instr->index);
      }

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

      enum rogue_regalloc_class raclass;

      if (regarray->size == 2)
         raclass = ROGUE_REGALLOC_CLASS_TEMP_2;
      else if (regarray->size == 4)
         raclass = ROGUE_REGALLOC_CLASS_TEMP_4;
      else
         unreachable("Unsupported regarray size.");

      ra_set_node_class(ra_graph,
                        regarray->regs[0]->index,
                        ra_get_class_from_index(ra_regs, raclass));

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
      rogue_live_range *live_range0 = &ssa_live_range[index0];

      for (unsigned index1 = 0; index1 < num_ssa_regs; ++index1) {
         if (index0 == index1)
            continue;

         rogue_live_range *live_range1 = &ssa_live_range[index1];

         /* If the live ranges overlap, those register nodes interfere. */
         if (!(live_range0->start >= live_range1->end ||
               live_range1->start >= live_range0->end))
            ra_add_node_interference(ra_graph, index0, index1);
      }
   }

   /* TODO: Spilling support. */
   if (!ra_allocate(ra_graph))
      unreachable("Register allocation failed.");

   unsigned regarray_count = list_length(&shader->regarrays);
   rogue_regarray **parent_regarrays =
      rzalloc_array_size(shader, sizeof(*parent_regarrays), regarray_count);

   unsigned num_parent_regarrays = 0;
   rogue_foreach_regarray (regarray, shader) {
      if (regarray->parent || regarray->regs[0]->class != ROGUE_REG_CLASS_SSA)
         continue;

      parent_regarrays[num_parent_regarrays++] = regarray;
   }

   for (unsigned u = 0; u < num_parent_regarrays; ++u) {
      rogue_regarray *regarray = parent_regarrays[u];

      unsigned start_index = regarray->regs[0]->index;
      unsigned new_base_index = ra_get_node_reg(ra_graph, start_index);
      enum rogue_regalloc_class ra_class =
         ra_class_index(ra_get_node_class(ra_graph, start_index));
      enum rogue_reg_class new_class = regalloc_info[ra_class].class;

      bool used = false;
      for (unsigned r = 0; r < regarray->size; ++r)
         used |= rogue_reg_is_used(shader, new_class, new_base_index + r);

      /* First time using new regarray, modify in place. */
      if (!used) {
         progress |=
            rogue_regarray_rewrite(shader, regarray, new_class, new_base_index);
      } else {
         /* Regarray has already been used, replace references and delete. */

         /* Replace parent regarray first. */
         rogue_regarray *new_regarray = rogue_regarray_cached(shader,
                                                              regarray->size,
                                                              new_class,
                                                              new_base_index);
         progress |= rogue_regarray_replace(regarray, new_regarray);

         /* Replace subarrays. */
         rogue_foreach_subarray_safe (subarray, regarray) {
            unsigned idx_offset =
               subarray->regs[0]->index - regarray->regs[0]->index;
            new_regarray = rogue_regarray_cached(shader,
                                                 subarray->size,
                                                 new_class,
                                                 new_base_index + idx_offset);
            progress |= rogue_regarray_replace(subarray, new_regarray);
            rogue_regarray_delete(subarray);
         }

         rogue_regarray_delete(regarray);
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
      if (!rogue_reg_is_used(shader, new_class, new_index)) {
         progress |= rogue_reg_rewrite(shader, reg, new_class, new_index);
      } else {
         /* Register has already been used, replace references and delete. */
         assert(list_is_singular(&reg->writes)); /* SSA reg. */
         rogue_reg *new_reg = rogue_temp_reg(shader, new_index);
         progress |= rogue_reg_replace(reg, new_reg);
      }
   }

   num_temp_regs = list_length(&shader->regs[ROGUE_REG_CLASS_TEMP]);
   /* Ensure that temp regs are continuous from zero, and have no gaps. */
   rogue_foreach_reg (reg, shader, ROGUE_REG_CLASS_TEMP) {
      assert(reg->index < num_temp_regs);
   }

   ralloc_free(parent_regarrays);
   ralloc_free(ssa_live_range);
   ralloc_free(ra_regs);
   return progress;
}
