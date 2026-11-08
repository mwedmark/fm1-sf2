# minimp3

`minimp3.h`: [lieff/minimp3](https://github.com/lieff/minimp3) at commit `ea99364f61c14656440e8d77e9c233ccf3124633`, **unmodified**,
SHA-256 `57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad`. License: CC0 1.0 Universal (`LICENSE`, public domain dedication).

Used by `app/mp3_sound.c` to decode one sound of an MP3-packed sound bank into BFP8 (FORMAT.md, "Calculation: a live cache of decoded sounds").
Built with `MINIMP3_ONLY_MP3` and `MINIMP3_NO_SIMD`. It needs only `memcpy`, `memset` and `memmove`, no heap and no libm. Two things to know on the FM-1:

- It decodes with 32-bit floats (the pi32v2 has a single-precision FPU; whether the OS saves the FPU registers when it switches tasks is not checked here).
- `mp3dec_decode_frame` keeps its working data on the stack, about 14 KB (`mp3dec_scratch_t`), and `mp3dec_t` is about 6.7 KB.
