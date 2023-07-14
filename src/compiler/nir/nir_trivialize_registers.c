/*
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/bitscan.h"
#include "util/hash_table.h"
#include "util/list.h"
#include "util/ralloc.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"
#include "nir_intrinsics_indices.h"

/*
 * If we have NIR like
 *
 *    x = load_reg reg
 *    use(x)
 *
 * we can translate to a single instruction use(reg) in one step by inspecting
 * the parent instruction of x, which is convenient for instruction selection
 * that historically used registers.
 *
 * However, if we have an intervening store
 *
 *    x = load_reg reg
 *    store_reg reg, y
 *    use(x)
 *
 * we are no longer able to translate to use(reg), since reg has been
 * overwritten. We could detect the write-after-read hazard at instruction
 * selection time, but that requires an O(n) walk of instructions for each
 * register source read, leading to quadratic compile time. Instead, we ensure
 * this hazard does not happen and then use the simple O(1) translation.
 *
 * We say that a load_register is "trivial" if every use is in the same
 * block and there is no intervening store_register (write-after-read) between
 * the load and the use.
 *
 * Similar, a store_register is trivial if:
 *
 * 1. the value stored has exactly one use (the store)
 *
 * 2. the value is written in the same block as the store, and there does not
 * exist any intervening load_reg (read-after-write) from that register or
 * store_register (write-after-write) to that register with intersecting write
 * masks.
 *
 * 3. the producer is not a load_const or ssa_undef (as these historically could
 * not write to registers so backends are expecting SSA here), or a load_reg
 * (since backends need a move to copy between registers)
 *
 * 4. if indirect, the indirect index is live at the producer.
 *
 * This pass inserts copies to ensure that all load_reg/store_reg are trivial.
 */

 /*
 * Any load can be trivialized by copying immediately after the load and then
 * rewriting uses of the load to read from the copy. That has no functional
 * change, but it means that for every use of the load (the copy), there is no
 * intervening instruction and in particular no intervening store on any control
 * flow path. Therefore the load is trivial.
 */
static void
trivialize_load(nir_intrinsic_instr *load)
{
   assert(nir_is_load_reg(load));

   nir_builder b = nir_builder_at(nir_after_instr(&load->instr));
   nir_ssa_def *copy = nir_mov(&b, &load->dest.ssa);
   nir_ssa_def_rewrite_uses_after(&load->dest.ssa, copy, copy->parent_instr);

   assert(list_is_singular(&load->dest.ssa.uses));
}

static bool
trivialize_src(nir_src *src, void *trivial_regs_)
{
   BITSET_WORD *trivial_regs = trivial_regs_;

   assert(src->is_ssa && "register intrinsics only");
   nir_instr *parent = src->ssa->parent_instr;
   if (parent->type != nir_instr_type_intrinsic)
      return true;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(parent);
   if (!nir_is_load_reg(intr))
      return true;

   unsigned reg_index = intr->src[0].ssa->index;
   if (!BITSET_TEST(trivial_regs, reg_index))
      trivialize_load(intr);

   return true;
}

static void
trivialize_loads(nir_function_impl *impl, nir_block *block)
{
   BITSET_WORD *trivial_regs =
         calloc(BITSET_WORDS(impl->ssa_alloc), sizeof(BITSET_WORD));

   nir_foreach_instr_safe(instr, block) {
      nir_foreach_src(instr, trivialize_src, trivial_regs);

      /* We maintain a set of registers which can be accessed trivially. When we
       * hit a load, the register becomes trivial. When the register is stored,
       * the register becomes nontrivial again. That means the window between
       * the load and the store is where the register can be accessed legally.
       */
      if (instr->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

         /* We don't consider indirect loads to ever be trivial */
         if (intr->intrinsic == nir_intrinsic_load_reg_indirect)
            trivialize_load(intr);
         else if (intr->intrinsic == nir_intrinsic_load_reg)
            BITSET_SET(trivial_regs, intr->src[0].ssa->index);
         else if (nir_is_store_reg(intr))
            BITSET_CLEAR(trivial_regs, intr->src[1].ssa->index);
      }
   }

   /* Also check the condition of the next if */
   nir_if *nif = nir_block_get_following_if(block);
   if (nif)
      trivialize_src(&nif->condition, trivial_regs);

   free(trivial_regs);
}

/*
 * Any store can be made trivial by inserting a copy of the value immediately
 * before the store and reading from the copy instead. Proof:
 *
 * 1. The new value stored (the copy result) is used exactly once.
 *
 * 2. No intervening instructions between the copy and the store.
 *
 * 3. The copy is ALU, not load_const or ssa_undef.
 *
 * 4. The indirect index must be live at the store, which means it is also
 * live at the copy inserted immediately before the store (same live-in set),
 * so it is live at the new producer (the copy).
 */
static void
isolate_store(nir_intrinsic_instr *store)
{
   assert(nir_is_store_reg(store));

   nir_builder b = nir_builder_at(nir_before_instr(&store->instr));
   nir_ssa_def *copy = nir_mov(&b, store->src[0].ssa);
   nir_instr_rewrite_src_ssa(&store->instr, &store->src[0], copy);
}

static void
clear_store(nir_intrinsic_instr *store,
            unsigned num_reg_components,
            nir_intrinsic_instr **reg_stores)
{
   nir_component_mask_t mask = nir_intrinsic_write_mask(store);
   u_foreach_bit(c, mask) {
      assert(c < num_reg_components);
      assert(reg_stores[c] == store);
      reg_stores[c] = NULL;
   }
}

static void
clear_reg_stores(nir_ssa_def *reg,
                 struct hash_table *possibly_trivial_stores)
{
   /* At any given point in store trivialize pass, every store in the current
    * block is either trivial or in the possibly_trivial_stores map.
    */
   struct hash_entry *entry =
      _mesa_hash_table_search(possibly_trivial_stores, reg);
   if (entry == NULL)
      return;

   nir_intrinsic_instr **stores = entry->data;
   nir_intrinsic_instr *decl = nir_reg_get_decl(reg);
   unsigned num_components = nir_intrinsic_num_components(decl);

   for (unsigned c = 0; c < num_components; c++) {
      if (stores[c] == NULL)
         continue;

      clear_store(stores[c], num_components, stores);
   }
}

static void
trivialize_store(nir_intrinsic_instr *store,
                 struct hash_table *possibly_trivial_stores)
{
   nir_ssa_def *reg = store->src[1].ssa;

   /* At any given point in store trivialize pass, every store in the current
    * block is either trivial or in the possibly_trivial_stores map.
    */
   struct hash_entry *entry =
      _mesa_hash_table_search(possibly_trivial_stores, reg);
   if (entry == NULL)
      return;

   nir_intrinsic_instr **stores = entry->data;
   nir_intrinsic_instr *decl = nir_reg_get_decl(reg);
   unsigned num_components = nir_intrinsic_num_components(decl);

   nir_component_mask_t found = 0;
   for (unsigned c = 0; c < num_components; c++) {
      if (stores[c] == store)
         found |= BITFIELD_BIT(c);
   }
   if (!found)
      return;

   /* A store can't be only partially trivial */
   assert(found == nir_intrinsic_write_mask(store));

   isolate_store(store);
   clear_store(store, num_components, stores);
}

static void
trivialize_reg_stores(nir_ssa_def *reg, nir_component_mask_t mask,
                      struct hash_table *possibly_trivial_stores)
{
   /* At any given point in store trivialize pass, every store in the current
    * block is either trivial or in the possibly_trivial_stores map.
    */
   struct hash_entry *entry =
      _mesa_hash_table_search(possibly_trivial_stores, reg);
   if (entry == NULL)
      return;

   nir_intrinsic_instr **stores = entry->data;
   nir_intrinsic_instr *decl = nir_reg_get_decl(reg);
   unsigned num_components = nir_intrinsic_num_components(decl);

   u_foreach_bit(c, mask) {
      assert(c < num_components);
      if (stores[c] == NULL)
         continue;

      isolate_store(stores[c]);
      clear_store(stores[c], num_components, stores);
   }
}

static bool
clear_def(nir_ssa_def *def, void *state)
{
   struct hash_table *possibly_trivial_stores = state;

   nir_foreach_use(src, def) {
      if (src->is_if)
         continue;

      nir_instr *parent = src->parent_instr;
      if (parent->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *store = nir_instr_as_intrinsic(parent);
      if (!nir_is_store_reg(store))
         continue;

      /* Anything global has already been trivialized and can be ignored */
      if (parent->block != def->parent_instr->block)
         continue;

      if (def == store->src[0].ssa) {
         /* We encountered a value which is written by a store_reg so, if this
          * store is still in possibly_trivial_stores, it is trivial and we
          * can remove it from the set.
          */
         assert(list_is_singular(&def->uses));
         clear_reg_stores(store->src[1].ssa, possibly_trivial_stores);
      } else {
         /* We encoutered either the ineirect index or the decl_reg (unlikely)
          * before the value while iterating backwards.  Trivialize the store
          * now to maintain dominance.
          */
         trivialize_store(store, possibly_trivial_stores);
      }
   }

   return false;
}

static void
trivialize_stores(nir_function_impl *impl, nir_block *block)
{
   /* Hash table mapping decl_reg defs to a num_components-size array of
    * nir_intrinsic_instr*s. Each component contains the pointer to the next
    * store to that component, if one exists in the block while walking
    * backwards that has not yet had an intervening load, or NULL otherwise.
    * This represents the set of stores that, at the current point of iteration,
    * could be trivial.
    */
   struct hash_table *possibly_trivial_stores =
      _mesa_pointer_hash_table_create(NULL);

   nir_foreach_instr_reverse_safe(instr, block) {
      nir_foreach_ssa_def(instr, clear_def, possibly_trivial_stores);

      if (instr->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

         if (nir_is_load_reg(intr)) {
            /* Read-after-write: there is a load between the def and store. */
            unsigned nr = intr->dest.ssa.num_components;
            trivialize_reg_stores(intr->src[0].ssa, nir_component_mask(nr),
                                  possibly_trivial_stores);
         } else if (nir_is_store_reg(intr)) {
            nir_ssa_def *value = intr->src[0].ssa;
            nir_ssa_def *reg = intr->src[1].ssa;
            nir_intrinsic_instr *decl = nir_reg_get_decl(reg);
            unsigned num_components = nir_intrinsic_num_components(decl);
            nir_component_mask_t write_mask = nir_intrinsic_write_mask(intr);
            bool nontrivial = false;

            /* Write-after-write dependency */
            trivialize_reg_stores(reg, write_mask, possibly_trivial_stores);

            /* We don't consider indirect stores to be trivial */
            nontrivial |= intr->intrinsic == nir_intrinsic_store_reg_indirect;

            /* If there are multiple uses, not trivial */
            nontrivial |= !list_is_singular(&value->uses);

            /* SSA-only instruction types */
            nir_instr *parent = value->parent_instr;
            nontrivial |= (parent->type == nir_instr_type_load_const) ||
                          (parent->type == nir_instr_type_ssa_undef);

            /* Must be written in the same block */
            nontrivial |= (parent->block != block);

            /* Don't allow write masking with non-ALU types for compatibility,
             * since other types didn't have write masks in old NIR.
             */
            nontrivial |=
               (write_mask != nir_component_mask(num_components) &&
                parent->type != nir_instr_type_alu);

            /* Need a move for register copies */
            nontrivial |= parent->type == nir_instr_type_intrinsic &&
                          nir_is_load_reg(nir_instr_as_intrinsic(parent));

            if (nontrivial) {
               isolate_store(intr);
            } else {
               /* This store might be trivial. Record it. */
               nir_intrinsic_instr **stores = NULL;

               struct hash_entry *entry =
                  _mesa_hash_table_search(possibly_trivial_stores, reg);

               if (entry) {
                  stores = entry->data;
               } else {
                  stores = rzalloc_array(possibly_trivial_stores,
                                         nir_intrinsic_instr *,
                                         num_components);

                  _mesa_hash_table_insert(possibly_trivial_stores, reg, stores);
               }

               u_foreach_bit(c, write_mask) {
                  assert(c < num_components);
                  assert(stores[c] == NULL);
                  stores[c] = intr;
               }
            }
         }
      }
   }

   _mesa_hash_table_destroy(possibly_trivial_stores, NULL);
}

void
nir_trivialize_registers(nir_shader *s)
{
   nir_foreach_function_impl(impl, s) {
      nir_foreach_block(block, impl) {
         trivialize_loads(impl, block);
         trivialize_stores(impl, block);
      }
   }
}
