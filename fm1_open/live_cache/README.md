# live_cache

A decoder and a cache for sounds packed as MP3 (FORMAT.md, "Calculation: a live cache of decoded sounds"). **Not in the firmware build:** `sdk/Build.ps1` compiles
every `.c` under `app/`, and this has not been built with the pi32v2 toolchain (clang 4.0.1, Windows only), so it is kept outside `app/` until it is connected on purpose.

- `mp3_sound.c/h`: one sound of an MP3 stream decoded into BFP8 a few frames at a time (minimp3, `../third_party/mp3`), the BFP8 packer, and the loop crossfade.
- `sound_cache.c/h`: a pool of RAM, best-fit gaps, least-recently-used drop, pins, a queue of decodes.
- Tests: `node test_sound_cache.js [font.sf2] [song.mid]` (the real C on WebAssembly; any clang for wasm32 and ffmpeg).

To connect it: add `live_cache` and `third_party/mp3` to the include paths and the sources to the build in `sdk/Build.ps1`; the stack of the task that calls `sound_cache_step`
needs about 14 KB for minimp3's working data plus 2.3 KB for a frame of samples. Still to write: the converter's writer of the packed bank (the pack table of FORMAT.md, the stream
with the bit reservoir off, the index), and the engine's use of it (a zone points at a cache block, a voice pins its sound).
