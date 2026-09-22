project('detonate-libretro', 'c', 'cpp', version: '2.0',
  default_options: ['buildtype=debugoptimized', 'c_std=c11', 'cpp_std=c++20', 'warning_level=2'])
cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
inc = include_directories('src', 'src/deps/imgui', 'src/deps/libretro-common/include',
  'src/deps', 'src/deps/decoder',
  'src/deps/decoder/musepack', 'src/player', 'src/player/formats')
wavpack = dependency('wavpack', static: true)
zlib = dependency('zlib', static: true)
math = cc.find_library('m', required: false)
threads = dependency('threads')
common_flags = cc.get_supported_arguments('-ffunction-sections', '-fdata-sections')
link_flags = cpp.get_supported_link_arguments('-Wl,--gc-sections')
if host_machine.system() == 'windows'
  link_flags += ['-static']
endif
subdir('src/deps/libvgm')
subdir('src/deps/gme')
subdir('src/deps/unarr')
subdir('src/deps/replays')
vgm_platform = []
vgm_deps = [zlib, math]
if host_machine.system() == 'windows'
  vgm_platform += 'src/deps/libvgm/utils/StrUtils-CPConv_Win.c'
else
  vgm_platform += 'src/deps/libvgm/utils/StrUtils-CPConv_IConv.c'
  vgm_deps += dependency('iconv')
endif
vgm_flags = []
gme_flags = ['-DHAVE_ZLIB_H', '-DVGM_YM2612_NUKED']
if host_machine.endian() == 'little'
  vgm_flags += '-DVGM_LITTLE_ENDIAN'
  gme_flags += '-DBLARGG_LITTLE_ENDIAN'
else
  vgm_flags += '-DVGM_BIG_ENDIAN'
  gme_flags += '-DBLARGG_BIG_ENDIAN'
endif
vgm = static_library('vgm', vgm_sources + vgm_platform,
  dependencies: vgm_deps, c_args: common_flags + vgm_flags,
  cpp_args: common_flags + vgm_flags, gnu_symbol_visibility: 'hidden',
  override_options: ['warning_level=0'])
gme = static_library('gme', gme_sources, dependencies: [zlib, math],
  c_args: common_flags + gme_flags,
  cpp_args: common_flags + gme_flags,
  gnu_symbol_visibility: 'hidden', override_options: ['warning_level=0'])
codec_flags = ['-DHAVE_RWAV', '-DHAVE_RFLAC', '-DHAVE_RMP3', '-DHAVE_RVORBIS',
  '-DHAVE_ROPUS', '-DHAVE_RAAC', '-DHAVE_RMP4', '-DHAVE_RMODTRACKER']
common_root = 'src/deps/libretro-common/'
common_files = [
  'formats/audio_transfer.c', 'formats/wav/rwav.c', 'formats/flac/rflac.c',
  'formats/mp3/rmp3.c', 'formats/vorbis/rvorbis.c', 'formats/opus/ropus.c',
  'formats/aac/raac.c', 'formats/mp4/rmp4.c', 'formats/mod/rmodtracker.c',
  'encodings/encoding_utf.c', 'time/rtime.c', 'encodings/encoding_crc32.c', 'features/features_cpu.c', 'file/file_path.c',
  'string/stdstring.c', 'compat/compat_strl.c', 'memmap/memalign.c',
  'audio/resampler/drivers/sinc_resampler.c',
]
common_sources = []
foreach file : common_files
  common_sources += common_root + file
endforeach
common = static_library('retro-audio', common_sources, include_directories: inc,
  c_args: common_flags + codec_flags, gnu_symbol_visibility: 'hidden')
player = static_library('player',
  'src/player/audiodecode.cpp', 'src/player/formats/common.cpp',
  'src/player/audiotags.cpp',
  'src/player/visualization.cpp',
  'src/player/archive_reader.cpp', 'src/player/file_browser.cpp',
  'src/player/formats/vgm.cpp', 'src/player/formats/gme.cpp',
  'src/player/formats/wav.cpp', 'src/player/formats/mpc.cpp', 'src/player/formats/wv.cpp',
  'src/player/formats/mpc_dep.c',
  'src/player/formats/replay_synth.cpp',
  'src/player/formats/sid.cpp',
  'src/player/formats/tfmx.cpp',
  'src/player/formats/hively.cpp',
  'src/player/formats/futurecomposer.cpp',
  'src/player/formats/ken.cpp',
  'src/player/formats/xsf_loader.cpp',
  'src/player/formats/xsf_psx.cpp',
  'src/player/formats/xsf_sega.cpp',
  'src/player/formats/xsf_qsf.cpp',
  'src/player/formats/xsf_usf.cpp',
  'src/player/formats/xsf_gsf.cpp',
  'src/player/formats/xsf_snsf.cpp',
  'src/player/formats/xsf_2sf.cpp',

  include_directories: [inc, include_directories('src/deps/replays/SidPlay/libsidplayfp', 'src/deps/replays/HighlyAdvanced/mGBA/include')], dependencies: [wavpack, zlib, unarr_dep, threads], link_with: [vgm, gme] + replay_libs,
  c_args: common_flags, cpp_args: common_flags + ['-DM_CORE_GBA', '-DMINIMAL_CORE=3', '-DDISABLE_THREADING', '-DMGBA_STANDALONE'], gnu_symbol_visibility: 'hidden')
imgui = static_library('imgui',
  'src/deps/imgui/imgui.cpp', 'src/deps/imgui/imgui_draw.cpp',
  'src/deps/imgui/imgui_tables.cpp', 'src/deps/imgui/imgui_widgets.cpp',
  'src/deps/imgui/imgui_sw.cpp',
  include_directories: inc, gnu_symbol_visibility: 'hidden')
core = shared_library('detonate-libretro',
  'src/libretro.cpp', 'src/imgui_libretro.cpp', 'src/player/imgui_playermenu.cpp',
  name_prefix: '', include_directories: inc,
  link_with: [player, common, imgui], dependencies: [wavpack, math, threads],
  cpp_args: ['-DLIBRETRO'], link_args: link_flags,
  gnu_symbol_visibility: 'hidden', install: true,
  install_dir: join_paths(meson.project_source_root(), 'compile_dir'))

if host_machine.system() == 'windows' and not get_option('standalone').disabled()
  if cc.get_argument_syntax() != 'gcc'
    error('The CRT-less Windows loader requires MinGW GCC or Clang with GNU driver syntax')
  endif
  native_entry = 'detonate_entry'
  if host_machine.cpu_family() == 'x86'
    native_entry = '_detonate_entry'
  endif
  native_deps = []
  foreach lib : ['kernel32', 'user32', 'gdi32', 'winmm', 'shell32']
    native_deps += cc.find_library(lib)
  endforeach
  standalone = executable('detonate', 'src/standalone/win32_loader.c',
    include_directories: inc, dependencies: native_deps,
    c_args: ['-Os', '-fno-builtin', '-fno-stack-protector', '-fno-ident',
      '-ffunction-sections', '-fno-data-sections', '-Wno-missing-field-initializers'],
    link_args: ['-nostdlib', '-Wl,--entry,' + native_entry, '-Wl,--gc-sections', '-Wl,--strip-all'],
    override_options: ['b_lto=false', 'b_sanitize=none', 'b_coverage=false'],
    win_subsystem: 'windows', install: true,
    install_dir: join_paths(meson.project_source_root(), 'compile_dir'))
elif host_machine.system() != 'windows'
  sdl3 = dependency('sdl3', version: '>=3.2.0', required: get_option('standalone'))
  if sdl3.found()
    standalone = executable('detonate-sdl3', 'src/standalone/sdl_loader.cpp',
      include_directories: inc, dependencies: sdl3, link_args: link_flags,
      win_subsystem: 'console', install: true,
      install_dir: join_paths(meson.project_source_root(), 'compile_dir'))
  endif
endif
