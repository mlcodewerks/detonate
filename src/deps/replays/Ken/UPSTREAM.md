# Ken Silverman replay engine

Vendored from arnaud-neny/rePlayer revision
`ca89e1b62b462c9d54ea51f300b2b8283f668fd9`, directory
[`source/Replays/Ken/ken`](https://github.com/arnaud-neny/rePlayer/tree/ca89e1b62b462c9d54ea51f300b2b8283f668fd9/source/Replays/Ken/ken).
The upstream README and source attribution notices are retained. See also
`../REPLAYER-LICENSE`. The rePlayer frontend and its settings/UI are not built.

Supported formats: KDM (digital samples), KSM, SM and SND (Adlib synthesis).
Output is 44.1 kHz signed 16-bit stereo, converted to Detonate float PCM.
The engine's stereo synthesis is retained; rePlayer's optional surround DSP
is not included. KDM requires `waves.kwv` in the song's directory.

Local integration changes:

- The `.c` engine files are included inside C++ state classes in `backend.cpp`.
  Global/static state becomes per-instance state; Adlib cell callbacks become
  member function pointers. Every decoder owns its synth, tables and buffers.
- Platform/file-I/O includes and redundant external Adlib declarations are
  removed. Serialized/arithmetic `long` values use explicit 32-bit types,
  sample pointers use `intptr_t`, and x86 multiply intrinsics use `int64_t`.
  Signed arithmetic wrapping preserves the original engine's integer behavior.
- Loader callbacks read bounded memory buffers. KDM's bank is read once by
  Detonate during background initialization and reused from memory for seeking.
  SM/SND/KSM archive members load directly; KDM archives use the existing
  private companion-directory fallback.
- Inputs are checked for array bounds, ordered events, valid quantization,
  instrument/effect indices and bounded sample/repeat ranges before loading.
- SM rows use unsigned bytes so instrument 28/29 commands are preserved.
  SM/SND loops wrap before reading beyond the last chord. KDM handles pitch
  steps that cross more than one sample loop and bounds its filename copy.
- Seeking rebuilds the instance from retained data and renders forward for
  deterministic PCM. Repeat keeps the native engine running across its loop.
- These formats have no native title/artist fields. Appended APEv2 and ID3v1
  tags are exposed; untagged tracks use Detonate's filename fallback.

`test/ken_smoke.cpp` generates all four formats and a KWV bank, and covers
audio, duration, repeat, exact seeking, independent instances, Unicode paths,
tags, background archive loading, sample companions and malformed input.
