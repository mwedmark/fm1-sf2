# minimp3

`minimp3.h`: [lieff/minimp3](https://github.com/lieff/minimp3) at commit `ea99364f61c14656440e8d77e9c233ccf3124633`, SHA-256
`57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad` as fetched. **Changes** (marked `fm1-sf2`; without the defines below the file behaves as upstream):
(2026-11-08) in `mp3dec_decode_frame`, `scratch` is `static` when `MINIMP3_STATIC_SCRATCH` is defined; (2026-11-11) three trims for the FM-1, which decodes one mono MPEG-1 stream it made itself, and
which `live_cache/mp3_sound.h` turns on: `MINIMP3_MONO_ONLY` (state and scratch for one channel, no stereo code; a stereo frame is refused), `MINIMP3_MPEG1_ONLY` (no MPEG-2/2.5 paths;
such a frame is refused) and `MINIMP3_FRAME_START` (no sync search or free-format detection: the stream is cut at frame starts; a bad header is refused). A refused frame gives
`frame_bytes` 0. Measured with clang for Thumb2, `-Oz`, `-fno-inline-functions`: code and tables 17 170 → 15 796 bytes, RAM (the static scratch) 16 236 → 12 972 bytes, `mp3dec_t` about 6 700 → 5 500.
`node test_sound_cache.js` checks that the trimmed decoder is as before (every sound against ffmpeg's decode, 41.1 dB at worst) and that the frames it no longer takes are refused. License: CC0 1.0 Universal (`LICENSE`, public domain dedication).

Used by `app/mp3_sound.c` to decode one sound of an MP3-packed sound bank into BFP8 (FORMAT.md, "Calculation: a live cache of decoded sounds").
Built with `MINIMP3_ONLY_MP3`, `MINIMP3_NO_SIMD` and `MINIMP3_STATIC_SCRATCH`. It needs only `memcpy`, `memset` and `memmove`, no heap and no libm. Two things to know on the FM-1:

- It decodes with 32-bit floats (the pi32v2 has a single-precision FPU; whether the OS saves the FPU registers when it switches tasks is not checked here).
- Upstream, `mp3dec_decode_frame` keeps its working data on the stack, about 16 KB (`mp3dec_scratch_t`). The FM-1's task stacks are a few KB: with it there,
  every note whose sound had to be decoded restarted the FM-1 (2026-11-08). Hence `MINIMP3_STATIC_SCRATCH`: static, so only one task may decode. `mp3dec_t` is about 6.7 KB.
