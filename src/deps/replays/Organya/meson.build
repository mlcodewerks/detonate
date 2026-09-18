replay_inc = include_directories('.')
replay_includes += replay_inc
replay_libs += static_library('replay-Organya',
  files('OrganyaDecoder.cpp'),
  include_directories: replay_inc, dependencies: [zlib, math],
  c_args: common_flags + [],
  cpp_args: common_flags + [],
  gnu_symbol_visibility: 'hidden', override_options: ['warning_level=0', 'c_std=gnu11'])
