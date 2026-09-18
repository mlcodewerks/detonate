replay_inc = include_directories('.')
replay_includes += replay_inc
replay_libs += static_library('replay-TFMX',
  files('TFMX/tfmx_audio.c',
    'TFMX/tfmx_iface.c',
    'TFMX/tfmx_loader.c',
    'TFMX/tfmx_player.c',
    'TFMX/tfmx_state.c',
    'TFMX/unsqsh.c'),
  include_directories: replay_inc, dependencies: [zlib, math],
  c_args: common_flags + [],
  cpp_args: common_flags + [],
  gnu_symbol_visibility: 'hidden', override_options: ['warning_level=0', 'c_std=gnu11'])
