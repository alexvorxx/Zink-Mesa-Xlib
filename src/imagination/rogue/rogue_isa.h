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
 * AUTHORS OR COPYRIGHT HOLDERS LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ROGUE_ISA_H
#define ROGUE_ISA_H

/**
 * \file rogue_isa.h
 *
 * \brief Contains hardware ISA definitions and encodings.
 */

/* Source/destination encodings. */

/* Internal source selector. */
typedef struct rogue_iss_encoding {
   unsigned is1 : 1;
   unsigned is2 : 1;
   unsigned is3 : 2;
   unsigned is4 : 2;
   unsigned is5 : 2;
} PACKED rogue_iss_encoding;
static_assert(sizeof(rogue_iss_encoding) == 1,
              "sizeof(rogue_iss_encoding) != 1");

typedef struct rogue_dbN {
   union {
      struct {
         unsigned _0 : 1;
         unsigned _2_1 : 2;
         unsigned : 5;
      } PACKED;

      uint8_t _;
   } PACKED;
} PACKED rogue_dbN;
static_assert(sizeof(rogue_dbN) == 1, "sizeof(rogue_dbN) != 1");

typedef struct rogue_dN {
   union {
      struct {
         unsigned _5_0 : 6;
         unsigned _7_6 : 2;
         unsigned _10_8 : 3;
         unsigned : 5;
      } PACKED;

      uint8_t _;
   } PACKED;
} PACKED rogue_dN;
static_assert(sizeof(rogue_dN) == 2, "sizeof(rogue_dN) != 2");

typedef struct rogue_db0 {
   union {
      struct {
         unsigned _0 : 1;
         unsigned _2_1 : 2;
         unsigned : 5;
      } PACKED;

      uint8_t _;
   } PACKED;
} PACKED rogue_db0;
static_assert(sizeof(rogue_db0) == 1, "sizeof(rogue_db0) != 1");

typedef struct rogue_db1 {
   union {
      struct {
         unsigned _0 : 1;
         unsigned _2_1 : 2;
         unsigned : 5;
      } PACKED;

      uint8_t _;
   } PACKED;
} PACKED rogue_db1;
static_assert(sizeof(rogue_db0) == 1, "sizeof(rogue_db0) != 1");

typedef struct rogue_d0 {
   union {
      struct {
         unsigned _6_0 : 7;
         unsigned _7 : 1;
         unsigned _10_8 : 3;
         unsigned : 5;
      } PACKED;

      uint16_t _;
   } PACKED;
} PACKED rogue_d0;
static_assert(sizeof(rogue_d0) == 2, "sizeof(rogue_d0) != 2");

typedef struct rogue_d1 {
   union {
      struct {
         unsigned _5_0 : 6;
         unsigned _7_6 : 2;
         unsigned _10_8 : 3;
         unsigned : 5;
      } PACKED;

      uint16_t _;
   } PACKED;
} PACKED rogue_d1;
static_assert(sizeof(rogue_d1) == 2, "sizeof(rogue_d1) != 2");

typedef struct rogue_dest_map_encoding {
   /* Byte 0 */
   union {
      struct {
         unsigned dN_5_0 : 6;
         unsigned dbN_0 : 1;
         unsigned ext0 : 1;
      } PACKED;

      struct {
         unsigned d0_6_0 : 7;
         unsigned db0_0 : 1;
      } PACKED;
   };

   /* Byte 1 */
   union {
      struct {
         unsigned dN_7_6 : 2;
         unsigned dbN_2_1 : 2;
         unsigned dN_10_8 : 3;
         unsigned rsvd1 : 1;
      } PACKED;

      struct {
         unsigned d1_5_0 : 6;
         unsigned db1_0 : 1;
         unsigned ext1 : 1;
      } PACKED;
   };

   /* Byte 2 */
   struct {
      unsigned d0_7 : 1;
      unsigned db0_2_1 : 2;
      unsigned d1_7_6 : 2;
      unsigned db1_2_1 : 2;
      unsigned ext2 : 1;
   } PACKED;

   /* Byte 3 */
   struct {
      unsigned d0_10_8 : 3;
      unsigned d1_10_8 : 3;
      unsigned rsvd3 : 2;
   } PACKED;
} PACKED rogue_dest_map_encoding;
static_assert(sizeof(rogue_dest_map_encoding) == 4,
              "sizeof(rogue_dest_map_encoding) != 4");

/****************/

typedef struct rogue_source_map_encoding {
   /* Byte 0 */
   struct {
      unsigned sA_5_0 : 6;
      unsigned sbA_0 : 1;
      unsigned ext0 : 1;
   } PACKED;

   /* Byte 1 */
   union {
      struct {
         unsigned sA_7_6 : 2;
         unsigned sbA_2_1 : 2;
         unsigned mux_1_0 : 2;
         unsigned : 2;
      } PACKED sA_1;

      struct {
         unsigned sB_4_0 : 5;
         unsigned sbB_0 : 1;
         unsigned ext1 : 1;
         unsigned sel : 1;
      } PACKED;
   } PACKED;

   /* Byte 2 */
   union {
      /* Common def. */
      struct sA_sC_10_8 {
         unsigned sA_10_8 : 3;
         unsigned sC_10_8 : 3;
         unsigned rsvd5 : 2;
      } PACKED sA_2;

      struct {
         unsigned sB_6_5 : 2;
         unsigned sA_6 : 1;
         unsigned sbB_1 : 1;
         unsigned sbA_1 : 1;
         unsigned mux_1_0 : 2;
         unsigned ext2 : 1;
      } PACKED;

      struct {
         unsigned : 3;
         unsigned rsvd2 : 5;
      } PACKED;
   } PACKED;

   /* Byte 3 */
   union {
      /* Common def. */
      struct sB_sC_mux2 {
         union {
            struct {
               unsigned sB_7 : 1;
               unsigned sA_7 : 1;
               unsigned rsvd3 : 1;
               unsigned sbA_2 : 1;
               unsigned mux_2 : 1;
               unsigned sA_10_8 : 3;
            } PACKED;

            struct {
               unsigned : 2;
               unsigned ext4 : 1;
               unsigned : 2;
               unsigned sC_7_6 : 2;
               unsigned sbC_2 : 1;
            } PACKED;
         } PACKED;
      } PACKED sB_3;

      struct {
         unsigned sC_5_0 : 6;
         unsigned sbC_1_0 : 2;
      } PACKED;
   } PACKED;

   /* Byte 4 */
   struct sB_sC_mux2 sC_4;

   /* Byte 5 */
   struct sA_sC_10_8 sC_5;
} PACKED rogue_source_map_encoding;
static_assert(sizeof(rogue_source_map_encoding) == 6,
              "sizeof(rogue_source_map_encoding) == 6");

typedef struct rogue_sbA {
   union {
      struct {
         unsigned _0 : 1;
         unsigned _2_1 : 2;
         unsigned : 5;
      } PACKED;

      struct {
         unsigned : 1;
         unsigned _1 : 1;
         unsigned _2 : 1;
         unsigned : 5;
      } PACKED;

      uint8_t _;
   } PACKED;
} PACKED rogue_sbA;
static_assert(sizeof(rogue_sbA) == 1, "sizeof(rogue_sbA) != 1");

typedef struct rogue_sA {
   union {
      struct {
         unsigned _5_0 : 6;
         unsigned _7_6 : 2;
         unsigned _10_8 : 3;
         unsigned : 5;
      } PACKED;

      struct {
         unsigned : 6;
         unsigned _6 : 1;
         unsigned _7 : 1;
         unsigned : 8;
      } PACKED;

      uint16_t _;
   } PACKED;
} PACKED rogue_sA;
static_assert(sizeof(rogue_sA) == 2, "sizeof(rogue_sA) != 2");

typedef struct rogue_sbB {
   union {
      struct {
         unsigned _0 : 1;
         unsigned _1 : 1;
         unsigned : 6;
      } PACKED;

      uint8_t _;
   } PACKED;
} PACKED rogue_sbB;
static_assert(sizeof(rogue_sbB) == 1, "sizeof(rogue_sbB) != 1");

typedef struct rogue_sB {
   union {
      struct {
         unsigned _4_0 : 5;
         unsigned _6_5 : 2;
         unsigned _7 : 1;
         unsigned : 8;
      } PACKED;

      uint16_t _;
   } PACKED;
} PACKED rogue_sB;
static_assert(sizeof(rogue_sB) == 2, "sizeof(rogue_sB) != 2");

typedef struct rogue_sbC {
   union {
      struct {
         unsigned _1_0 : 2;
         unsigned _2 : 1;
         unsigned : 5;
      } PACKED;

      uint8_t _;
   } PACKED;
} PACKED rogue_sbC;
static_assert(sizeof(rogue_sbC) == 1, "sizeof(rogue_sbC) != 1");

typedef struct rogue_sC {
   union {
      struct {
         unsigned _5_0 : 6;
         unsigned _7_6 : 2;
         unsigned _10_8 : 3;
         unsigned : 5;
      } PACKED;

      uint16_t _;
   } PACKED;
} PACKED rogue_sC;
static_assert(sizeof(rogue_sC) == 2, "sizeof(rogue_sC) != 2");

typedef struct rogue_mux {
   union {
      struct {
         unsigned _1_0 : 2;
         unsigned _2 : 1;
         unsigned : 5;
      } PACKED;

      uint8_t _;
   } PACKED;
} PACKED rogue_mux;
static_assert(sizeof(rogue_mux) == 1, "sizeof(rogue_mux) != 1");

enum reg_bank {
   BANK_SPECIAL = 0b000,
   BANK_TEMP = 0b001,
   BANK_VTXIN = 0b010,
   BANK_COEFF = 0b011,
   BANK_SHARED = 0b100,
   BANK_COEFF_ALT = 0b101,
   BANK_IDX0 = 0b110,
   BANK_IDX1 = 0b111,
};

enum is0 {
   IS0_S0 = 0b000,
   IS0_S3 = 0b001,
   IS0_S4 = 0b010,
   IS0_S5 = 0b011,
   IS0_S1 = 0b100,
   IS0_S2 = 0b101,
};

enum is1 {
   IS1_FT0 = 0b0,
   IS1_FTE = 0b1,
};

enum is2 {
   IS2_FT1 = 0b0,
   IS2_FTE = 0b1,
};

enum is3 {
   IS3_FT0 = 0b00,
   IS3_FT1 = 0b01,
   IS3_S2 = 0b10,
   IS3_FTE = 0b11,
};

enum is4 {
   IS4_FT0 = 0b00,
   IS4_FT1 = 0b01,
   IS4_FT2 = 0b10,
   IS4_FTE = 0b11,
};

enum is5 {
   IS5_FT0 = 0b00,
   IS5_FT1 = 0b01,
   IS5_FT2 = 0b10,
   IS5_FTE = 0b11,
};

/* Single source instructions. */

typedef struct rogue_single_pck_encoding {
   /* Byte 1 */
   union {
      struct {
         union {
            struct {
               unsigned format : 5;
               unsigned scale : 1;
               unsigned elem : 2;
            } PACKED;

            struct {
               unsigned : 5;
               unsigned rtz : 1;
               unsigned : 2;
            } PACKED;
         } PACKED;
      } PACKED upck;

      struct {
         unsigned format : 5;
         unsigned scale : 1;
         unsigned rtz : 1;
         unsigned prog : 1;
      } PACKED pck;
   } PACKED;
} PACKED rogue_single_pck_encoding;
static_assert(sizeof(rogue_single_pck_encoding) == 1,
              "sizeof(rogue_single_pck_encoding) != 1");

enum pck_fmt {
   PCK_FMT_U8888 = 0b00000,
   PCK_FMT_S8888 = 0b00001,
   PCK_FMT_O8888 = 0b00010,
   PCK_FMT_U1616 = 0b00011,
   PCK_FMT_S1616 = 0b00100,
   PCK_FMT_O1616 = 0b00101,
   PCK_FMT_U32 = 0b00110,
   PCK_FMT_S32 = 0b00111,
   PCK_FMT_U1010102 = 0b01000,
   PCK_FMT_S1010102 = 0b01001,
   PCK_FMT_U111110 = 0b01010,
   PCK_FMT_S111110 = 0b01011,
   PCK_FMT_F111110 = 0b01100,
   PCK_FMT_F16F16 = 0b01110,
   PCK_FMT_F32 = 0b01111,
   PCK_FMT_COV = 0b10000,
   PCK_FMT_U565U565 = 0b10001,
   PCK_FMT_D24S8 = 0b10010,
   PCK_FMT_S8D24 = 0b10011,
   PCK_FMT_F32_MASK = 0b10100,
   PCK_FMT_2F10F10F10 = 0b10101,
   PCK_FMT_S8888OGL = 0b10110,
   PCK_FMT_S1616OGL = 0b10111,
   PCK_FMT_ZERO = 0b11110,
   PCK_FMT_ONE = 0b11111,
};

typedef struct rogue_single_mbyp_encoding {
   /* Byte 1 */
   struct {
      unsigned s0abs : 1;
      unsigned s0neg : 1;
      unsigned : 6;
   } PACKED;
} PACKED rogue_single_mbyp_encoding;
static_assert(sizeof(rogue_single_mbyp_encoding) == 1,
              "sizeof(rogue_single_mbyp_encoding) != 1");

enum snglop {
   SNGLOP_RCP = 0b0000,
   SNGLOP_RSQ = 0b0001,
   SNGLOP_LOG = 0b0010,
   SNGLOP_EXP = 0b0011,
   SNGLOP_F16SOP = 0b0100,
   SNGLOP_LOGCN = 0b0101,
   SNGLOP_GAMMA = 0b0110,
   SNGLOP_BYP = 0b0111,
   SNGLOP_DSX = 0b1000,
   SNGLOP_DSY = 0b1001,
   SNGLOP_DSXF = 0b1010,
   SNGLOP_DSYF = 0b1011,
   SNGLOP_PCK = 0b1100,
   SNGLOP_RED = 0b1101,
   SNGLOP_SINC = 0b1110,
   SNGLOP_ARCTANC = 0b1111,
};

typedef struct rogue_alu_single_encoding {
   /* Byte 0 */
   struct {
      unsigned snglop : 4;
      unsigned ext0 : 1;
      unsigned : 3;
   } PACKED;

   /* Byte 1+ */
   union {
      rogue_single_mbyp_encoding mbyp;
      rogue_single_pck_encoding pck;
   } PACKED;
} PACKED rogue_alu_single_encoding;
static_assert(sizeof(rogue_alu_single_encoding) == 2,
              "sizeof(rogue_alu_single_encoding) != 2");

typedef struct rogue_alu_fmad_encoding {
   /* Byte 0 */
   struct {
      unsigned sat : 1;
      unsigned s2neg : 1;
      unsigned s0abs : 1;
      unsigned s0neg : 1;
      unsigned ext : 1;
      unsigned : 3;
   } PACKED;

   /* Byte 1 */
   struct {
      unsigned s2abs : 1;
      unsigned s2flr : 1;
      unsigned s1neg : 1;
      unsigned s1abs : 1;
      unsigned lp : 1;
      unsigned : 3;
   } PACKED;
} PACKED rogue_alu_fmad_encoding;
static_assert(sizeof(rogue_alu_fmad_encoding) == 2,
              "sizeof(rogue_alu_fmad_encoding) != 2");

typedef struct rogue_alu_fdual_encoding {
   /* Byte 0 */
   struct {
      unsigned s0flr : 1;
      unsigned s1abs : 1;
      unsigned s0abs : 1;
      unsigned s0neg : 1;
      unsigned sat : 1;
      unsigned lp : 1;
      unsigned : 2;
   } PACKED;
} PACKED rogue_alu_fdual_encoding;
static_assert(sizeof(rogue_alu_fdual_encoding) == 1,
              "sizeof(rogue_alu_fdual_encoding) != 1");

typedef struct rogue_alu_tst_encoding {
   /* Byte 0 */
   struct {
      unsigned pwen : 1;
      unsigned tstop_2_0 : 3;
      unsigned ext : 1;
      unsigned : 3;
   } PACKED;

   /* Byte 1 */
   struct {
      unsigned tstop_3 : 1;
      unsigned : 1;
      unsigned elem : 2;
      unsigned p2end : 1;
      unsigned type : 3;
   } PACKED;
} PACKED rogue_alu_tst_encoding;
static_assert(sizeof(rogue_alu_tst_encoding) == 2,
              "sizeof(rogue_alu_tst_encoding) != 2");

typedef struct rogue_tstop {
   union {
      struct {
         unsigned _2_0 : 3;
         unsigned _3 : 1;
         unsigned : 4;
      } PACKED;

      uint8_t _;
   } PACKED;
} PACKED rogue_tstop;
static_assert(sizeof(rogue_tstop) == 1, "sizeof(rogue_tstop) != 1");

enum tstop {
   TSTOP_Z = 0b0000,
   TSTOP_GZ = 0b0001,
   TSTOP_GEZ = 0b0010,
   TSTOP_IC = 0b0011,
   TSTOP_EQ = 0b0100,
   TSTOP_GT = 0b0101,
   TSTOP_GE = 0b0110,
   TSTOP_NE = 0b0111,
   TSTOP_LT = 0b1000,
   TSTOP_LE = 0b1001,
};

typedef struct rogue_alu_mov_encoding {
   /* Byte 0 */
   struct {
      unsigned movw0 : 2;
      unsigned movw1 : 2;
      unsigned ext : 1;
      unsigned : 3;
   } PACKED;

   /* Byte 1 */
   struct {
      unsigned p2end : 1;
      unsigned aw : 1;
      unsigned maskw0 : 4;
      unsigned : 2;
   } PACKED;
} PACKED rogue_alu_mov_encoding;
static_assert(sizeof(rogue_alu_mov_encoding) == 2,
              "sizeof(rogue_alu_mov_encoding) != 2");

typedef struct rogue_alu_instr_encoding {
   union {
      /* Byte 0 */
      struct {
         unsigned : 5;
         unsigned op : 3;
      } PACKED;

      /* Bytes 1+ */
      rogue_alu_single_encoding sngl;
      rogue_alu_fdual_encoding fadd;
      rogue_alu_fdual_encoding fmul;
      rogue_alu_fmad_encoding fmad;
      rogue_alu_tst_encoding tst;
      rogue_alu_mov_encoding mov;
   } PACKED;
} PACKED rogue_alu_instr_encoding;
static_assert(sizeof(rogue_alu_instr_encoding) == 2,
              "sizeof(rogue_alu_instr_encoding) != 2");

enum aluop {
   ALUOP_FADD = 0b000, /** Phase 0, 1. */
   ALUOP_FMUL = 0b010, /** Phase 0, 1. */
   ALUOP_SNGL = 0b100, /** Phase 0, 1, 2. */
   ALUOP_INT8_16 = 0b101, /** Phase 0. */
   ALUOP_FMAD = 0b110, /** Phase 0, 1. */
   ALUOP_MOV = 0b110, /** Phase 2. */
   ALUOP_INT32_64 = 0b111, /** Phase 0. */
   ALUOP_TST = 0b111, /** Phase 2. */
};

/* Backend instructions. */

typedef struct rogue_backend_emitpix_encoding {
   /* Byte 0 */
   struct {
      unsigned : 1;
      unsigned freep : 1;
      unsigned : 6;
   } PACKED;
} PACKED rogue_backend_emitpix_encoding;
static_assert(sizeof(rogue_backend_emitpix_encoding) == 1,
              "sizeof(rogue_backend_emitpix_encoding) != 1");

typedef struct rogue_backend_fitr_encoding {
   /* Byte 0 */
   struct {
      unsigned mode : 2;
      unsigned : 1;
      unsigned drc : 1;
      unsigned p : 1;
      unsigned : 3;
   } PACKED;

   /* Byte 1 */
   struct {
      unsigned count : 4;
      unsigned sat : 1;
      unsigned : 3;
   } PACKED;
} PACKED rogue_backend_fitr_encoding;
static_assert(sizeof(rogue_backend_fitr_encoding) == 2,
              "sizeof(rogue_backend_fitr_encoding) != 2");

enum fitr_mode {
   FITR_MODE_PIXEL = 0b00,
   FITR_MODE_SAMPLE = 0b01,
   FITR_MODE_CENTROID = 0b10,
};

typedef struct rogue_backend_uvsw_encoding {
   /* Byte 0 */
   struct {
      unsigned writeop : 3;
      unsigned imm : 1;
      unsigned dsel : 1;
      unsigned : 3;
   } PACKED;

   /* Byte 1 */
   union {
      struct {
         unsigned srcsel : 3;
         unsigned : 5;
      } PACKED src;

      struct {
         unsigned imm_addr : 8;
      } PACKED imm_src;

      struct {
         unsigned streamid : 2;
         unsigned : 6;
      } PACKED stream_src;
   } PACKED;
} PACKED rogue_backend_uvsw_encoding;
static_assert(sizeof(rogue_backend_uvsw_encoding) == 2,
              "sizeof(rogue_backend_uvsw_encoding) != 2");

enum uvsw_writeop {
   UVSW_WRITEOP_WRITE = 0b000,
   UVSW_WRITEOP_EMIT = 0b001,
   UVSW_WRITEOP_CUT = 0b010,
   UVSW_WRITEOP_EMIT_CUT = 0b011,
   UVSW_WRITEOP_END = 0b100,
   UVSW_WRITEOP_EMIT_END = 0b101,
   UVSW_WRITEOP_WRITE_EMIT_END = 0b110,
};

typedef struct rogue_backend_instr_encoding {
   union {
      /* Byte 0 */
      struct {
         unsigned : 5;
         unsigned op : 3;
      } PACKED;

      rogue_backend_uvsw_encoding uvsw;
      rogue_backend_fitr_encoding fitr;
      rogue_backend_emitpix_encoding emitpix;
   } PACKED;
} PACKED rogue_backend_instr_encoding;
static_assert(sizeof(rogue_backend_instr_encoding) == 2,
              "sizeof(rogue_backend_instr_encoding) != 2");

enum backendop {
   BACKENDOP_UVSW = 0b000,
   BACKENDOP_MSK = 0b001,
   BACKENDOP_PHAS = 0b010,
   BACKENDOP_SETL = 0b011,
   BACKENDOP_VISTEST = 0b100,
   BACKENDOP_FITR = 0b101,
   BACKENDOP_EMIT = 0b110,
   BACKENDOP_DMA = 0b111,
};

/* Branch */
typedef struct rogue_ctrl_ba_encoding {
   /* Byte 0 */
   struct {
      unsigned : 1;
      unsigned abs : 1;
      unsigned allp : 1;
      unsigned anyp : 1;
      unsigned link : 1;
      unsigned : 3;
   } PACKED;

   /* Byte 1 */
   struct {
      unsigned : 1;
      unsigned offset_7_1 : 7;
   } PACKED;

   /* Byte 2 */
   struct {
      unsigned offset_15_8 : 8;
   } PACKED;

   /* Byte 3 */
   struct {
      unsigned offset_23_16 : 8;
   } PACKED;

   /* Byte 4 */
   struct {
      unsigned offset_31_24 : 8;
   } PACKED;
} PACKED rogue_ctrl_ba_encoding;
static_assert(sizeof(rogue_ctrl_ba_encoding) == 5,
              "sizeof(rogue_ctrl_ba_encoding) != 5");

typedef struct rogue_offset32 {
   union {
      struct {
         struct {
            unsigned : 1;
            unsigned _7_1 : 7;
         } PACKED;

         struct {
            unsigned _15_8 : 8;
         } PACKED;

         struct {
            unsigned _23_16 : 8;
         } PACKED;

         struct {
            unsigned _31_24 : 8;
         } PACKED;
      } PACKED;

      uint32_t _;
   } PACKED;
} PACKED rogue_offset32;
static_assert(sizeof(rogue_offset32) == 4, "sizeof(rogue_offset32) != 4");

/* NOP */
typedef struct rogue_ctrl_nop_encoding {
   /* Byte 0 */
   struct {
      unsigned : 8;
   } PACKED;
} PACKED rogue_ctrl_nop_encoding;
static_assert(sizeof(rogue_ctrl_nop_encoding) == 1,
              "sizeof(rogue_ctrl_nop_encoding) != 1");

/* Common for all control instructions. */
typedef struct rogue_ctrl_instr_encoding {
   union {
      /* Bytes 0+ */
      rogue_ctrl_ba_encoding ba;
      rogue_ctrl_nop_encoding nop;
   } PACKED;
} PACKED rogue_ctrl_instr_encoding;
static_assert(sizeof(rogue_ctrl_instr_encoding) == 5,
              "sizeof(rogue_ctrl_instr_encoding) != 5");

enum ctrlop {
   CTRLOP_BA = 0b0000,
   CTRLOP_LAPC = 0b0001,
   CTRLOP_SAVL = 0b0010,
   CTRLOP_CND = 0b0011,
   CTRLOP_WOP = 0b0100,
   CTRLOP_WDF = 0b0101,
   CTRLOP_MUTEX = 0b0110,
   CTRLOP_NOP = 0b0111,
   CTRLOP_ITRSMP = 0b1000,
   CTRLOP_UNIQ = 0b1001,
   CTRLOP_FETCH = 0b1010,
   CTRLOP_SBO = 0b1011,
};

typedef struct rogue_instr_group_header_encoding {
   /* Byte 0 */
   struct {
      unsigned length : 4;
      unsigned da : 4;
   } PACKED;

   /* Byte 1 */
   union {
      struct {
         unsigned cc : 1;
         unsigned w0p : 1;
         unsigned w1p : 1;
         unsigned olchk : 1;
         unsigned oporg : 3;
         unsigned ext : 1;
      } PACKED;

      struct {
         unsigned : 4;
         unsigned opcnt : 3;
         unsigned : 1;
      } PACKED;
   } PACKED;

   /* Byte 2 */
   union {
      struct {
         unsigned ccext : 1;
         unsigned rpt : 2;
         unsigned atom : 1;
         unsigned crel : 1;
         unsigned alutype : 2;
         unsigned end : 1;
      } PACKED;

      struct {
         unsigned : 1;
         unsigned ctrlop : 4;
         unsigned : 2;
         unsigned miscctl : 1;
      } PACKED;
   } PACKED;
} PACKED rogue_instr_group_header_encoding;
static_assert(sizeof(rogue_instr_group_header_encoding) == 3,
              "sizeof(rogue_instr_group_header_encoding) != 3");

enum oporg {
   OPORG_P0 = 0b000,
   OPORG_P2 = 0b001,
   OPORG_BE = 0b010,
   OPORG_P0_P1 = 0b011,
   OPORG_P0_P2 = 0b100,
   OPORG_P0_P1_P2 = 0b101,
   OPORG_P0_P2_BE = 0b110,
   OPORG_P0_P1_P2_BE = 0b111,
};

enum opcnt {
   OPCNT_P0 = 0b001,
   OPCNT_P1 = 0b010,
   OPCNT_P2 = 0b100,
};

enum alutype {
   ALUTYPE_MAIN = 0b00,
   ALUTYPE_BITWISE = 0b10,
   ALUTYPE_CONTROL = 0b11,
};

enum cc {
   CC_PE_TRUE = 0b00,
   CC_P0_TRUE = 0b01,
   CC_PE_ANY = 0b10,
   CC_P0_FALSE = 0b11,
};

typedef struct rogue_cc {
   union {
      struct {
         unsigned cc : 1;
         unsigned ccext : 1;
         unsigned : 6;
      } PACKED;

      uint8_t _;
   } PACKED;
} PACKED rogue_cc;
static_assert(sizeof(rogue_cc) == 1, "sizeof(rogue_cc) != 1");

#endif /* ROGUE_ISA_H */
