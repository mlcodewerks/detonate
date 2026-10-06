# AdLib / AdPlug

Vendored from [rePlayer's AdLib backend](https://github.com/arnaud-neny/rePlayer/tree/8b0f10fd39846746a872cf3b42b25f495ac1a2a9/source/Replays/AdLib),
revision `8b0f10fd39846746a872cf3b42b25f495ac1a2a9`.
Upstream source notices and the rePlayer frontend reference files are retained.
AdPlug and libbinio use LGPL 2.1 or later; Nuked OPL3's license is in `adplug/nukedopl.c`.
See also `../REPLAYER-LICENSE`.

Meson builds the format loaders, libbinio and per-instance Nuked OPL3 emulator.
The rePlayer UI, hardware OPL driver and alternative emulators are not built.
Detonate's adapter provides stereo float PCM, format/extension enumeration,
native subsongs, metadata, duration, deterministic seek and native repeat.
Single files load directly from memory; loaders requiring companion files use
the existing private archive companion directory fallback.

The harmonic stereo option builds `surroundopl.cpp`. Its transposition logic is
restricted to the nine valid OPL channels, and initialization resets the selected
chip. The adapter renders blocks of at most 1024 frames to respect its buffer size.
