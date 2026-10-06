replay_libs += static_library('replay-atari',
  'AtariAudioRenderer.cpp', 'AtariMachine.cpp', 'Blitter.cpp',
  'Mk68901.cpp', 'SndhRenderer.cpp', 'SteDac.cpp', 'YmRenderer.cpp',
  'timedb.cpp', 'ym2149c.cpp', 'external/ice_24.cpp', 'external/lzh.cpp',
  'external/Musashi/m68k.cpp', 'external/Musashi/m68kcpu.cpp',
  'external/Musashi/m68kops.cpp',
  dependencies: [math, threads], cpp_args: common_flags + ['-fwrapv', '-DNDEBUG'],
  gnu_symbol_visibility: 'hidden', override_options: ['warning_level=0'])
