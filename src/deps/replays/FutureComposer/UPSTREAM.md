# FutureComposer / libtfmxaudiodecoder

Vendored from arnaud-neny/rePlayer, revision
`65271ed92403e750c733a5b7f02d02c52aecc43c`, directory
`source/Replays/FutureComposer/libtfmxaudiodecoder`:
https://github.com/arnaud-neny/rePlayer/tree/65271ed92403e750c733a5b7f02d02c52aecc43c/source/Replays/FutureComposer

Engine: Michael Schwendt's libtfmxaudiodecoder 1.0.14, including rePlayer's
companion-file callbacks, native tag scanning and precise mixer loop boundary
changes. License: GPL-2.0-or-later; see `libtfmxaudiodecoder/COPYING` and the
individual file notices.

Meson builds the C++ engine directly. The unused upstream C wrapper and
rePlayer's frontend/settings code are not built. The Detonate adapter uses
44.1 kHz stereo, PAL timing, full stereo separation, and no low-pass filter.
Module input is limited to 1 MiB, matching rePlayer; external sample files are
limited to 16 MiB. Raw FC14/SMOD headers receive bounds checks in the adapter.

Local changes to the vendored engine:

- `src/Filter.cpp`: use C++20's portable pi constant instead of `M_PI`.
- `src/Jochen/Probe.cpp`: check the trailing byte is in range when searching
  for a TFMX tag, and advance past nonmatching portamento candidates rather
  than repeatedly examining the same bytes.

Native title, artist and game fields are exposed as title, artist and album.
APEv2/ID3v1 tags, when present, override these fields. FC14 and SMOD themselves
do not define song-title/artist fields; untagged songs use Detonate's filename
fallback rather than inventing metadata.
