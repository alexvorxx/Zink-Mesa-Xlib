# Copyright © 2022 Imagination Technologies Ltd.

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

libpowervr_rogue_files = files(
  'nir/rogue_nir_constreg.c',
  'nir/rogue_nir_lower_io.c',
  'nir/rogue_nir_pfo.c',

  'rogue.c',
  'rogue_build_data.c',
  'rogue_compiler.c',
  'rogue_constreg.c',
  'rogue_dump.c',
  'rogue_encode.c',
  'rogue_encoders.c',
  'rogue_instr.c',
  'rogue_nir.c',
  'rogue_operand.c',
  'rogue_regalloc.c',
  'rogue_shader.c',
  'rogue_util.c',
  'rogue_validate.c',
)

libpowervr_rogue = shared_library(
  'powervr_rogue',
  libpowervr_rogue_files,
  include_directories : [
    inc_imagination,
    inc_include,
    inc_compiler,
    inc_src,
    inc_mapi,
    inc_mesa,
    inc_gallium,
    inc_gallium_aux,
  ],
  c_args : [c_msvc_compat_args, no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_mesautil, idep_nir, dep_csbgen],
  install : true,
)

rogue_compiler = executable(
  'rogue_compiler',
  'tools/offline_compiler.c',
  link_with : [libpowervr_rogue],
  dependencies : [idep_mesautil, idep_nir],
  include_directories : [
    inc_mesa,
    inc_include,
    inc_src,
    inc_mapi,
    inc_gallium,
    inc_gallium_aux,
    inc_compiler,
  ],
  install : false,
)