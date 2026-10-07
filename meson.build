project('detonate-libretro', 'c', 'cpp', version: '2.0',
  default_options: ['buildtype=debugoptimized', 'c_std=c11', 'cpp_std=c++20', 'warning_level=2'])
cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
inc = include_directories('src', 'src/deps/imgui', 'src/deps/libretro-common/include',
  'src/deps', 'src/deps/decoder',
  'src/deps/decoder/musepack', 'src/player', 'src/player/formats')
wavpack = dependency('wavpack', static: true)
openmpt = dependency('libopenmpt', version: '>=0.6', static: true)
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
subdir('src/deps/midi')
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
  '-DHAVE_ROPUS', '-DHAVE_RAAC', '-DHAVE_RMP4']
common_root = 'src/deps/libretro-common/'
common_files = [
  'formats/audio_transfer.c', 'formats/wav/rwav.c', 'formats/flac/rflac.c',
  'formats/mp3/rmp3.c', 'formats/vorbis/rvorbis.c', 'formats/opus/ropus.c',
  'formats/aac/raac.c', 'formats/mp4/rmp4.c',
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
shader_presets = custom_target('shader-presets',
  input: files('shaders/spectrum.stoy', 'shaders/bars.stoy', 'shaders/oscilloscope.stoy'),
  output: 'shader_presets.h',
  command: [import('python').find_installation(), files('shaders/embed_presets.py'), '@OUTPUT@', '@INPUT@'])
player = static_library('player',
  shader_presets, 'src/player/shader_visualization.cpp',
  'src/player/audiodecode.cpp', 'src/player/formats/common.cpp',
  'src/player/audiotags.cpp', 'src/player/replayer_settings.cpp',
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
  'src/player/formats/adlib.cpp',
  'src/player/formats/midi.cpp',
  'src/player/formats/pretracker.cpp',
  'src/player/formats/uade.cpp',
  'src/player/formats/openmpt.cpp',
  'src/player/formats/atari.cpp',
  'src/player/formats/xsf_loader.cpp',
  'src/player/formats/xsf_psx.cpp',
  'src/player/formats/xsf_sega.cpp',
  'src/player/formats/xsf_qsf.cpp',
  'src/player/formats/xsf_usf.cpp',
  'src/player/formats/xsf_gsf.cpp',
  'src/player/formats/xsf_snsf.cpp',
  'src/player/formats/xsf_2sf.cpp',

  include_directories: [inc, include_directories('src/deps/replays/AdLib/adplug/libbinio', 'src/deps/replays/SidPlay/libsidplayfp', 'src/deps/replays/HighlyAdvanced/mGBA/include')], dependencies: [wavpack, openmpt, zlib, unarr_dep, midi_dep, threads, cpp.find_library('dl', required: false)], link_with: [vgm, gme] + replay_libs,
  c_args: common_flags, cpp_args: common_flags + ['-DDETONATE_UADE_HELPER="' + uade_helper.full_path().replace('\\', '/') + '"', '-DM_CORE_GBA', '-DMINIMAL_CORE=3', '-DDISABLE_THREADING', '-DMGBA_STANDALONE'], gnu_symbol_visibility: 'hidden')
gl_renderer = []
gl_flags = []
if get_option('gl33')
  gl_sources = ['src/deps/imgui/imgui_impl_opengl3.cpp', 'src/glsym/glsym_gl.c', 'src/glsym/rglgen.c', 'src/glsym/glsym_gl11.cpp']
  gl_renderer = [static_library('imgui-gl33', gl_sources,
    include_directories: inc, gnu_symbol_visibility: 'hidden')]
  gl_flags = ['-DDETONATE_GL33']
endif
if get_option('gles2')
  gl_renderer += [static_library('imgui-gles2', 'src/gles2_renderer.cpp', 'src/glsym/gles2_loader.cpp',
    include_directories: inc, gnu_symbol_visibility: 'hidden')]
  gl_flags += ['-DDETONATE_GLES2']
endif
imgui = static_library('imgui',
  'src/deps/ImGuiColorTextEdit/TextEditor.cpp',
  'src/deps/imgui/imgui.cpp', 'src/deps/imgui/imgui_draw.cpp',
  'src/deps/imgui/imgui_tables.cpp', 'src/deps/imgui/imgui_widgets.cpp',
  'src/deps/imgui/imgui_sw.cpp',
  include_directories: inc, gnu_symbol_visibility: 'hidden')
shader_renderer_sources = []
install_data('shaders/spectrum.stoy', 'shaders/bars.stoy', 'shaders/oscilloscope.stoy',
  install_dir: join_paths(meson.project_source_root(), 'compile_dir', 'shaders'))
if get_option('gl33') or get_option('gles2')
  shader_renderer_sources += 'src/shader_renderer.cpp'
endif
core = shared_library('detonate-libretro',
  'src/libretro.cpp', 'src/imgui_libretro.cpp', 'src/player/imgui_playermenu.cpp',
  'src/player/imgui_shader_windows.cpp', shader_renderer_sources,
  name_prefix: '', include_directories: inc, link_depends: uade_helper,
  link_with: [player, common, imgui] + gl_renderer, dependencies: [wavpack, math, threads],
  cpp_args: ['-DLIBRETRO'] + gl_flags, link_args: link_flags,
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
    c_args: gl_flags + ['-Os', '-fno-builtin', '-fno-stack-protector', '-fno-ident',
      '-ffunction-sections', '-fno-data-sections', '-Wno-missing-field-initializers'],
    link_args: ['-nostdlib', '-Wl,--entry,' + native_entry, '-Wl,--gc-sections', '-Wl,--strip-all'],
    override_options: ['b_lto=false', 'b_sanitize=none', 'b_coverage=false'],
    win_subsystem: 'windows', install: true,
    install_dir: join_paths(meson.project_source_root(), 'compile_dir'))
elif host_machine.system() != 'windows'
  sdl3 = dependency('sdl3', version: '>=3.2.0', required: get_option('standalone'))
  if sdl3.found()
    standalone = executable('detonate-sdl3', 'src/standalone/sdl_loader.cpp',
      include_directories: inc, dependencies: sdl3, link_args: link_flags, cpp_args: gl_flags,
      win_subsystem: 'console', install: true,
      install_dir: join_paths(meson.project_source_root(), 'compile_dir'))
  endif
endif

foreach smoke : ['decoder', 'atari_openmpt', 'new_replays', 'replayer_settings', 'shader_format', 'midi']
  smoke_exe = executable(smoke + '-smoke', 'test/' + smoke + '_smoke.cpp',
    include_directories: inc, link_with: [player, common],
    dependencies: [wavpack, openmpt, math, threads], link_args: link_flags)
  test(smoke, smoke_exe, args: [meson.project_source_root()], depends: uade_helper, timeout: 180)
endforeach

replayer_menu_smoke = executable('replayer-menu-smoke',
  'test/replayer_menu_smoke.cpp', 'src/player/imgui_playermenu.cpp', 'src/player/imgui_shader_windows.cpp',
  include_directories: inc, link_with: [player, common, imgui],
  dependencies: [wavpack, openmpt, math, threads], link_args: link_flags)
test('replayer-menu', replayer_menu_smoke, timeout: 60)
shader_editor_smoke = executable('shader-editor-smoke', 'test/shader_editor_smoke.cpp',
  include_directories: inc, link_with: [player, common, imgui],
  dependencies: [wavpack, openmpt, math, threads], link_args: link_flags)
test('shader-editor', shader_editor_smoke, timeout: 60)

# Hardware tests use the SDL host on either platform and require a working display.
if get_option('gl33') or get_option('gles2')
  gl_test_sdl = dependency('sdl3', version: '>=3.2.0', required: false)
  if gl_test_sdl.found()
    shader_gpu_test = executable('shader-gpu-smoke', 'test/shader_gpu_smoke.cpp', 'src/shader_renderer.cpp',
      include_directories: inc, link_with: [player], dependencies: [gl_test_sdl, math], cpp_args: gl_flags, link_args: link_flags)
    gl_test = executable('hardware-renderer-smoke', 'test/hardware_renderer_smoke.cpp',
      include_directories: inc, dependencies: gl_test_sdl, cpp_args: gl_flags)
    foreach renderer : ['gl33', 'gles2']
      if get_option(renderer)
        test(renderer + '-shaders', shader_gpu_test, args: [renderer], suite: renderer, timeout: 60)
        test(renderer + '-render', gl_test, args: [core.full_path(), renderer], depends: core,
          env: ['SDL_AUDIO_DRIVER=dummy'], suite: renderer, timeout: 60)
      endif
    endforeach
  endif
endif
