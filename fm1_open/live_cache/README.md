# live_cache

A decoder and a cache for sounds packed as MP3 (FORMAT.md, "Calculation: a live cache of decoded sounds"). **Not in the firmware build:** `sdk/Build.ps1` compiles
every `.c` under `app/`, and this has not been built with the pi32v2 toolchain (clang 4.0.1, Windows only), so it is kept outside `app/` until it is connected on purpose.

- `mp3_sound.c/h`: one sound of an MP3 stream decoded into BFP8 a few frames at a time (minimp3, `../third_party/mp3`), the BFP8 packer, and the loop crossfade.
- `sound_cache.c/h`: a pool of RAM, best-fit gaps, least-recently-used drop, pins, a queue of decodes.
- Tests: `node test_sound_cache.js [font.sf2] [song.mid]` (the real C on WebAssembly; any clang for wasm32 and ffmpeg).

- The engine's side is in `app/fm1b.c` under `#if FM1B_PACKED` (off by default, so the firmware is unchanged): it binds banks of version 2 with the pack table, and a note whose sound is
  not in the cache waits for it. API in `fm1b.h`: `fm1b_cache_attach(pool, bytes)` before `fm1b_bind()`, and `fm1b_cache_service(mp3_frames)` from the control task every few ms.
  `node test_packed_engine.js [font.sf2]` plays packed banks against the plain engine on WebAssembly (the packed bank is made by `../../pack_bank.js`).

**Connected, behind a switch (not built yet):** `sdk/Build.ps1 -LiveCache [-CacheKB 96] [-CacheFrames 2]` (also `build_fw.ps1 -LiveCache`); see RUNNING.md, section 1. The firmware side is
`fm1b_bank.c` (the static pool, attached in `fm1b_device_init`) and `sf2player.c` (`fm1b_cache_service` once a control cycle, in `groove_screen_control`). The interrupt-side helpers of `fm1b.c` are
`FM1_DSP_CODE`, so they run from SRAM, and they read the cache's tables directly rather than call `sound_cache.c` (flash). Checked here only on WebAssembly and with a host clang: the three sources compile for
wasm32 without a warning but for the stack frames (minimp3's decode 16.4 KB, the crossfade 4.2 KB), and need nothing from a C library but `memcpy`, `memset`, `memmove`, `memcmp`.

Earlier note, for any other build: add `live_cache` and `third_party/mp3` to the include paths, the sources to the build in `sdk/Build.ps1`, and `-DFM1B_PACKED=1`; the pool
(the free SRAM), a task that calls `fm1b_cache_service` and the stack of that task (about 14 KB for minimp3's working data plus 2.3 KB for a frame of samples) are the firmware's to give.
Nothing of this has been built with JieLi's compiler.
