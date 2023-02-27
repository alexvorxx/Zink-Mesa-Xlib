/*
 * Copyright © 2023 Imagination Technologies Ltd.
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

#include "pvr_job_transfer.h"
#include "pvr_uscgen.h"
#include "rogue/rogue.h"
#include "rogue/rogue_builder.h"
#include "util/u_dynarray.h"

#include <stdbool.h>

void pvr_uscgen_per_job_eot(uint32_t state0,
                            uint32_t state1,
                            unsigned *temps_used,
                            struct util_dynarray *binary)
{
   rogue_builder b;
   rogue_shader *shader = rogue_shader_create(NULL, MESA_SHADER_NONE);
   rogue_set_shader_name(shader, "per-job EOT");
   rogue_builder_init(&b, shader);
   rogue_push_block(&b);

   rogue_reg *state_word_0 = rogue_ssa_reg(shader, 0);
   rogue_reg *state_word_1 = rogue_ssa_reg(shader, 1);

   rogue_MOV(&b, rogue_ref_reg(state_word_0), rogue_ref_imm(state0));
   rogue_MOV(&b, rogue_ref_reg(state_word_1), rogue_ref_imm(state1));

   rogue_backend_instr *emitpix = rogue_EMITPIX(&b,
                                                rogue_ref_reg(state_word_0),
                                                rogue_ref_reg(state_word_1));
   rogue_set_backend_op_mod(emitpix, ROGUE_BACKEND_OP_MOD_FREEP);

   rogue_END(&b);

   rogue_shader_passes(shader);
   rogue_encode_shader(NULL, shader, binary);

   *temps_used = rogue_count_used_regs(shader, ROGUE_REG_CLASS_TEMP);

   ralloc_free(shader);
}

void pvr_uscgen_nop(struct util_dynarray *binary)
{
   rogue_builder b;
   rogue_shader *shader = rogue_shader_create(NULL, MESA_SHADER_NONE);
   rogue_set_shader_name(shader, "NOP");
   rogue_builder_init(&b, shader);
   rogue_push_block(&b);

   rogue_END(&b);

   rogue_shader_passes(shader);
   rogue_encode_shader(NULL, shader, binary);

   ralloc_free(shader);
}
