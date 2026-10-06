# PreTracker

Vendored from [retrovertapp/playback-pretracker](https://github.com/retrovertapp/playback-pretracker/tree/c8a4cdc7de3319e2f8d2e02d0b6fbd09e3cf5f4d),
revision `c8a4cdc7de3319e2f8d2e02d0b6fbd09e3cf5f4d`.
The six C player sources and headers are unchanged. The Retrovert plugin API
adapter is not built. `pretracker/LICENSE` and `LICENSES` retain the MIT license
and author attribution.

Detonate supports PRT through version 1.5, stereo 44.1 kHz output, native
subsongs, title/author, memory-backed archive playback, deterministic seek,
duration and native repeat. Duration is measured by rendering until the first
native end marker, with a ten-minute cap. That measurement is cached per subsong.
Repeat keeps the native sequencer/mixer running after the end marker.
The adapter rejects files with no waves: the native sequencer dereferences
wave zero even on silent channels.

`test/fixtures/retrovert_selftest.prt` comes from the same pinned repository.
