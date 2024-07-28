# Copyright © 2019 Raspberry Pi Ltd
# SPDX-License-Identifier: MIT
inc_simulator = include_directories('.')

libbroadcom_simulator_files = files(
  'v3d_simulator.c',
  'v3d_simulator_wrapper.cpp',
)

files_per_version = files(
    'v3dx_simulator.c',
)

dep_v3d_hw = dependency('v3d_hw', required: false)
v3d_simulator_arg = '-DUSE_V3D_SIMULATOR=@0@'.format(dep_v3d_hw.found().to_int())

per_version_libs = []
foreach ver : v3d_versions
  per_version_libs += static_library(
    'v3d-simulator-v' + ver,
    [files_per_version, v3d_xml_pack],
    include_directories : [
      inc_src, inc_include, inc_broadcom,
    ],
    c_args : [v3d_simulator_arg, '-DV3D_VERSION=' + ver],
    gnu_symbol_visibility: 'hidden',
    dependencies : [dep_v3d_hw, dep_libdrm, dep_valgrind],
)
endforeach

libbroadcom_simulator = static_library(
  'broadcom_simulator',
  [libbroadcom_simulator_files],
  include_directories : [inc_src, inc_include],
  c_args : [v3d_simulator_arg, no_override_init_args],
  cpp_args : [v3d_simulator_arg],
  gnu_symbol_visibility : 'hidden', 
  dependencies : [dep_v3d_hw, dep_libdrm, dep_valgrind],
  link_with : [per_version_libs],
  build_by_default : false,
)

v3d_libs += libbroadcom_simulator
