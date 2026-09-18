replay_inc = include_directories('.')
replay_includes += replay_inc
replay_libs += static_library('replay-HighlyTheoretical',
  files('core/arm.c',
    'core/c68k/c68k.c',
    'core/c68k/c68kexec.c',
    'core/dcsound.c',
    'core/satsound.c',
    'core/sega.c',
    'core/yam.c'),
  include_directories: replay_inc, dependencies: [zlib, math],
  c_args: common_flags + ['-DHAVE_STDINT_H', '-DEMU_COMPILE', '-DEMU_LITTLE_ENDIAN', '-DLSB_FIRST', '-DC68K_NO_JUMP_TABLE', '-DNO_DYNAREC'],
  cpp_args: common_flags + ['-DHAVE_STDINT_H', '-DEMU_COMPILE', '-DEMU_LITTLE_ENDIAN', '-DLSB_FIRST', '-DC68K_NO_JUMP_TABLE', '-DNO_DYNAREC'],
  gnu_symbol_visibility: 'hidden', override_options: ['warning_level=0', 'c_std=gnu11'])
