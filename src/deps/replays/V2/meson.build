replay_inc = include_directories('.')
replay_includes += replay_inc
replay_libs += static_library('replay-V2',
  files('V2/ronan.cpp',
    'V2/scriptasm.cpp',
    'V2/sounddef.cpp',
    'V2/synthasm.cpp',
    'V2/synth_core.cpp',
    'V2/v2mconv.cpp',
    'V2/v2mplayer.cpp'),
  include_directories: replay_inc, dependencies: [zlib, math],
  c_args: common_flags + [],
  cpp_args: common_flags + ['-fno-strict-aliasing', '-fwrapv'],
  gnu_symbol_visibility: 'hidden', override_options: ['warning_level=0', 'c_std=gnu11'])
