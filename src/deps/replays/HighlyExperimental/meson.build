replay_inc = include_directories('.')
replay_includes += replay_inc
replay_libs += static_library('replay-HighlyExperimental',
  files('core/bios.c',
    'core/iop.c',
    'core/ioptimer.c',
    'core/psx.c',
    'core/r3000.c',
    'core/r3000asm.c',
    'core/r3000dis.c',
    'core/spu.c',
    'core/spucore.c',
    'core/vfs.c',
    'psflib/psf2fs.c',
    'psflib/psflib.c'),
  include_directories: replay_inc, dependencies: [zlib, math],
  c_args: common_flags + ['-DEMU_COMPILE', '-DEMU_LITTLE_ENDIAN'],
  cpp_args: common_flags + ['-DEMU_COMPILE', '-DEMU_LITTLE_ENDIAN'],
  gnu_symbol_visibility: 'hidden', override_options: ['warning_level=0', 'c_std=gnu11'])
