# Copyright © 2022 Collabora, Ltd.
# SPDX-License-Identifier: MIT
if meson.version().version_compare('< 1.3.0')
  error('NVK requires meson 1.3.0 or newer')
endif

add_languages('rust', required: true)
rust = import('rust')

rc = meson.get_compiler('rust')
if rc.version().version_compare('< 1.73.0')
  error('NAK requires Rust 1.73.0')
endif

bindgen_version = find_program('bindgen').version()

if bindgen_version == 'unknown'
  error('Failed to detect bindgen version. If you are using bindgen 0.69.0, ' +
        'please either update to 0.69.1 or downgrade to 0.68.1. You can ' +
        'install the latest version for your user with ' +
        '`cargo install bindgen-cli`.')
endif

if bindgen_version.version_compare('< 0.65')
  error('NAK requires bindgen 0.65 or newer. If your distribution does not ' +
        'ship a recent enough version, you can install the latest version ' +
        'for your user with `cargo install bindgen-cli`.')
endif

nak_rust_args = [
  '-Aclippy::identity_op',
  '-Aclippy::len_zero',
  '-Aclippy::manual_range_contains',
  # normally this is a good one, but we use it where the "better" code is worse
  '-Aclippy::needless_range_loop',
  '-Aclippy::redundant_field_names',
  '-Aclippy::upper_case_acronyms',
  '-Aclippy::vec_box',
  '-Aclippy::write_with_newline',
  # warns about public function might dereference a raw pointer, but nothing is
  # actually public here
  '-Aclippy::not_unsafe_ptr_arg_deref',
  '-Anon_snake_case',
]

dep_syn = dependency('syn',
  version : '>= 2.0.15',
  fallback : ['syn', 'dep_syn'],
  required : true,
)

dep_paste = dependency('paste',
  version : '>= 1.0.14',
  fallback : ['paste', 'dep_paste'],
  required : true,
)

libnak_c_files = files(
  'nak.h',
  'nak_nir.c',
  'nak_nir_lower_cf.c',
  'nak_nir_lower_fs_inputs.c',
  'nak_nir_lower_gs_intrinsics.c',
  'nak_nir_lower_non_uniform_ldcx.c',
  'nak_nir_lower_scan_reduce.c',
  'nak_nir_lower_tex.c',
  'nak_nir_lower_vtg_io.c',
  'nak_nir_split_64bit_conversions.c',
  'nak_memstream.c',
)

_libacorn_rs = static_library(
  'acorn',
  files('acorn/lib.rs'),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
  rust_args : nak_rust_args,
)

idep_acorn_rs = declare_dependency(
  link_with : _libacorn_rs,
)

_libbitview_rs = static_library(
  'bitview',
  files('bitview/lib.rs'),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
  rust_args : nak_rust_args,
)

idep_bitview_rs = declare_dependency(
  link_with : _libbitview_rs,
)

libnak_deps = [
  idep_mesautil,
  idep_nir_headers,
  idep_nvidia_headers,
]

_nak_bindings_rs = rust.bindgen(
  input : ['nak_bindings.h'],
  output : 'nak_bindings.rs',
  c_args : [
    pre_args,
  ],
  args : [
    '--raw-line', '#![allow(non_camel_case_types)]',
    '--raw-line', '#![allow(non_snake_case)]',
    '--raw-line', '#![allow(non_upper_case_globals)]',
    '--allowlist-type', 'drm.*',
    '--allowlist-type', 'exec_list',
    '--allowlist-type', 'exec_node',
    '--allowlist-type', 'float_controls',
    '--allowlist-type', 'gl_access_qualifier',
    '--allowlist-type', 'gl_frag_result',
    '--allowlist-type', 'gl_interp_mode',
    '--allowlist-type', 'gl_shader_stage',
    '--allowlist-type', 'gl_system_value',
    '--allowlist-type', 'gl_tess_spacing',
    '--allowlist-type', 'gl_varying_slot',
    '--allowlist-type', 'gl_vert_attrib',
    '--allowlist-type', 'nak_.*',
    '--allowlist-type', 'nir_.*',
    '--allowlist-type', 'nouveau_ws_.*',
    '--allowlist-type', 'mesa_scope',
    '--allowlist-type', 'mesa_prim',
    '--allowlist-type', 'tess_primitive_mode',
    '--allowlist-var', 'DRM_.*',
    '--allowlist-var', 'nir_.*_infos',
    '--allowlist-var', 'NVIDIA_VENDOR_ID',
    '--allowlist-function', 'drm.*',
    '--allowlist-function', 'glsl_.*',
    '--allowlist-function', '_mesa_shader_stage_to_string',
    '--allowlist-function', 'nak_.*',
    '--allowlist-function', 'nir_.*',
    '--allowlist-function', 'nouveau_ws_.*',
    '--no-prepend-enum-name',
  ],
  dependencies : [
    dep_libdrm,
    idep_nouveau_ws,
    libnak_deps,
  ],
)

_libnak_bindings_rs = static_library(
  'nak_bindings',
  _nak_bindings_rs,
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
)

_libnak_ir_proc_rs = rust.proc_macro(
  'nak_ir_proc',
  files('nak/ir_proc.rs'),
  dependencies : [dep_syn],
)

_libnak_qmd_rs = static_library(
  'nak_qmd_rs',
  files('nak_qmd/lib.rs'),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'c',
  rust_args : nak_rust_args,
  dependencies : [
    dep_paste,
    idep_nvidia_headers_rs,
  ],
  link_with: [
    _libbitview_rs,
    _libnak_bindings_rs,
  ],
)

_libnak_rs = static_library(
  'nak_rs',
  files('nak/lib.rs'),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'c',
  rust_args : [
    nak_rust_args,
    # Otherwise, rustc trips up on -pthread
    '-Clink-arg=-Wno-unused-command-line-argument',
  ],
  link_with: [
    _libbitview_rs,
    _libnak_bindings_rs,
    _libnak_ir_proc_rs,
    _libnak_qmd_rs,
  ],
)

# TODO: Linking Rust executables (such as unit tests) doesn't play nicely
# with the sanitizers because meson doesn't know to pass -fsanitize to the
# Rust linker.  See also https://github.com/mesonbuild/meson/issues/11741
if with_tests and get_option('b_sanitize') == 'none'
  _libnak_runner = static_library(
    'nak_runner',
    files('nak_runner/lib.rs'),
    gnu_symbol_visibility : 'hidden',
    rust_abi : 'rust',
    rust_args : nak_rust_args,
    dependencies : [
      dep_libdrm,
      idep_nouveau_ws,
      idep_nvidia_headers_rs,
      idep_nv_push_rs,
    ],
    link_with: [
      _libnak_bindings_rs,
      _libnak_qmd_rs,
    ],
  )

  rust.test(
    'nak',
    _libnak_rs,
    args : [
      # Don't run HW tests by default
      '--skip', 'hw_tests::',
    ],
    suite : ['nouveau'],
    dependencies : [
      idep_mesautil.partial_dependency(link_args : true, links : true),
      idep_compiler.partial_dependency(link_args : true, links : true),
    ],
    link_with: [
      _libacorn_rs,
      _libnak_runner,
    ],
  )
endif

nak_nir_algebraic_c = custom_target(
  'nak_nir_algebraic.c',
  input : 'nak_nir_algebraic.py',
  output : 'nak_nir_algebraic.c',
  command : [
    prog_python, '@INPUT@',
    '-p', dir_compiler_nir,
    '--out', '@OUTPUT@',
  ],
  depend_files : nir_algebraic_depends,
)

_libnak = static_library(
  'nak',
  [libnak_c_files, nak_nir_algebraic_c],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium],
  dependencies : libnak_deps,
  link_with : [_libnak_rs],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
)

idep_nak = declare_dependency(
  include_directories : include_directories('.'),
  link_with : _libnak,
)
