# Copyright © 2022 Collabora Ltd
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

libnil_files = files(
  'nil_format.c',
  'nil_format.h',
  'nil_image.c',
  'nil_image.h',
  'nil_image_tic.c',
)

libnil_deps = [
  idep_mesautil,
  idep_nouveau_ws,
  idep_nvidia_headers,
]

_libnil = static_library(
  'nil',
  libnil_files,
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium],
  dependencies : libnil_deps,
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
)

idep_nil = declare_dependency(
  include_directories : include_directories('.'),
  link_with : _libnil,
)
