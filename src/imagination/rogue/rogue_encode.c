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
#include "rogue_isa.h"
#include "util/macros.h"
#include "util/u_dynarray.h"

#include <stdbool.h>

/**
 * \file rogue_encode.c
 *
 * \brief Contains hardware encoding functions.
 */

#define util_dynarray_append_mem(buf, size, mem) \
   memcpy(util_dynarray_grow_bytes((buf), 1, size), mem, size)

static unsigned rogue_calc_da(const rogue_instr_group *group)
{
   unsigned da = group->size.header;

   if (group->header.alu == ROGUE_ALU_MAIN) {
      for (unsigned u = ROGUE_INSTR_PHASE_COUNT; u > 0; --u) {
         enum rogue_instr_phase p = u - 1;
         if (p > ROGUE_INSTR_PHASE_1)
            da += group->size.instrs[p];
      }
   } else if (group->header.alu == ROGUE_ALU_BITWISE) {
      for (unsigned u = ROGUE_INSTR_PHASE_COUNT; u > 0; --u) {
         enum rogue_instr_phase p = u - 1;
         da += group->size.instrs[p];
      }
   } else if (group->header.alu == ROGUE_ALU_CONTROL) {
      const rogue_instr *instr = group->instrs[ROGUE_INSTR_PHASE_CTRL];
      const rogue_ctrl_instr *ctrl = rogue_instr_as_ctrl(instr);

      if (!rogue_ctrl_op_has_srcs(ctrl->op) &&
          !rogue_ctrl_op_has_dsts(ctrl->op)) {
         da = 0;
      } else {
         da += group->size.instrs[ROGUE_INSTR_PHASE_CTRL];
      }

   } else {
      unreachable("Unsupported instruction group ALU.");
   }

   return da;
}

static void rogue_encode_instr_group_header(rogue_instr_group *group,
                                            struct util_dynarray *binary)
{
   rogue_instr_group_header_encoding h = { 0 };

   h.da = rogue_calc_da(group);
   h.length = (group->size.total / 2) % 16;
   h.ext = (group->size.header == 3);

   rogue_ref *w0ref = rogue_instr_group_io_sel_ref(&group->io_sel, ROGUE_IO_W0);
   rogue_ref *w1ref = rogue_instr_group_io_sel_ref(&group->io_sel, ROGUE_IO_W1);

   /* TODO: Update this - needs to be set for MOVMSK, and if instruction group
    * READS OR WRITES to/from pixout regs. */
   h.olchk = rogue_ref_is_pixout(w0ref) || rogue_ref_is_pixout(w1ref);
   h.w1p = !rogue_ref_is_null(w1ref);
   h.w0p = !rogue_ref_is_null(w0ref);

   rogue_cc cc = { 0 };
   switch (group->header.exec_cond) {
   case ROGUE_EXEC_COND_PE_TRUE:
      cc._ = CC_PE_TRUE;
      break;

   case ROGUE_EXEC_COND_P0_TRUE:
      cc._ = CC_P0_TRUE;
      break;

   case ROGUE_EXEC_COND_PE_ANY:
      cc._ = CC_PE_ANY;
      break;

   case ROGUE_EXEC_COND_P0_FALSE:
      cc._ = CC_P0_FALSE;
      break;

   default:
      unreachable("Unsupported condition code.");
   }

   h.cc = cc.cc;
   h.ccext = cc.ccext;

   switch (group->header.alu) {
   case ROGUE_ALU_MAIN:
      h.alutype = ALUTYPE_MAIN;
      /* TODO: Support multiple phase instructions. */
#define P(type) BITFIELD64_BIT(ROGUE_INSTR_PHASE_##type)
      if (group->header.phases & P(0))
         h.oporg = OPORG_P0;
      if (group->header.phases & P(2_PCK) || group->header.phases & P(2_TST) ||
          group->header.phases & P(2_MOV))
         h.oporg = OPORG_P2;
      if (group->header.phases & P(BACKEND))
         h.oporg = OPORG_BE;
#undef P
      break;

   case ROGUE_ALU_BITWISE:
      h.alutype = ALUTYPE_BITWISE;
#define P(type) BITFIELD64_BIT(ROGUE_INSTR_PHASE_##type)
      if (group->header.phases & P(0_BITMASK) ||
          group->header.phases & P(0_SHIFT1) ||
          group->header.phases & P(0_COUNT))
         h.oporg |= OPCNT_P0;
      if (group->header.phases & P(1_LOGICAL))
         h.oporg |= OPCNT_P1;
      if (group->header.phases & P(2_SHIFT2) ||
          group->header.phases & P(2_TEST))
         h.oporg |= OPCNT_P2;
#undef P
      break;

   case ROGUE_ALU_CONTROL:
      h.alutype = ALUTYPE_CONTROL;
      const rogue_instr *instr = group->instrs[ROGUE_INSTR_PHASE_CTRL];
      const rogue_ctrl_instr *ctrl = rogue_instr_as_ctrl(instr);
      switch (ctrl->op) {
      case ROGUE_CTRL_OP_WDF:
         h.ctrlop = CTRLOP_WDF;
         h.miscctl = rogue_ref_get_drc_index(&ctrl->src[0].ref);
         break;

      case ROGUE_CTRL_OP_NOP:
         h.ctrlop = CTRLOP_NOP;
         h.miscctl = rogue_ctrl_op_mod_is_set(ctrl, ROGUE_CTRL_OP_MOD_END);
         break;

      default:
         unreachable("Unsupported ctrl op.");
      }
      break;

   default:
      unreachable("Unsupported instruction group ALU.");
   }

   if (group->header.alu != ROGUE_ALU_CONTROL) {
      h.end = group->header.end;
      /* h.crel = ; */ /* Unused for now */
      /* h.atom = ; */ /* Unused for now */
      h.rpt = group->header.repeat - 1;
   }

   util_dynarray_append_mem(binary, group->size.header, &h);
}

typedef union rogue_instr_encoding {
   rogue_alu_instr_encoding alu;
   rogue_backend_instr_encoding backend;
   rogue_ctrl_instr_encoding ctrl;
} PACKED rogue_instr_encoding;

#define SM(src_mod) ROGUE_ALU_SRC_MOD_##src_mod
#define DM(dst_mod) ROGUE_ALU_DST_MOD_##dst_mod
#define OM(op_mod) ROGUE_ALU_OP_MOD_##op_mod
static void rogue_encode_alu_instr(const rogue_alu_instr *alu,
                                   unsigned instr_size,
                                   rogue_instr_encoding *instr_encoding)
{
   switch (alu->op) {
   case ROGUE_ALU_OP_MBYP:
      instr_encoding->alu.op = ALUOP_SNGL;
      instr_encoding->alu.sngl.snglop = SNGLOP_BYP;

      if (instr_size == 2) {
         instr_encoding->alu.sngl.ext0 = 1;
         instr_encoding->alu.sngl.mbyp.s0neg =
            rogue_alu_src_mod_is_set(alu, 0, SM(NEG));
         instr_encoding->alu.sngl.mbyp.s0abs =
            rogue_alu_src_mod_is_set(alu, 0, SM(ABS));
      }
      break;

   case ROGUE_ALU_OP_FMUL:
      instr_encoding->alu.op = ALUOP_FMUL;
      instr_encoding->alu.fmul.lp = rogue_alu_op_mod_is_set(alu, OM(LP));
      instr_encoding->alu.fmul.sat = rogue_alu_op_mod_is_set(alu, OM(SAT));
      instr_encoding->alu.fmul.s0neg =
         rogue_alu_src_mod_is_set(alu, 0, SM(NEG));
      instr_encoding->alu.fmul.s0abs =
         rogue_alu_src_mod_is_set(alu, 0, SM(ABS));
      instr_encoding->alu.fmul.s1abs =
         rogue_alu_src_mod_is_set(alu, 1, SM(ABS));
      instr_encoding->alu.fmul.s0flr =
         rogue_alu_src_mod_is_set(alu, 0, SM(FLR));
      break;

   case ROGUE_ALU_OP_FMAD:
      instr_encoding->alu.op = ALUOP_FMAD;
      instr_encoding->alu.fmad.s0neg =
         rogue_alu_src_mod_is_set(alu, 0, SM(NEG));
      instr_encoding->alu.fmad.s0abs =
         rogue_alu_src_mod_is_set(alu, 0, SM(ABS));
      instr_encoding->alu.fmad.s2neg =
         rogue_alu_src_mod_is_set(alu, 2, SM(NEG));
      instr_encoding->alu.fmad.sat = rogue_alu_op_mod_is_set(alu, OM(SAT));

      if (instr_size == 2) {
         instr_encoding->alu.fmad.ext = 1;
         instr_encoding->alu.fmad.lp = rogue_alu_op_mod_is_set(alu, OM(LP));
         instr_encoding->alu.fmad.s1abs =
            rogue_alu_src_mod_is_set(alu, 1, SM(ABS));
         instr_encoding->alu.fmad.s1neg =
            rogue_alu_src_mod_is_set(alu, 1, SM(NEG));
         instr_encoding->alu.fmad.s2flr =
            rogue_alu_src_mod_is_set(alu, 2, SM(FLR));
         instr_encoding->alu.fmad.s2abs =
            rogue_alu_src_mod_is_set(alu, 2, SM(ABS));
      }
      break;

   case ROGUE_ALU_OP_PCK_U8888:
      instr_encoding->alu.op = ALUOP_SNGL;
      instr_encoding->alu.sngl.snglop = SNGLOP_PCK;
      instr_encoding->alu.sngl.ext0 = 1;

      instr_encoding->alu.sngl.pck.pck.prog = 0;
      instr_encoding->alu.sngl.pck.pck.rtz =
         rogue_alu_op_mod_is_set(alu, OM(ROUNDZERO));
      instr_encoding->alu.sngl.pck.pck.scale =
         rogue_alu_op_mod_is_set(alu, OM(SCALE));
      instr_encoding->alu.sngl.pck.pck.format = PCK_FMT_U8888;
      break;

   default:
      unreachable("Unsupported alu op.");
   }
}
#undef OM
#undef DM
#undef SM

#define OM(op_mod) BITFIELD64_BIT(ROGUE_BACKEND_OP_MOD_##op_mod)
static void rogue_encode_backend_instr(const rogue_backend_instr *backend,
                                       unsigned instr_size,
                                       rogue_instr_encoding *instr_encoding)
{
   switch (backend->op) {
   case ROGUE_BACKEND_OP_FITRP_PIXEL:
      instr_encoding->backend.op = BACKENDOP_FITR;
      instr_encoding->backend.fitr.p = 1;
      instr_encoding->backend.fitr.drc =
         rogue_ref_get_drc_index(&backend->src[0].ref);
      instr_encoding->backend.fitr.mode = FITR_MODE_PIXEL;
      instr_encoding->backend.fitr.sat =
         rogue_backend_op_mod_is_set(backend, OM(SAT));
      instr_encoding->backend.fitr.count =
         rogue_ref_get_val(&backend->src[3].ref);
      break;

   case ROGUE_BACKEND_OP_UVSW_WRITE:
      instr_encoding->backend.op = BACKENDOP_UVSW;
      instr_encoding->backend.uvsw.writeop = UVSW_WRITEOP_WRITE;
      instr_encoding->backend.uvsw.imm = 1;
      instr_encoding->backend.uvsw.imm_src.imm_addr =
         rogue_ref_get_reg_index(&backend->dst[0].ref);
      break;

   case ROGUE_BACKEND_OP_UVSW_EMIT:
      instr_encoding->backend.op = BACKENDOP_UVSW;
      instr_encoding->backend.uvsw.writeop = UVSW_WRITEOP_EMIT;
      break;

   case ROGUE_BACKEND_OP_UVSW_ENDTASK:
      instr_encoding->backend.op = BACKENDOP_UVSW;
      instr_encoding->backend.uvsw.writeop = UVSW_WRITEOP_END;
      break;

   case ROGUE_BACKEND_OP_UVSW_EMITTHENENDTASK:
      instr_encoding->backend.op = BACKENDOP_UVSW;
      instr_encoding->backend.uvsw.writeop = UVSW_WRITEOP_EMIT_END;
      break;

   case ROGUE_BACKEND_OP_UVSW_WRITETHENEMITTHENENDTASK:
      instr_encoding->backend.op = BACKENDOP_UVSW;
      instr_encoding->backend.uvsw.writeop = UVSW_WRITEOP_WRITE_EMIT_END;
      instr_encoding->backend.uvsw.imm = 1;
      instr_encoding->backend.uvsw.imm_src.imm_addr =
         rogue_ref_get_reg_index(&backend->dst[0].ref);
      break;

   default:
      unreachable("Unsupported backend op.");
   }
}
#undef OM

static void rogue_encode_ctrl_instr(const rogue_ctrl_instr *ctrl,
                                    unsigned instr_size,
                                    rogue_instr_encoding *instr_encoding)
{
   /* Only some control instructions have additional bytes. */
   switch (ctrl->op) {
   case ROGUE_CTRL_OP_NOP:
      memset(&instr_encoding->ctrl.nop, 0, sizeof(instr_encoding->ctrl.nop));
      break;

   default:
      unreachable("Unsupported ctrl op.");
   }
}

/* TODO: Add p2end where required. */
static void rogue_encode_instr_group_instrs(rogue_instr_group *group,
                                            struct util_dynarray *binary)
{
   rogue_instr_encoding instr_encoding;

   /* Reverse order for encoding. */
   rogue_foreach_phase_in_set_rev (p, group->header.phases) {
      if (!group->size.instrs[p])
         continue;

      memset(&instr_encoding, 0, sizeof(instr_encoding));

      const rogue_instr *instr = group->instrs[p];
      switch (instr->type) {
      case ROGUE_INSTR_TYPE_ALU:
         rogue_encode_alu_instr(rogue_instr_as_alu(instr),
                                group->size.instrs[p],
                                &instr_encoding);
         break;

      case ROGUE_INSTR_TYPE_BACKEND:
         rogue_encode_backend_instr(rogue_instr_as_backend(instr),
                                    group->size.instrs[p],
                                    &instr_encoding);
         break;

      case ROGUE_INSTR_TYPE_CTRL:
         rogue_encode_ctrl_instr(rogue_instr_as_ctrl(instr),
                                 group->size.instrs[p],
                                 &instr_encoding);
         break;

      default:
         unreachable("Unsupported instruction type.");
      }

      util_dynarray_append_mem(binary, group->size.instrs[p], &instr_encoding);
   }
}

static void rogue_encode_source_map(const rogue_instr_group *group,
                                    bool upper_srcs,
                                    rogue_source_map_encoding *e)
{
   unsigned base = upper_srcs ? 3 : 0;
   unsigned index = upper_srcs ? group->encode_info.upper_src_index
                               : group->encode_info.lower_src_index;
   const rogue_reg_src_info *info = upper_srcs
                                       ? &rogue_reg_upper_src_infos[index]
                                       : &rogue_reg_lower_src_infos[index];
   const rogue_instr_group_io_sel *io_sel = &group->io_sel;

   rogue_mux mux = { 0 };

   if (!upper_srcs && rogue_ref_is_io(&io_sel->iss[0])) {
      switch (io_sel->iss[0].io) {
      case ROGUE_IO_S0:
         mux._ = IS0_S0;
         break;
      case ROGUE_IO_S3:
         mux._ = IS0_S3;
         break;
      case ROGUE_IO_S4:
         mux._ = IS0_S4;
         break;
      case ROGUE_IO_S5:
         mux._ = IS0_S5;
         break;
      case ROGUE_IO_S1:
         mux._ = IS0_S1;
         break;
      case ROGUE_IO_S2:
         mux._ = IS0_S2;
         break;

      default:
         unreachable("IS0 set to unsupported value.");
      }
   }

   rogue_sbA sbA = { 0 };
   rogue_sA sA = { 0 };

   if (!rogue_ref_is_null(&io_sel->srcs[base + 0])) {
      sbA._ = rogue_reg_bank_encoding(
         rogue_ref_get_reg_class(&io_sel->srcs[base + 0]));
      sA._ = rogue_ref_get_reg_index(&io_sel->srcs[base + 0]);
   }

   rogue_sbB sbB = { 0 };
   rogue_sB sB = { 0 };

   if (!rogue_ref_is_null(&io_sel->srcs[base + 1])) {
      sbB._ = rogue_reg_bank_encoding(
         rogue_ref_get_reg_class(&io_sel->srcs[base + 1]));
      sB._ = rogue_ref_get_reg_index(&io_sel->srcs[base + 1]);
   }

   rogue_sbC sbC = { 0 };
   rogue_sC sC = { 0 };

   if (!rogue_ref_is_null(&io_sel->srcs[base + 2])) {
      sbC._ = rogue_reg_bank_encoding(
         rogue_ref_get_reg_class(&io_sel->srcs[base + 2]));
      sC._ = rogue_ref_get_reg_index(&io_sel->srcs[base + 2]);
   }

   /* Byte 0 is common for all encodings. */
   e->sbA_0 = sbA._0;
   e->sA_5_0 = sA._5_0;

   switch (info->num_srcs) {
   case 1:
      switch (info->bytes) {
      case 3:
         /* Byte 1 */
         assert(!upper_srcs || !mux._1_0);

         e->sA_1.mux_1_0 = mux._1_0;
         e->sA_1.sbA_2_1 = sbA._2_1;
         e->sA_1.sA_7_6 = sA._7_6;

         /* Byte 2 */
         e->sA_2.sA_10_8 = sA._10_8;

         e->ext0 = 1;
         FALLTHROUGH;

      case 1:
         break;

      default:
         unreachable("Unsupported source/bytes combination.");
      }
      break;

   case 2:
      e->ext0 = 1;
      e->sel = 1;
      switch (info->bytes) {
      case 4:
         /* Byte 3 */
         assert(!upper_srcs || !mux._2);

         e->sB_3.sA_10_8 = sA._10_8;
         e->sB_3.mux_2 = mux._2;
         e->sB_3.sbA_2 = sbA._2;
         e->sB_3.sA_7 = sA._7;
         e->sB_3.sB_7 = sB._7;

         e->ext2 = 1;
         FALLTHROUGH;

      case 3:
         /* Byte 2 */
         assert(!upper_srcs || !mux._1_0);

         e->mux_1_0 = mux._1_0;
         e->sbA_1 = sbA._1;
         e->sbB_1 = sbB._1;
         e->sA_6 = sA._6;
         e->sB_6_5 = sB._6_5;

         e->ext1 = 1;
         FALLTHROUGH;

      case 2:
         /* Byte 1 */
         e->sbB_0 = sbB._0;
         e->sB_4_0 = sB._4_0;
         break;

      default:
         unreachable("Unsupported source/bytes combination.");
      }
      break;

   case 3:
      e->ext0 = 1;
      e->ext1 = 1;
      switch (info->bytes) {
      case 6:
         /* Byte 5 */
         assert(!upper_srcs || !sC._10_8);

         e->sC_5.sC_10_8 = sC._10_8;
         e->sC_5.sA_10_8 = sA._10_8;

         e->sC_4.ext4 = 1;
         FALLTHROUGH;

      case 5:
         /* Byte 4 */
         assert(!upper_srcs || !mux._2);
         assert(!upper_srcs || !sbC._2);

         e->sC_4.sbC_2 = sbC._2;
         e->sC_4.sC_7_6 = sC._7_6;
         e->sC_4.mux_2 = mux._2;
         e->sC_4.sbA_2 = sbA._2;
         e->sC_4.sA_7 = sA._7;
         e->sC_4.sB_7 = sB._7;

         e->ext2 = 1;
         FALLTHROUGH;

      case 4:
         /* Byte 1 */
         e->sbB_0 = sbB._0;
         e->sB_4_0 = sB._4_0;

         /* Byte 2 */
         assert(!upper_srcs || !mux._1_0);

         e->mux_1_0 = mux._1_0;
         e->sbA_1 = sbA._1;
         e->sbB_1 = sbB._1;
         e->sA_6 = sA._6;
         e->sB_6_5 = sB._6_5;

         /* Byte 3 */
         e->sbC_1_0 = sbC._1_0;
         e->sC_5_0 = sC._5_0;
         break;

      default:
         unreachable("Unsupported source/bytes combination.");
      }
      break;

   default:
      unreachable("Unsupported source/bytes combination.");
   }
}

static void rogue_encode_dest_map(const rogue_instr_group *group,
                                  rogue_dest_map_encoding *e)
{
   const rogue_reg_dst_info *info =
      &rogue_reg_dst_infos[group->encode_info.dst_index];
   const rogue_instr_group_io_sel *io_sel = &group->io_sel;

   unsigned num_dsts = !rogue_ref_is_null(&io_sel->dsts[0]) +
                       !rogue_ref_is_null(&io_sel->dsts[1]);

   switch (num_dsts) {
   case 1: {
      const rogue_ref *dst_ref = !rogue_ref_is_null(&io_sel->dsts[0])
                                    ? &io_sel->dsts[0]
                                    : &io_sel->dsts[1];

      rogue_dbN dbN = { ._ = rogue_reg_bank_encoding(
                           rogue_ref_get_reg_class(dst_ref)) };
      rogue_dN dN = { ._ = rogue_ref_get_reg_index(dst_ref) };

      switch (info->bytes) {
      case 2:
         e->dN_10_8 = dN._10_8;
         e->dbN_2_1 = dbN._2_1;
         e->dN_7_6 = dN._7_6;

         e->ext0 = 1;
         FALLTHROUGH;

      case 1:
         e->dbN_0 = dbN._0;
         e->dN_5_0 = dN._5_0;
         break;

      default:
         unreachable("Unsupported dest/bytes combination.");
      }
      break;
   }
   case 2: {
      rogue_db0 db0 = { ._ = rogue_reg_bank_encoding(
                           rogue_ref_get_reg_class(&io_sel->dsts[0])) };
      rogue_d0 d0 = { ._ = rogue_ref_get_reg_index(&io_sel->dsts[0]) };
      rogue_db1 db1 = { ._ = rogue_reg_bank_encoding(
                           rogue_ref_get_reg_class(&io_sel->dsts[1])) };
      rogue_d1 d1 = { ._ = rogue_ref_get_reg_index(&io_sel->dsts[1]) };

      switch (info->bytes) {
      case 4:
         e->d1_10_8 = d1._10_8;
         e->d0_10_8 = d0._10_8;

         e->ext2 = 1;
         FALLTHROUGH;

      case 3:
         e->db1_2_1 = db1._2_1;
         e->d1_7_6 = d1._7_6;
         e->db0_2_1 = db0._2_1;
         e->d0_7 = d0._7;

         e->ext1 = 1;
         FALLTHROUGH;

      case 2:
         e->db0_0 = db0._0;
         e->d0_6_0 = d0._6_0;

         e->db1_0 = db1._0;
         e->d1_5_0 = d1._5_0;
         break;

      default:
         unreachable("Unsupported dest/bytes combination.");
      }
   } break;

   default:
      unreachable("Unsupported dest/bytes combination.");
   }
}

static void rogue_encode_iss_map(const rogue_instr_group *group,
                                 rogue_iss_encoding *e)
{
   const rogue_instr_group_io_sel *io_sel = &group->io_sel;

   if (rogue_ref_is_io(&io_sel->iss[1]))
      switch (rogue_ref_get_io(&io_sel->iss[1])) {
      case ROGUE_IO_FT0:
         e->is1 = IS1_FT0;
         break;
      case ROGUE_IO_FTE:
         e->is1 = IS1_FTE;
         break;

      default:
         unreachable("Unsupported setting for IS1.");
      }

   if (rogue_ref_is_io(&io_sel->iss[2]))
      switch (rogue_ref_get_io(&io_sel->iss[2])) {
      case ROGUE_IO_FT1:
         e->is2 = IS2_FT1;
         break;
      case ROGUE_IO_FTE:
         e->is2 = IS2_FTE;
         break;

      default:
         unreachable("Unsupported setting for IS2.");
      }

   if (rogue_ref_is_io(&io_sel->iss[3]))
      switch (rogue_ref_get_io(&io_sel->iss[3])) {
      case ROGUE_IO_FT0:
         e->is3 = IS3_FT0;
         break;
      case ROGUE_IO_FT1:
         e->is3 = IS3_FT1;
         break;
      case ROGUE_IO_S2:
         e->is3 = IS3_S2;
         break;
      case ROGUE_IO_FTE:
         e->is3 = IS3_FTE;
         break;

      default:
         unreachable("Unsupported setting for IS3.");
      }

   if (rogue_ref_is_io(&io_sel->iss[4]))
      switch (rogue_ref_get_io(&io_sel->iss[4])) {
      case ROGUE_IO_FT0:
         e->is4 = IS4_FT0;
         break;
      case ROGUE_IO_FT1:
         e->is4 = IS4_FT1;
         break;
      case ROGUE_IO_FT2:
         e->is4 = IS4_FT2;
         break;
      case ROGUE_IO_FTE:
         e->is4 = IS4_FTE;
         break;

      default:
         unreachable("Unsupported setting for IS4.");
      }

   if (rogue_ref_is_io(&io_sel->iss[5]))
      switch (rogue_ref_get_io(&io_sel->iss[5])) {
      case ROGUE_IO_FT0:
         e->is5 = IS5_FT0;
         break;
      case ROGUE_IO_FT1:
         e->is5 = IS5_FT1;
         break;
      case ROGUE_IO_FT2:
         e->is5 = IS5_FT2;
         break;
      case ROGUE_IO_FTE:
         e->is5 = IS5_FTE;
         break;

      default:
         unreachable("Unsupported setting for IS5.");
      }
}

static void rogue_encode_instr_group_io(const rogue_instr_group *group,
                                        struct util_dynarray *binary)
{
   if (group->size.lower_srcs) {
      rogue_source_map_encoding lower_srcs = { 0 };
      rogue_encode_source_map(group, false, &lower_srcs);
      util_dynarray_append_mem(binary, group->size.lower_srcs, &lower_srcs);
   }

   if (group->size.upper_srcs) {
      rogue_source_map_encoding upper_srcs = { 0 };
      rogue_encode_source_map(group, true, &upper_srcs);
      util_dynarray_append_mem(binary, group->size.upper_srcs, &upper_srcs);
   }

   if (group->size.iss) {
      rogue_iss_encoding internal_src_sel = { 0 };
      rogue_encode_iss_map(group, &internal_src_sel);
      util_dynarray_append_mem(binary, group->size.iss, &internal_src_sel);
   }

   if (group->size.dsts) {
      rogue_dest_map_encoding dests = { 0 };
      rogue_encode_dest_map(group, &dests);
      util_dynarray_append_mem(binary, group->size.dsts, &dests);
   }
}

static void rogue_encode_instr_group_padding(const rogue_instr_group *group,
                                             struct util_dynarray *binary)
{
   if (group->size.word_padding)
      util_dynarray_append(binary, uint8_t, 0xff);

   if (group->size.align_padding) {
      assert(!(group->size.align_padding % 2));
      unsigned align_words = group->size.align_padding / 2;
      util_dynarray_append(binary, uint8_t, 0xf0 | align_words);
      for (unsigned u = 0; u < group->size.align_padding - 1; ++u)
         util_dynarray_append(binary, uint8_t, 0xff);
   }
}

static void rogue_encode_instr_group(rogue_instr_group *group,
                                     struct util_dynarray *binary)
{
   rogue_encode_instr_group_header(group, binary);
   rogue_encode_instr_group_instrs(group, binary);
   rogue_encode_instr_group_io(group, binary);
   rogue_encode_instr_group_padding(group, binary);
}

PUBLIC
void rogue_encode_shader(UNUSED rogue_build_ctx *ctx,
                         rogue_shader *shader,
                         struct util_dynarray *binary)
{
   if (!shader->is_grouped)
      unreachable("Can't encode shader with ungrouped instructions.");

   util_dynarray_init(binary, shader);

   rogue_foreach_instr_group_in_shader (group, shader)
      rogue_encode_instr_group(group, binary);
}
