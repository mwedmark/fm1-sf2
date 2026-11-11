# minimp3

`minimp3.h`: [lieff/minimp3](https://github.com/lieff/minimp3) at commit `ea99364f61c14656440e8d77e9c233ccf3124633`, SHA-256
`57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad` as fetched. **Two changes**, each under its own define and marked `fm1-sf2`; without the defines the file behaves as upstream:

- `MINIMP3_STATIC_SCRATCH` (2026-11-08): in `mp3dec_decode_frame`, `scratch` is `static`.
- `MINIMP3_FM1_MONO_32K` (2026-11-11): `hdr_valid` takes only MPEG-1 layer 3, mono, 32 kHz, not free format (what `pack_bank.js` writes), and the header tests
  that follow from it are constants, so the stereo, MPEG-2/2.5 and free-format code drops out; the scale factor band tables keep the 32 kHz row only, the
  bitrate table the layer 3 row. Any other frame is refused (`frame_bytes` 0). It saved 2 304 bytes of the `-LiveCache` image (5 to spare before, 2 309 after).
  `test_sound_cache.js` gives the same figures with and without it (every sound against ffmpeg: worst 41.1 dB, mean 43.1 dB; the song's hits and decodes). License: CC0 1.0 Universal (`LICENSE`, public domain dedication).

Used by `app/mp3_sound.c` to decode one sound of an MP3-packed sound bank into BFP8 (FORMAT.md, "Calculation: a live cache of decoded sounds").
Built with `MINIMP3_ONLY_MP3`, `MINIMP3_NO_SIMD`, `MINIMP3_STATIC_SCRATCH` and `MINIMP3_FM1_MONO_32K` (the web page's decoder, `mp3_decoder_web.c`, does not use the last). It needs only `memcpy`, `memset` and `memmove`, no heap and no libm. Two things to know on the FM-1:

- It decodes with 32-bit floats (the pi32v2 has a single-precision FPU; whether the OS saves the FPU registers when it switches tasks is not checked here).
- Upstream, `mp3dec_decode_frame` keeps its working data on the stack, about 16 KB (`mp3dec_scratch_t`). The FM-1's task stacks are a few KB: with it there,
  every note whose sound had to be decoded restarted the FM-1 (2026-11-08). Hence `MINIMP3_STATIC_SCRATCH`: static, so only one task may decode. `mp3dec_t` is about 6.7 KB.
