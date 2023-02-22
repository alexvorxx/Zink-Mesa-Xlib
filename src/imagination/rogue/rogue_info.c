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

/**
 * \file rogue_info.c
 *
 * \brief Contains information and definitions for defined types and structures.
 */

/* TODO: Adjust according to core configurations. */
/* TODO: Remaining restrictions, e.g. some registers are only
 * usable by a particular instruction (vertex output) etc. */
#define S(n) BITFIELD64_BIT(ROGUE_IO_S##n - 1)
const rogue_reg_info rogue_reg_infos[ROGUE_REG_CLASS_COUNT] = {
   [ROGUE_REG_CLASS_INVALID] = { .name = "!INVALID!", .str = "!INVALID!", },
   [ROGUE_REG_CLASS_SSA] = { .name = "ssa", .str = "R", },
   [ROGUE_REG_CLASS_TEMP] = { .name = "temp", .str = "r", .num = 248, },
   [ROGUE_REG_CLASS_COEFF] = { .name = "coeff", .str = "cf", .num = 4096, .supported_io_srcs = S(0) | S(2) | S(3), },
   [ROGUE_REG_CLASS_SHARED] = { .name = "shared", .str = "sh", .num = 4096, .supported_io_srcs = S(0) | S(2) | S(3), },
   [ROGUE_REG_CLASS_SPECIAL] = { .name = "special", .str = "sr", .num = 240, }, /* TODO NEXT: Only S1, S2, S4. */
   [ROGUE_REG_CLASS_INTERNAL] = { .name = "internal", .str = "i", .num = 8, },
   [ROGUE_REG_CLASS_CONST] = { .name = "const", .str = "sc", .num = 240, },
   [ROGUE_REG_CLASS_PIXOUT] = { .name = "pixout", .str = "po", .num = 8, .supported_io_srcs = S(0) | S(2) | S(3), },
   [ROGUE_REG_CLASS_VTXIN] = { .name = "vtxin", .str = "vi", .num = 248, },
   [ROGUE_REG_CLASS_VTXOUT] = { .name = "vtxout", .str = "vo", .num = 256, },
};
#undef S

const rogue_regalloc_info regalloc_info[ROGUE_REGALLOC_CLASS_COUNT] = {
   [ROGUE_REGALLOC_CLASS_TEMP_1] = { .class = ROGUE_REG_CLASS_TEMP, .stride = 1, },
   [ROGUE_REGALLOC_CLASS_TEMP_2] = { .class = ROGUE_REG_CLASS_TEMP, .stride = 2, },
   [ROGUE_REGALLOC_CLASS_TEMP_4] = { .class = ROGUE_REG_CLASS_TEMP, .stride = 4, },
};

const rogue_reg_dst_info rogue_reg_dst_infos[ROGUE_REG_DST_VARIANTS] = {
   {
      .num_dsts = 1,
      .bank_bits = { 1 },
      .index_bits = { 6 },
      .bytes = 1,
   },
   {
      .num_dsts = 1,
      .bank_bits = { 3 },
      .index_bits = { 11 },
      .bytes = 2,
   },
   {
      .num_dsts = 2,
      .bank_bits = { 1, 1 },
      .index_bits = { 7, 6 },
      .bytes = 2,
   },
   {
      .num_dsts = 2,
      .bank_bits = { 3, 3 },
      .index_bits = { 8, 8 },
      .bytes = 3,
   },
   {
      .num_dsts = 2,
      .bank_bits = { 3, 3 },
      .index_bits = { 11, 11 },
      .bytes = 4,
   },
};

const rogue_reg_src_info rogue_reg_lower_src_infos[ROGUE_REG_SRC_VARIANTS] = {
   {
      .num_srcs = 1,
      .mux_bits = 0,
      .bank_bits = { 1 },
      .index_bits = { 6 },
      .bytes = 1,
   },
   {
      .num_srcs = 1,
      .mux_bits = 2,
      .bank_bits = { 3 },
      .index_bits = { 11 },
      .bytes = 3,
   },
   {
      .num_srcs = 2,
      .mux_bits = 0,
      .bank_bits = { 1, 1 },
      .index_bits = { 6, 5 },
      .bytes = 2,
   },
   {
      .num_srcs = 2,
      .mux_bits = 2,
      .bank_bits = { 2, 2 },
      .index_bits = { 7, 7 },
      .bytes = 3,
   },
   {
      .num_srcs = 2,
      .mux_bits = 3,
      .bank_bits = { 3, 2 },
      .index_bits = { 11, 8 },
      .bytes = 4,
   },
   {
      .num_srcs = 3,
      .mux_bits = 2,
      .bank_bits = { 2, 2, 2 },
      .index_bits = { 7, 7, 6 },
      .bytes = 4,
   },
   {
      .num_srcs = 3,
      .mux_bits = 3,
      .bank_bits = { 3, 2, 3 },
      .index_bits = { 8, 8, 8 },
      .bytes = 5,
   },
   {
      .num_srcs = 3,
      .mux_bits = 3,
      .bank_bits = { 3, 2, 3 },
      .index_bits = { 11, 8, 11 },
      .bytes = 6,
   },
};

const rogue_reg_src_info rogue_reg_upper_src_infos[ROGUE_REG_SRC_VARIANTS] = {
   {
      .num_srcs = 1,
      .bank_bits = { 1 },
      .index_bits = { 6 },
      .bytes = 1,
   },
   {
      .num_srcs = 1,
      .bank_bits = { 3 },
      .index_bits = { 11 },
      .bytes = 3,
   },
   {
      .num_srcs = 2,
      .bank_bits = { 1, 1 },
      .index_bits = { 6, 5 },
      .bytes = 2,
   },
   {
      .num_srcs = 2,
      .bank_bits = { 2, 2 },
      .index_bits = { 7, 7 },
      .bytes = 3,
   },
   {
      .num_srcs = 2,
      .bank_bits = { 3, 2 },
      .index_bits = { 11, 8 },
      .bytes = 4,
   },
   {
      .num_srcs = 3,
      .bank_bits = { 2, 2, 2 },
      .index_bits = { 7, 7, 6 },
      .bytes = 4,
   },
   {
      .num_srcs = 3,
      .bank_bits = { 3, 2, 2 },
      .index_bits = { 8, 8, 8 },
      .bytes = 5,
   },
   {
      .num_srcs = 3,
      .bank_bits = { 3, 2, 2 },
      .index_bits = { 11, 8, 8 },
      .bytes = 6,
   },
};

const rogue_alu_op_mod_info rogue_alu_op_mod_infos[ROGUE_ALU_OP_MOD_COUNT] = {
	[ROGUE_ALU_OP_MOD_LP] = { .str = "lp", },
	[ROGUE_ALU_OP_MOD_SAT] = { .str = "sat", },
	[ROGUE_ALU_OP_MOD_SCALE] = { .str = "scale", },
	[ROGUE_ALU_OP_MOD_ROUNDZERO] = { .str = "roundzero", },
};

const rogue_alu_dst_mod_info rogue_alu_dst_mod_infos[ROGUE_ALU_DST_MOD_COUNT] = {
	[ROGUE_ALU_DST_MOD_E0] = { .str = "e0", },
	[ROGUE_ALU_DST_MOD_E1] = { .str = "e1", },
	[ROGUE_ALU_DST_MOD_E2] = { .str = "e2", },
	[ROGUE_ALU_DST_MOD_E3] = { .str = "e3", },
};

const rogue_alu_src_mod_info rogue_alu_src_mod_infos[ROGUE_ALU_SRC_MOD_COUNT] = {
	[ROGUE_ALU_SRC_MOD_FLR] = { .str = "flr", },
	[ROGUE_ALU_SRC_MOD_ABS] = { .str = "abs", },
	[ROGUE_ALU_SRC_MOD_NEG] = { .str = "neg", },
};

const rogue_ctrl_op_mod_info rogue_ctrl_op_mod_infos[ROGUE_CTRL_OP_MOD_COUNT] = {
	[ROGUE_CTRL_OP_MOD_END] = { .str = "end", },
};

#define OM(op_mod) BITFIELD64_BIT(ROGUE_CTRL_OP_MOD_##op_mod)
#define T(type) BITFIELD64_BIT(ROGUE_REF_TYPE_##type - 1)
const rogue_ctrl_op_info rogue_ctrl_op_infos[ROGUE_CTRL_OP_COUNT] = {
	[ROGUE_CTRL_OP_INVALID] = { .str = "!INVALID!", },
	[ROGUE_CTRL_OP_END] = { .str = "end", .ends_block = true, },
	[ROGUE_CTRL_OP_NOP] = { .str = "nop",
		.supported_op_mods = OM(END),
	},
	[ROGUE_CTRL_OP_BA] = { .str = "ba", .has_target = true, .ends_block = true, },
	[ROGUE_CTRL_OP_WDF] = { .str = "wdf", .num_srcs = 1,
      .supported_src_types = { [0] = T(DRC), },
   },
};
#undef T
#undef OM

#define IO(io) ROGUE_IO_##io
#define OM(op_mod) BITFIELD64_BIT(ROGUE_BACKEND_OP_MOD_##op_mod)
#define T(type) BITFIELD64_BIT(ROGUE_REF_TYPE_##type - 1)
#define B(n) BITFIELD64_BIT(n)
const rogue_backend_op_info rogue_backend_op_infos[ROGUE_BACKEND_OP_COUNT] = {
	[ROGUE_BACKEND_OP_INVALID] = { .str = "!INVALID!", },
   [ROGUE_BACKEND_OP_UVSW_WRITE] = { .str = "uvsw.write", .num_dsts = 1, .num_srcs = 1,
      .phase_io = { .src[0] = IO(W0), },
      .supported_dst_types = { [0] = T(REG), },
      .supported_src_types = { [0] = T(REG), },
   },
   [ROGUE_BACKEND_OP_UVSW_EMIT] = { .str = "uvsw.emit", },
   [ROGUE_BACKEND_OP_UVSW_ENDTASK] = { .str = "uvsw.endtask", },

   [ROGUE_BACKEND_OP_UVSW_EMITTHENENDTASK] = { .str = "uvsw.emitthenendtask", },
   [ROGUE_BACKEND_OP_UVSW_WRITETHENEMITTHENENDTASK] = { .str = "uvsw.writethenemitthenendtask", .num_dsts = 1, .num_srcs = 1,
      .phase_io = { .src[0] = IO(W0), },
      .supported_dst_types = { [0] = T(REG), },
      .supported_src_types = { [0] = T(REG), },
   },
   /* .src[1] and .src[2] can actually be S0-5. */
   [ROGUE_BACKEND_OP_LD] = { .str = "ld", .num_dsts = 1, .num_srcs = 3,
      .phase_io = { .dst[0] = IO(S3), .src[2] = IO(S0), },
      .supported_dst_types = { [0] = T(REG) | T(REGARRAY), },
      .supported_src_types = {
         [0] = T(DRC),
         [1] = T(VAL),
         [2] = T(REGARRAY),
      },
      .dst_stride = {
         [0] = ~0U,
      },
      .src_stride = {
         [2] = 1,
      },
   },
	[ROGUE_BACKEND_OP_FITRP_PIXEL] = { .str = "fitrp.pixel", .num_dsts = 1, .num_srcs = 4,
      .phase_io = { .dst[0] = IO(S3), .src[1] = IO(S0), .src[2] = IO(S2), },
      .supported_op_mods = OM(SAT),
      .supported_dst_types = { [0] = T(REG), },
      .supported_src_types = {
         [0] = T(DRC),
         [1] = T(REGARRAY),
         [2] = T(REGARRAY),
         [3] = T(VAL),
      },
      .src_stride = {
         [1] = 3,
         [2] = ~0U,
      },
   },
};
#undef B
#undef T
#undef OM
#undef IO

const rogue_backend_op_mod_info rogue_backend_op_mod_infos[ROGUE_BACKEND_OP_MOD_COUNT] = {
	[ROGUE_BACKEND_OP_MOD_SAT] = { .str = "sat", },
};

#define P(type) BITFIELD64_BIT(ROGUE_INSTR_PHASE_##type)
#define PH(type) ROGUE_INSTR_PHASE_##type
#define IO(io) ROGUE_IO_##io
#define T(type) BITFIELD64_BIT(ROGUE_REF_TYPE_##type - 1)
const rogue_bitwise_op_info rogue_bitwise_op_infos[ROGUE_BITWISE_OP_COUNT] = {
   [ROGUE_BITWISE_OP_INVALID] = { .str = "", },
   [ROGUE_BITWISE_OP_BYP0] = { .str = "byp", .num_dsts = 2, .num_srcs = 2,
      .supported_phases = P(0_BITMASK),
      .phase_io[PH(0_BITMASK)] = { .dst[1] = IO(FT1), },
      .supported_dst_types = {
         [0] = T(REG) | T(REGARRAY) | T(IO),
         [1] = T(REG) | T(REGARRAY) | T(IO),
      },
      .supported_src_types = {
         [0] = T(REG) | T(REGARRAY) | T(IO),
         [1] = T(REG) | T(REGARRAY) | T(IO) | T(VAL),
      },
   },
};
#undef T
#undef IO
#undef PH
#undef P

const rogue_io_info rogue_io_infos[ROGUE_IO_COUNT] = {
	[ROGUE_IO_INVALID] = { .str = "!INVALID!", },
	[ROGUE_IO_S0] = { .str = "s0", },
	[ROGUE_IO_S1] = { .str = "s1", },
	[ROGUE_IO_S2] = { .str = "s2", },
	[ROGUE_IO_S3] = { .str = "s3", },
	[ROGUE_IO_S4] = { .str = "s4", },
	[ROGUE_IO_S5] = { .str = "s5", },
	[ROGUE_IO_W0] = { .str = "w0", },
	[ROGUE_IO_W1] = { .str = "w1", },
	[ROGUE_IO_IS0] = { .str = "is0", },
	[ROGUE_IO_IS1] = { .str = "is1", },
	[ROGUE_IO_IS2] = { .str = "is2", },
	[ROGUE_IO_IS3] = { .str = "is3", },
	[ROGUE_IO_IS4] = { .str = "is4/w0", },
	[ROGUE_IO_IS5] = { .str = "is5/w1", },
	[ROGUE_IO_FT0] = { .str = "ft0", },
	[ROGUE_IO_FT1] = { .str = "ft1", },
	[ROGUE_IO_FT2] = { .str = "ft2", },
	[ROGUE_IO_FTE] = { .str = "fte", },
	[ROGUE_IO_FT3] = { .str = "ft3", },
	[ROGUE_IO_FT4] = { .str = "ft4", },
	[ROGUE_IO_FT5] = { .str = "ft5", },
	[ROGUE_IO_P0] = { .str = "p0", },
	[ROGUE_IO_NONE] = { .str = "_", },
};

#define SM(src_mod) BITFIELD64_BIT(ROGUE_ALU_SRC_MOD_##src_mod)
#define DM(dst_mod) BITFIELD64_BIT(ROGUE_ALU_DST_MOD_##dst_mod)
#define OM(op_mod) BITFIELD64_BIT(ROGUE_ALU_OP_MOD_##op_mod)
#define P(type) BITFIELD64_BIT(ROGUE_INSTR_PHASE_##type)
#define PH(type) ROGUE_INSTR_PHASE_##type
#define IO(io) ROGUE_IO_##io
#define T(type) BITFIELD64_BIT(ROGUE_REF_TYPE_##type - 1)
#define B(n) BITFIELD64_BIT(n)
const rogue_alu_op_info rogue_alu_op_infos[ROGUE_ALU_OP_COUNT] = {
   [ROGUE_ALU_OP_INVALID] = { .str = "!INVALID!", },
   [ROGUE_ALU_OP_MBYP] = { .str = "mbyp", .num_dsts = 1, .num_srcs = 1,
      .supported_phases = P(0),
      .phase_io[PH(0)] = { .dst[0] = IO(FT0), .src[0] = IO(S0), },
      .supported_src_mods = {
         [0] = SM(ABS) | SM(NEG),
      },
      .supported_dst_types = { [0] = T(REG) | T(REGARRAY), },
      .supported_src_types = {
         [0] = T(REG),
      },
   },
   [ROGUE_ALU_OP_FADD] = { .str = "fadd", .num_dsts = 1, .num_srcs = 2,
      .supported_phases = P(0),
      .phase_io[PH(0)] = { .dst[0] = IO(FT0), .src[0] = IO(S0), .src[1] = IO(S1), },
      .supported_op_mods = OM(LP) | OM(SAT),
      .supported_src_mods = {
         [0] = SM(FLR) | SM(ABS) | SM(NEG),
         [1] = SM(ABS),
      },
   },
   [ROGUE_ALU_OP_FMUL] = { .str = "fmul", .num_dsts = 1, .num_srcs = 2,
      .supported_phases = P(0),
      .phase_io[PH(0)] = { .dst[0] = IO(FT0), .src[0] = IO(S0), .src[1] = IO(S1), },
      .supported_op_mods = OM(LP) | OM(SAT),
      .supported_src_mods = {
         [0] = SM(FLR) | SM(ABS) | SM(NEG),
         [1] = SM(ABS),
      },
      .supported_dst_types = { [0] = T(REG), },
      .supported_src_types = {
         [0] = T(REG),
         [1] = T(REG),
      },
   },
   [ROGUE_ALU_OP_FMAD] = { .str = "fmad", .num_dsts = 1, .num_srcs = 3,
      .supported_phases = P(0),
      .phase_io[PH(0)] = { .dst[0] = IO(FT0), .src[0] = IO(S0), .src[1] = IO(S1), .src[2] = IO(S2), },
      .supported_op_mods = OM(LP) | OM(SAT),
      .supported_src_mods = {
         [0] = SM(ABS) | SM(NEG),
         [1] = SM(ABS) | SM(NEG),
         [2] = SM(FLR) | SM(ABS) | SM(NEG),
      },
      .supported_dst_types = { [0] = T(REG), },
      .supported_src_types = {
         [0] = T(REG),
         [1] = T(REG),
         [2] = T(REG),
      },
   },
   /* TODO: Implement */
   [ROGUE_ALU_OP_TST] = { .str = "tst", .num_dsts = 2, .num_srcs = 2, },
   [ROGUE_ALU_OP_ADD64] = { .str = "add64", .num_dsts = 3, .num_srcs = 5,
      .supported_phases = P(0),
      .phase_io[PH(0)] = { .dst[0] = IO(FT0), .dst[1] = IO(FTE), .src[0] = IO(S0), .src[1] = IO(S1), .src[2] = IO(S2), .src[3] = IO(IS0), },
      .supported_src_mods = {
         [0] = SM(ABS) | SM(NEG),
         [1] = SM(ABS) | SM(NEG),
         [2] = SM(ABS) | SM(NEG),
         [3] = SM(ABS) | SM(NEG),
      },
      .supported_dst_types = { [0] = T(REG) | T(REGARRAY), [1] = T(REG) | T(REGARRAY) | T(IO), [2] = T(IO) },
      .supported_src_types = {
         [0] = T(REG) | T(REGARRAY),
         [1] = T(REG) | T(REGARRAY),
         [2] = T(REG) | T(REGARRAY) | T(IMM),
         [3] = T(REG) | T(REGARRAY)| T(IO) | T(IMM),
         [4] = T(IO),
      },
   },
   [ROGUE_ALU_OP_PCK_U8888] = { .str = "pck.u8888", .num_dsts = 1, .num_srcs = 1,
      .supported_phases = P(2_PCK),
      .phase_io[PH(2_PCK)] = { .dst[0] = IO(FT2), .src[0] = IO(IS3), },
      .supported_op_mods = OM(SCALE) | OM(ROUNDZERO),
      .supported_dst_types = { [0] = T(REG), },
      .supported_src_types = {
         [0] = T(REGARRAY),
      },
      .src_repeat_mask = B(0),
   },
   /* This mov is "fake" since it can be lowered to a MBYP, make a new instruction for real mov (call it MOVD?). */
   [ROGUE_ALU_OP_MOV] = { .str = "mov", .num_dsts = 1, .num_srcs = 1,
      .supported_dst_types = { [0] = T(REG) | T(REGARRAY), },
      .supported_src_types = {
         [0] = T(REG) | T(IMM),
      },
   },
   [ROGUE_ALU_OP_FABS] = { .str = "fabs", .num_dsts = 1, .num_srcs = 1, },
   [ROGUE_ALU_OP_FNEG] = { .str = "fneg", .num_dsts = 1, .num_srcs = 1, },
   [ROGUE_ALU_OP_FNABS] = { .str = "fnabs", .num_dsts = 1, .num_srcs = 1, },

   [ROGUE_ALU_OP_FMAX] = { .str = "fmax", .num_dsts = 1, .num_srcs = 2, }, /* TODO */
   [ROGUE_ALU_OP_FMIN] = { .str = "fmin", .num_dsts = 1, .num_srcs = 2, }, /* TODO */
};
#undef B
#undef T
#undef IO
#undef PH
#undef P
#undef OM
#undef DM
#undef SM

const char *const rogue_comp_test_str[ROGUE_COMP_TEST_COUNT] = {
   [ROGUE_COMP_TEST_NONE] = "!INVALID!", [ROGUE_COMP_TEST_EQ] = "eq",
   [ROGUE_COMP_TEST_GT] = "gt",          [ROGUE_COMP_TEST_GE] = "ge",
   [ROGUE_COMP_TEST_NE] = "ne",          [ROGUE_COMP_TEST_LT] = "lt",
   [ROGUE_COMP_TEST_LE] = "le",
};

const char *const rogue_comp_type_str[ROGUE_COMP_TYPE_COUNT] = {
   [ROGUE_COMP_TYPE_NONE] = "!INVALID!", [ROGUE_COMP_TYPE_F32] = "f32",
   [ROGUE_COMP_TYPE_U16] = "u16",        [ROGUE_COMP_TYPE_S16] = "s16",
   [ROGUE_COMP_TYPE_U8] = "u8",          [ROGUE_COMP_TYPE_S8] = "s8",
   [ROGUE_COMP_TYPE_U32] = "u32",        [ROGUE_COMP_TYPE_S32] = "s32",
};

const char *rogue_instr_type_str[ROGUE_INSTR_TYPE_COUNT] = {
   [ROGUE_INSTR_TYPE_INVALID] = "!INVALID!",

   [ROGUE_INSTR_TYPE_ALU] = "alu",
   /* [ROGUE_INSTR_TYPE_CMPLX] = "cmplx", */
   [ROGUE_INSTR_TYPE_BACKEND] = "backend",
   [ROGUE_INSTR_TYPE_CTRL] = "ctrl",
   [ROGUE_INSTR_TYPE_BITWISE] = "bitwise",
   /* [ROGUE_INSTR_TYPE_F16SOP] = "f16sop", */
};

const char *const rogue_alu_str[ROGUE_ALU_COUNT] = {
   [ROGUE_ALU_INVALID] = "!INVALID!",
   [ROGUE_ALU_MAIN] = "main",
   [ROGUE_ALU_BITWISE] = "bitwise",
   [ROGUE_ALU_CONTROL] = "control",
};

const char *const rogue_instr_phase_str[ROGUE_ALU_COUNT][ROGUE_INSTR_PHASE_COUNT] = {
   /** Main/ALU (and backend) instructions. */
   [ROGUE_ALU_MAIN] = {
      [ROGUE_INSTR_PHASE_0] = "p0",
      [ROGUE_INSTR_PHASE_1] = "p1",
      [ROGUE_INSTR_PHASE_2_PCK] = "p2pck",
      [ROGUE_INSTR_PHASE_2_TST] = "p2tst",
      [ROGUE_INSTR_PHASE_2_MOV] = "p2mov",
      [ROGUE_INSTR_PHASE_BACKEND] = "backend",
   },

   /** Bitwise instructions. */
   [ROGUE_ALU_BITWISE] = {
      [ROGUE_INSTR_PHASE_0_BITMASK] = "p0bm",
      [ROGUE_INSTR_PHASE_0_SHIFT1] = "p0shf1",
      [ROGUE_INSTR_PHASE_0_COUNT] = "p0cnt",
      [ROGUE_INSTR_PHASE_1_LOGICAL] = "p1log",
      [ROGUE_INSTR_PHASE_2_SHIFT2] = "p2shf2",
      [ROGUE_INSTR_PHASE_2_TEST] = "p2tst",
   },

   /** Control instructions (no co-issuing). */
   [ROGUE_ALU_CONTROL] = {
      [ROGUE_INSTR_PHASE_CTRL] = "ctrl",
   },
};
