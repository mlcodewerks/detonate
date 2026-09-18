unarr_config = configuration_data()
unarr_config.set('unarr_VERSION_MAJOR', 1)
unarr_config.set('unarr_VERSION_MINOR', 2)
unarr_config.set('unarr_VERSION_PATCH', 0)
unarr_config.set('unarr_VERSION', '1.2.0')
configure_file(input: 'unarr.h.in', output: 'unarr.h', configuration: unarr_config)
unarr = static_library('unarr',
  'common/conv.c', 'common/crc32.c', 'common/stream.c', 'common/unarr.c',
  'rar/filter-rar.c', 'rar/uncompress-rar.c', 'rar/huffman-rar.c',
  'rar/rar.c', 'rar/rarvm.c', 'rar/parse-rar.c',
  'lzmasdk/Ppmd7.c', 'lzmasdk/Ppmd7aDec.c',
  'zip/zip.c', 'zip/parse-zip.c', 'zip/uncompress-zip.c', 'zip/inflate.c',
  '_7z/_7z.c', 'lzmasdk/7zArcIn.c', 'lzmasdk/7zBuf.c', 'lzmasdk/7zDec.c',
  'lzmasdk/7zStream.c', 'lzmasdk/Bcj2.c', 'lzmasdk/Bra.c', 'lzmasdk/Bra86.c',
  'lzmasdk/Delta.c', 'lzmasdk/Lzma2Dec.c', 'lzmasdk/LzmaDec.c',
  'lzmasdk/CpuArch.c', 'lzmasdk/Ppmd7Dec.c', 'lzmasdk/Ppmd8.c', 'lzmasdk/Ppmd8Dec.c',
  include_directories: include_directories('.'),
  dependencies: zlib,
  c_args: common_flags + ['-DHAVE_ZLIB', '-DHAVE_7Z', '-DZ7_PPMD_SUPPORT'], gnu_symbol_visibility: 'hidden',
  override_options: ['warning_level=0'])
unarr_dep = declare_dependency(link_with: unarr,
  include_directories: include_directories('.'))
