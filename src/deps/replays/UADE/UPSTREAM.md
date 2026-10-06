# UADE

Vendored from [rePlayer's UADE backend](https://github.com/arnaud-neny/rePlayer/tree/8b0f10fd39846746a872cf3b42b25f495ac1a2a9/source/Replays/UADE),
revision `8b0f10fd39846746a872cf3b42b25f495ac1a2a9`.
Upstream source notices, player binaries and license files are retained.
See `uade/COPYING`, `uade/COPYING.GPL`, `uade/COPYING.LGPL`, `bencode/LICENSE`
and `../REPLAYER-LICENSE`. Individual Amiga player/data licenses vary; the
upstream notices and data are distributed together here.

The UAE core has process-global state. Each Detonate decoder launches its own
`detonate-uade` helper, with local pipes on Windows or Unix sockets on POSIX. Windows output uses
overlapped reads and process-exit events, without polling a timer.
The helper runs the frontend and emulator on separate threads using a bounded
request/response protocol. Helpers have no visible window. EOF, decoder
destruction and failed loads release the process; stalled reads time out after
30 seconds. This isolates parallel playback and asynchronous replacement loads.

Meson generates `assets.h` from the pinned data and embeds it in the helper.
Install the helper beside the core. It requires no separately installed UADE
executable or data directory. Development builds can locate the build-tree helper.
The rePlayer UI and its file wrappers are not built. The port replaces those
wrappers with standard C file streams and in-memory core IPC.

Local changes fix MSVC-only packing/endian/integer declarations, C inline
linkage, Windows directory creation and missing compatibility includes.
The song database interface is disabled: Detonate does not read or write UADE
user configuration/databases. Silence detection and automatic subsong changes
are disabled. Companion requests resolve within the song directory and bundled
extras; archive files use Detonate's private companion directory.

Output is signed 16-bit stereo at 44.1 kHz, converted to float by Detonate.
Extensions and Amiga prefix filenames derive from `data/eagleplayer.conf`.
Earlier specialized decoders retain priority on shared extensions. UADE supplies
native subsongs/title, deterministic rendering-based seek and repeat. Without
a database duration, the first-pass duration defaults to three minutes.
rePlayer's customized score continues the native engine past song-end notices.
Its special container overrides (such as CUST-PKG) are not included.
