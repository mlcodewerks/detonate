# Detonate

A libretro audio player with a software-rendered Dear ImGui file browser.
Requires a frontend accepting XRGB8888 software video; no hardware graphics
context is requested. Video is 1280 x 720 at 60 Hz, with stereo 44.1 kHz audio.
Windows/MSYS2 and Linux builds use C++20, Meson and Ninja.

## Standalone player

`detonate.exe` on Windows and `detonate-sdl3` on other platforms run the core
without RetroArch or another libretro frontend.
It loads `detonate-libretro.dll` (Windows), `.so` (Linux), or `.dylib` (macOS)
from beside the executable. Keep the loader and core together in `compile_dir`.

```sh
./compile_dir/detonate-sdl3                    # open the browser
./compile_dir/detonate-sdl3 "path/to/song.flac"
./compile_dir/detonate-sdl3 "path/to/music.zip"
./compile_dir/detonate-sdl3 --core "path/to/detonate-libretro.so" "song.flac"
```

On Windows, use `compile_dir\detonate.exe` with the same arguments. You can also drag a file or
archive onto the window. The existing mouse/keyboard UI works unchanged;
**F11** toggles fullscreen and **Ctrl+Q** quits. Resizing preserves the 16:9
display and maps mouse coordinates through the letterboxing. Audio continues
when the window loses focus; held keys and mouse buttons are released.

File opens, directory scans, archive expansion and song initialization run in
background workers. The native Windows host also uses this path for startup
content and drag-and-drop, keeping frames and input flowing while loading.
New requests replace queued work; Stop prevents a pending song from starting.
Seeking and subsong changes also run off the UI thread. Navigation can continue
during song loading, and existing playback continues during directory/archive
navigation. Song replacement or seeking outputs silence until the decoder is
ready. Shutdown joins outstanding work before unloading the core.

Files with multiple decoder-supported subsongs (including NSF/NSFE, SID,
HivelyTracker and TFMX) appear as folders, including inside archives. Open one
to browse its numbered subsongs; hover an entry for its title, artist, album
and duration when available. Each entry plays its selected subsong and participates
in directory playback, repeat and shuffle. Up returns to the containing folder
or archive. Files with only one song remain ordinary playable entries.

Common audio formats, trackers, Future Composer, Musepack and Wave64 can open archive members
directly from memory. Filename-dependent replay engines still use a private
companion directory, prepared and cleaned up by the playback worker. WavPack
buffers both its audio and optional correction file during loading, so playback
and seeking do not read from disk. The GDI presenter clears only letterbox bars,
avoiding the black frame that previously caused flicker.

The Windows loader uses GDI for video and WinMM for stereo PCM audio, with
at most 100 ms of audio queued. It supports Win32 and Win64, Unicode paths and
text input, and needs a core DLL of the same architecture. It has no CRT startup,
C/C++ runtime or SDL dependency; it imports only Kernel32, User32, GDI32,
Shell32 and WinMM. This applies to the loader; the separately built core retains
its existing dependencies. The MinGW GCC/Clang build uses size optimization,
dead-code removal and stripped symbols. Zero-initialized buffers stay in BSS
so they consume no space in the executable. Windows builds enable it by default.

On other platforms, install SDL3 development files (3.2 or later); the loader is
built automatically when SDL3 is available. Use `-Dstandalone=enabled` with Meson
to require the platform's loader, or `-Dstandalone=disabled` for a core-only build.

The non-Windows host uses SDL3's [audio streams](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream)
and [logical presentation](https://wiki.libsdl.org/SDL3/SDL_SetRenderLogicalPresentation)
APIs. Headless SDL tests use dummy audio/video drivers and a bounded
`--hidden --frames N` run; input tests cover fast clicks, focus loss, wheel
accumulation and keyboard translation.

Windows also accepts `--hidden --frames N`, but uses the real WinMM device
(an audio output device is required). The `win32-*` Meson tests cover the
browser, FLAC playback, archives, load failures, input, Unicode text and pitched
framebuffers, including a check that background clearing preserves the frame.
The `async-loading` test blocks a decoder behind a synchronization gate to
check frontend progress, request replacement, Stop and archive data ownership.
The real-core test also exercises asynchronous errors, directory opens,
replacement loads and shutdown with work pending. The input/video test needs
no audio device. To build just the
native loader, use `meson compile -C builddir detonate`. For a 32-bit build use
a separate Meson build directory configured with the MSYS2 MINGW32 toolchain;
MINGW64 produces the 64-bit loader. Keep each loader beside its matching core.

Dear ImGui is rebased to WTFweg's (`../wtweg`) 1.93.0 WIP / 19296 snapshot.
The native audio decoders and sinc resampler use the vendored
[libretro-common revision 214aa1d](https://github.com/libretro/libretro-common/tree/214aa1dbd511b8a71aa8d8c51461d8ab84811309).
See the `UPSTREAM.md` files in `src/deps` for provenance.

## Formats

| Formats | Decoder |
| --- | --- |
| WAV | libretro-common rwav |
| FLAC, FLA | libretro-common rflac |
| MP3 | libretro-common rmp3 |
| OGG, OGA | Content-detected Vorbis, Opus or FLAC through libretro-common |
| Opus | libretro-common ropus |
| AAC, M4A | libretro-common raac / rmp4 (AAC-LC) |
| MOD, S3M, XM, IT | libretro-common rmodtracker |
| Musepack (MPC, MPP, MP+) | Bundled Musepack, rewritten adapter |
| WavPack (WV) | System libwavpack, rewritten adapter |
| Wave64 (W64) | Bundled dr_wav, rewritten adapter |
| VGM, VGZ (gzip VGM) | rePlayer's libvgm and chip cores |
| AY, GBS, GYM, HES, KSS, NSF, NSFE, SAP, SPC | rePlayer's Game_Music_Emu |
| RSN (RAR4 archives of SPC tracks) | unarr (including solid compression) + Game_Music_Emu |
| FC, FC13, FC14, FC3, FC4, SMOD; HIP, HIP7, HIPC, MCMD, DNS; TFMX, TFX, TFM, MDAT | rePlayer's FutureComposer / libtfmxaudiodecoder (existing TFMX backend retained as fallback) |
| KDM, KSM, SM, SND | rePlayer's Ken Silverman engine (digital samples / Adlib synthesis) |

All supported module formats provide duration reporting, seeking and looping.
AAC profiles beyond AAC-LC (including HE-AAC) are outside the native decoder's
support. AIFF is no longer advertised: the previous WAV adapter advertised it
without implementing it. Encoded files are kept in memory while playing, except
WavPack, which reads through libwavpack. Multichannel input is folded to stereo.

Load an audio file through the frontend, or start without content to open the
browser. Double-click a file to play or a directory to enter it. Pause, Stop,
Repeat and seeking work alongside software rendering. Decoder errors
appear in the player. Paths and extensions are handled as UTF-8 and extensions
are matched without case sensitivity.

The **Playback** menu offers Play song once, Repeat song, Play directory once
(the default), Repeat directory, Shuffle directory once, and Repeat shuffled
directory. Directory playback starts at the selected
song, follows the browser's filename order, and stops after the last song unless
Repeat directory is selected. The shuffle modes randomize remaining songs
without duplicates; repeating starts a new shuffled pass. Selecting a song with
shuffle enabled starts a full pass with that song first.

The playlist keeps the directory where playback began, including folders inside
archives, while you browse elsewhere. Subdirectories and archive containers are
not queued. Track changes load in the background. Stop cancels the playlist;
unreadable songs stop playback and show the error. Repeat song retains the
existing native module/game-music loop behavior.

The player shows **artist [album] songtitle**, omitting missing artist/album
fields and falling back to the original filename when no title is available.
Streamed formats use `audiotags` for ID3, Vorbis/Opus comments, FLAC, MP4,
APEv2 and WAV INFO tags; MOD/S3M/XM/IT titles also use `audiotags`.
Emulated formats use their native metadata, including GME author/game,
VGM GD3, SID author and PSF-family artist/game tags. Archive members retain
their tags and original filenames, and changing tracks updates the display.

Future Composer uses the pinned rePlayer engine documented in
[`src/deps/replays/FutureComposer/UPSTREAM.md`](src/deps/replays/FutureComposer/UPSTREAM.md).
It supports memory-backed single-file playback, duration, seeking, native repeat
and subsongs where the format provides them. Amiga names such as `fc.song`,
`fc14.song` and `smod.song` are recognized alongside normal extensions.
Native title/artist/game metadata is displayed when available; appended APEv2
or ID3v1 title/artist/album tags override it. Raw FC14 and SMOD have no native
title or artist fields, so untagged modules use the filename. Multi-file modules
load sample companions through the background worker, including archive fallback.

Ken Silverman playback supports duration, seeking, repeat and appended APEv2/ID3v1
tags. KSM, SM and SND load directly from memory. KDM requires `waves.kwv` beside
the song (or in the same archive directory); the bank is buffered during
background loading, so playback and seeking do not access disk. Each decoder
has its own engine state. See [`Ken/UPSTREAM.md`](src/deps/replays/Ken/UPSTREAM.md)
for provenance and porting details.

Double-click **ZIP, RAR, 7z or RSN** files to browse their music and folders.
**Up** returns to the parent folder or leaves the archive; nested archives
are supported up to eight levels. ZIP, RAR and 7z can also be loaded directly
through the frontend to open the browser. **VGZ** is always a playable file,
including when selected inside another archive.

Browser navigation and archive decompression run in the background. While
the browser shows **Loading browser...**, playback controls, audio and the
mouse cursor remain active. Failed navigation keeps the previous location.

The archive browser uses bundled unarr (RAR4, including solid archives; RAR5
and encrypted archives are unsupported). Open archive contents are limited to
256 MiB in total and 16384 entries per archive. Selecting a song writes a
temporary copy with a generated filename, removed when playback stops or a
new song loads. Archive paths are never used as extraction destinations.
An accompanying `yrw801.rom` in the same archive folder is available to VGM
playback. Leaving an archive does not interrupt the selected song.

Enable **Repeat** to play indefinitely. MOD, S3M, XM and IT continue through their
native restart orders and pattern loops without resetting voices at the
first-pass duration. While looping, the player shows elapsed time and a
**Restart** button. Turning Repeat off stops at the first-pass end, or after
queued audio drains if that point has already passed. Other files repeat from
the beginning.

The **Visualization** selector offers **Off**, a stereo **Oscilloscope**, and
**Spectrum bars** (32 logarithmic frequency bands, about 22 Hz to 20 kHz).
Press **V** to cycle views. Both views use the actual 44.1 kHz output accepted
by the frontend, including silence during pause. Stop, seeking and track
changes clear the display history.

## Game music and SPC archives

VGM and GME sources are vendored from the requested
[rePlayer revision dd76fe7](https://github.com/arnaud-neny/rePlayer/tree/dd76fe7cf8ec9109035cb510726bbb315e259316/source/Replays).
Their `UPSTREAM.md` files record source locations, build choices and licenses.
VGM/VGZ playback uses libvgm's chip engines; SPC and the other game-music
formats use GME. Tracks requiring an external OPL4 sample ROM expect
`yrw801.rom` beside the VGM file.

Loading an RSN directly through the frontend plays its first track and also
opens its contents in the browser. Double-clicking an RSN in the browser
opens it for individual SPC selection. For direct RSN playback, members are read into memory,
sorted by archive path, and shown in the **Track** selector. Use **Previous
track** and **Next track** to change songs; the same controls select subsongs
in NSF and other multi-track GME formats. Track changes preserve Pause and
Repeat, reset position and clear the visualization. Repeat applies to the
selected track; other tracks are selected manually.

GME and VGM/VGZ play indefinitely while **Repeat** is enabled, following native
loops where available and restarting at EOF otherwise. Seeking and changing
subsongs preserve Repeat. Enabling it after the displayed duration resumes the
native engine without resetting elapsed time. GME fading and silence detection
stay disabled, so quiet passages retain their place in the song, like modules.
With Repeat off, VGM
plays once and GME uses its reported play length (150 seconds when metadata
has no duration). Seeking and titles are supported. RSN supports readable,
unencrypted RAR4 archives, including solid compression, and ignores non-SPC
entries. Direct RSN decoding stays in memory. The reader limits input and total decompressed data to
256 MiB, each SPC member to 16 MiB and an archive to 4096 SPC tracks. Solid RAR4
uses the vendored [unarr reader](https://github.com/selmf/unarr), with the same
16 MiB member limit applied to non-SPC files needed for decompression. RAR5 is
not supported; use RAR4 archives or individual SPC files.

## Build and check

Install GCC/G++, Meson, Ninja, WavPack and zlib development
files. The unarr reader is bundled. Non-Windows builds also require iconv
(provided by libc on many systems).
Neither the core nor its tests require OpenGL, SDL2 or a display server.
The old FDK-AAC and opusfile dependencies are no longer needed.

```sh
meson setup builddir
meson compile -C builddir
meson test -C builddir --print-errorlogs
meson install -C builddir
```

Use a fresh build directory when replacing an old build configuration.
Installation writes the core to `compile_dir`. `./cli_build.sh` performs the
same steps without deleting an existing build directory.

The decoder tests use the supplied samples and generated WAVs to check frame
counts, mono conversion, seeking, EOF, resampling, pause/repeat, short frontend
writes and invalid input. The `libretro-software` test loads the actual shared
core and checks framebuffer dimensions, pitch, XRGB8888 colors, row orientation,
visible text, keyboard/pointer events, reset, content unload/reload and full
deinitialization/reinitialization. It also checks rejection of frontends that
do not accept XRGB8888. All tests can run headlessly.
The `software-renderer` test checks scaled clipping, textures, gradients,
large draw lists, draw callbacks, dynamic font updates and texture cleanup.
Roboto glyph pixels are checked against a bilinear reference at 1x, 1.5x and
2x scales. Additional checks cover alpha blending, antialiased rounded corners,
flipped textures, quad boundaries and triangle winding.
Set `DETONATE_SMOKE_SCREENSHOTS=1` when running `libretro-smoke` to save
`visualization-0.ppm` through `visualization-2.ppm` in its working directory.

Loop checks compare MOD/S3M/XM/IT audio across the first-pass boundary with the
native tracker and exercise live Repeat changes. Visualization checks cover
known-frequency tones, stereo phase, history resets and short frontend writes;
the software frontend test also cycles through all three views during playback.

The `game-music` test generates audible VGM/VGZ and SPC fixtures, a two-track
NSF and RSN archives. It checks playback, metadata, exact frame counts,
native loops, seeking, track switching and malformed input. The software frontend test
also loads an RSN and switches SPC tracks through the on-screen controls.
It also browses a ZIP folder, selects VGZ playback and enables Repeat on screen.
The `archive-browser` test checks ZIP, solid 7z, RAR and RSN navigation,
nested archives, safe member paths, playback and recoverable errors.
The `game-music-rar5-rejection` test checks that unsupported RAR5 archives show
a clear error; see `test/fixtures/README.md` for the fixture's provenance.

To check additional samples without modifying them, pass files or a directory
to `builddir/game-music-smoke` (`.exe` on Windows). This decodes the first three
seconds of every track, checks seeking, and crosses the first track's loop
boundary. For example:

```powershell
& .\builddir\game-music-smoke.exe 'C:\codecrack\personalcode\wtweg\compile_dir\wtftest\detonate'
```
