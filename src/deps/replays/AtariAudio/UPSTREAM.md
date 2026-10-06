# AtariAudio

Vendored AtariAudio 1.26 from arnaud-neny/rePlayer revision
`8b0f10fd39846746a872cf3b42b25f495ac1a2a9`, directory
[source/Replays/SNDHPlayer/AtariAudio](https://github.com/arnaud-neny/rePlayer/tree/8b0f10fd39846746a872cf3b42b25f495ac1a2a9/source/Replays/SNDHPlayer/AtariAudio).
Original attribution and embedded Musashi / depacker license notices are retained;
see also [../REPLAYER-LICENSE](../REPLAYER-LICENSE). No rePlayer frontend or DSP is built.

The adapter provides 44.1 kHz stereo float PCM, metadata, default subsong
selection, subsong durations, native looping and seeking by resetting and
rendering forward. Files and archive members load directly from memory.
Unknown SNDH durations fall back to 180 seconds.

Local changes:

- Bounds validation before parsing unpacked SNDH and YM, including strings,
  timing/sample tables, score data and clock rates. Limit decoded SNDH to 3 MiB
  and YM to 16 MiB, and bound allocations from packed headers.
- Check ICE backward input reads and output back references; check LZH header,
  bitstream, Huffman tree and table bounds before unpacking.
- Correct SNDH `!#SN` tag advancement and unsigned short branch offsets.
- Decode YM3b loop position as big endian and exclude it from the score length.
- Reset oscillator/noise state, effects, tracker voices and MIX playback state
  deterministically. Remove the shared random oscillator initialization seed.
- Ignore out-of-range tracker sample IDs and YM register reads.
- Clamp oversized SNDH timings and explicitly narrow mixed samples for C++20.
- Disable upstream hardware assertions and compile with wrapping signed
  arithmetic, consistent with the other bundled emulator integrations.

Musashi's opcode tables are built once during static initialization; CPU and
hardware state belong to each renderer. The bundled SNDH duration database is
retained. `test/atari_openmpt_smoke.cpp` generates YM and SNDH fixtures and checks
file/memory playback, metadata, subsongs, seeking, repeat, independent instances,
archive loading and malformed input, alongside the existing module fixtures.
