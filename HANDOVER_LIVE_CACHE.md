# Handover: the live cache (branch `claude/live-cache`)

For the next session, which should run **locally on the Windows machine** (`D:\git\fm1-sf2`, JieLi toolchain `D:\JL\pi32`, PowerShell, node, ffmpeg) so it can build, flash and measure.
Written 2026-11-08 by the cloud session, which had no Windows toolchain. Everything below that is not marked *measured on the FM-1* was done on WebAssembly or read from build output.

## The idea

The flash holds the sound bank as **one MP3 stream** (64 kbit/s mono, 32 kHz, bit reservoir off). A note whose sound is not in a **cache in SRAM** has it decoded into **BFP8**
(33 bytes per 32 samples, the format the engine plays in place); the least recently used sounds that no voice plays are dropped. A few sounds that MP3 handles badly stay BFP8
in the flash (the "lossless tier"). Goal: 4 MB SoundFonts (`4gmgsmt.sf2`, 128 melodic presets + kit 0 = about 430 KB as MP3) in the 456 KB the flash has.
Design, numbers and caveats: `FORMAT.md`, "Calculation: a live cache of decoded sounds" and "Built so far". Listening was done by the user: "almost like uncompressed", the real pipeline's songs "all correct", Under the Sea has more treble and reverb that rings longer than the original.

## What is built (all on branch `claude/live-cache`, pull request not opened)

| What | Where | Test |
|---|---|---|
| minimp3 decoder into BFP8, loop crossfade | `fm1_open/live_cache/mp3_sound.c/h`, `fm1_open/third_party/mp3/` (CC0, unmodified, pinned) | `node test_sound_cache.js` (22 checks) |
| the cache (pool, best fit, LRU, pins, decode queue) | `fm1_open/live_cache/sound_cache.c/h` | same |
| converter side: packed bank writer | `pack_bank.js in.fm1b out.fm1b` (needs ffmpeg with libmp3lame) | `node test_pack_bank.js` (8 checks) |
| engine side | `fm1_open/app/fm1b.c` under `#if FM1B_PACKED` (+ `fm1b.h`) | `node test_packed_engine.js` (14 checks, 1 min with the 4 MB font) |
| firmware build switch | `fm1_open/sdk/Build.ps1 -LiveCache [-CacheKB 96] [-CacheFrames 2]`, `build_fw.ps1 -LiveCache`; pool in `fm1b_bank.c`, `fm1b_cache_service()` in `sf2player.c`'s `groove_screen_control` | none (Windows only) |
| link-map tool | `node map_sizes.js fm1_open\build\dx7\sdk.map` | |

The tests run on any machine with a clang for wasm32 and ffmpeg (`FM1_CLANG` can name it). The packed bank format is version 2, flags `0x18`, pack table per `FORMAT.md`; one entry per sound,
codec 2 (MP3 from a frame boundary, `delay` samples to throw away) or codec 0 (stored BFP8). The engine refuses a packed bank unless `-DFM1B_PACKED=1` and a pool is attached;
the plain engine refuses it too.

### How the engine uses the cache (important if you change it)

The audio interrupt never touches the cache. A note on an MP3 sound puts a request in a ring; `fm1b_cache_service()` (control task, once a cycle) pins and decodes, and answers in a grant ring;
the interrupt takes it at the start of the next block, or hands the pin back through an unpin ring. A voice with no grant, or that would read past the groups decoded, **stands still**
(nothing of it runs, not its filter: the filter's first block quantises differently if it runs early; found by the test). A stored sound plays at once. Bit-exact against the plain engine,
one block (4 ms) later. Known weakness: the voices of one note with stacked layers start when their own sounds are ready, one by one.

## Build state (measured, 2026-11-08, on the user's machine, firmware `de0de03`)

- Layout limit for the app image: **453 921 bytes** (`sdk/Package.cs`, `FIRMWARE.md`).
- Base (no `-LiveCache`): **440 284** bytes, 13 637 to spare.
- `-LiveCache` at `de0de03` (BFP8 only, no SDRAM test, minimp3 compiled with LTO): **454 364**, **443 over**. The cache costs 14 080 bytes.
- Runtime heap with a 96 KB pool: **111 500 bytes**, so about 206 000 bytes of SRAM are free before the pool (the build stops under 49 152). The heap may be used by other things at run time: not checked.
- `-Oz` does nothing under `-flto` here (three builds came out to the byte alike, after two changes that were not in the user's checkout; see below). Commit `4d4b284` compiles `live_cache/*.c`
  as native objects (`-fno-lto -Oz`); **its result is not known yet**: the user built an older checkout (wrong local directory) each time since `de0de03`. First thing to do: `git log -1` must say
  `4d4b284` (or later), then `.\build_fw.ps1 -Name sf2player_live -LiveCache -CacheKB 96 -CacheFrames 2` and read the `FM1:` lines ("has N bytes to spare" is the goal).
- **Update, local session 2026-11-08 (measured on the user's machine, from `7fbfc48`):** `4d4b284` gave **454 524** (603 over), worse than LTO's figure above; all LTO now gives 460 700.
  Fixed in `Build.ps1`: only `mp3_sound.c` is native, with `-fno-lto -Oz -fno-inline-functions`; `sound_cache.c` stays LTO. **`-LiveCache` now 453 788 bytes, 133 to spare**, it links, and
  `dist\sf2player_live.fwsc` is built (not flashed). Base now 440 348 (13 573 to spare). Other variants tried: `-Os` 1 019 over; no unrolling, `-inline-threshold=0` or the link's merge
  options, each 507 over; both files native with no inlining, 37 to spare. Not known: how much slower the decoder is with its stages not inlined (measure K on the FM-1, step 3 below).
  133 bytes is thin; the next feature in this build will need the minimp3 trim below.
- If a link error says `mp3_sound_*`/`sound_cache_*` are undefined: the native objects were not seen by the LTO link (a comment in `Build.ps1` warns that this plugin can discard definitions
  when native SDK objects have weak defaults). Then go back to LTO objects and cut size another way.
- If still over: trim minimp3 for mono 32 kHz (stereo, free-format, intensity/MS paths and unused tables; it is CC0 but then no longer "unmodified" — update `fm1_open/third_party/mp3/README.md`
  and `THIRD_PARTY.md`), or other things in the image. `map_sizes.js` shows sections but LTO merges the application into one object, so names are lost.
- `sdk/manifest.json` pins the SHA-256 of `sdk/Build.ps1`: **after every edit of `Build.ps1` put the new hash in the manifest** (`Get-FileHash`, lower case; the repo file has LF line ends and the hash is of those bytes) or the build stops with "SDK payload changed".
- **2026-11-10:** the bank check (cmd 9 in `sf2_upload.c`) made `-LiveCache` 187 over; `read35` and `crc32_flash` noinline and a shorter check brought it to
  **453 916 bytes, 5 to spare**. The next feature in this build needs room found first (the minimp3 trim below).
- `-LiveCache` implies `FM1_MIDIPLAY=0` (since the merge of main, 2026-11-08: the MIDI file player made the image 2 235 bytes over; now 229 to spare) and `FM1B_BFP8_ONLY=1`: the firmware plays BFP8 and packed banks only (SEA, 8/12/16-bit refused) and the info page has no SDRAM test (no room).
- ~~Compiler warnings expected: `-Wframe-larger-than` for minimp3 (16 KB) and the crossfade (4.2 KB).~~ **Measured on the FM-1, 2026-11-08:** with `-LiveCache` and the packed bank, every note restarted
  the FM-1 (the BFP8 bank played fine). Cause taken to be the stack (no warning is left; the SDK's stacks seen in the map are 4 KB): minimp3's scratch (`MINIMP3_STATIC_SCRATCH`, a four-line change
  to `minimp3.h`), `pcm[1152]` and `blended[1024]` in `mp3_sound.c` are now static. Image 453 596 (325 to spare), heap 88 812. Tests on the PC pass; **on the FM-1 not yet tried.** If it still
  restarts: next suspect is the FPU registers across tasks (minimp3 uses floats), not checked.
- **Then the FM-1 did not boot at all** (2026-11-08, `-CacheKB 96` with the static buffers: heap 88 812; no USB device in Windows, so neither install.html nor
  the workbench can reach it; it needs the Transporter/ROM boot). Reproduced in fm1-emu: that build shows a blank screen, no sound and 320 % load; `-CacheKB 72`
  (heap 113 388) boots and plays the packed bank there (`mt.script`), as does the base build. So the SDK needs more than 88 KB of heap at boot; 111 468 booted on the
  device. **Before flashing a build, boot it in fm1-emu** (`start_emu.ps1 -Tap`; check `Running sf2player.fwsc?v=` is the new build: until this date the catalog's
  relative address was not updated and the page could run a kept older package). The emulator did not show the stack crash (that test may have run a kept package).
  Wanted by the user: a failsafe. **Built:** safe mode, OCT- and OCT+ held at power-on (FIRMWARE.md, "Safe mode"); checked in fm1-emu and **on the FM-1** (2026-11-08,
  `sf2player_live72.fwsc`, `-CacheKB 72`): safe mode works, and a normal start plays the packed bank (`gm_32k_packed.fm1b`). Listening on the FM-1, MIDI playback
  included (the user): "sounds very good", about the same as the unpacked BFP8 bank. Step 3 below (measurements) is next.
  The heap check in `Build.ps1` is now 111 468. `fm1_device_recovery()` exists in the SDK, undocumented and not used (it may enter the loader; try it in fm1-emu first, never first on the FM-1).

- **2026-11-11 (local session, branch `claude/live-cache-stats`, measured on the user's machine):** minimp3 trimmed for mono 32 kHz MPEG-1 layer 3
  (`MINIMP3_FM1_MONO_32K`, see `fm1_open/third_party/mp3/README.md`): `-LiveCache` 451 612 bytes, **2 309 to spare** (was 5); `test_sound_cache.js` gives the same
  figures with and without it, `test_pack_bank.js` and `test_packed_engine.js` pass. Then the cache's counters and timing on the info page (two lines, `#elif FM1B_PACKED`
  in `sf2player.c`, read in FM1_TEST.md section H): **452 924 bytes, 997 to spare**, heap 113 260 at `-CacheKB 72`. Base build 439 516 (14 405 to spare).
  `dist\sf2player_live72.fwsc` (CRC32 `4549A7A9`) booted in fm1-emu with `gm_32k_packed.fm1b` (`test_emu\live_cache.script`): `CACHE H7 M4 E0 F0 L0 P0 W0`,
  `MP3 45 FR 1800 US/FR CYC 10 MAX 42 MS` (emulator timing, not the device's). **Not flashed on the FM-1 yet**: that is step 3's measurement (FM1_TEST.md H1-H4).
  A song in fm1-emu (60 s of Never gonna give you up, `-CacheKB 72`): **8 notes lost**, 144 sounds dropped, 159 decodes, 2 191 µs per MP3 frame (K ≈ 15), control
  cycle 11 ms (longest 43); the audio interrupt's CPU the same as with the BFP8 bank (FM1_TEST.md H). So the pool, which the heap holds to about 72 KB, is
  the limit: next, find SRAM for a bigger pool (`-CacheKB 128` gave 0 dropped on the PC for ABBA) or decode more frames a cycle (`-CacheFrames`).
  Bigger pools in fm1-emu (heap check lowered for the test only, not committed): **84 KB** (heap 100 972) boots, same song: `H1696 M89 E73 F275 L4`, half the lost
  notes; **90 KB** (heap 94 828) and **96 KB** (88 684) do not (blank screen, 306 % load). So the SDK needs between 94 828 and 100 972 bytes of heap at boot in the
  emulator; the device has only been seen to boot with 111 468, so 84 KB is untried there (`dist\sf2player_live84.fwsc`, built with the check lowered). More pool
  than that needs SRAM from elsewhere: the live cache's own buffers are 16 KB minimp3 scratch, 4 KB crossfade, 2.3 KB PCM, about 6.7 KB decoder state; the
  crossfade's 4 KB could share the minimp3 scratch (never in use at the same time; not checked), and mono-only minimp3 state would save about 3.4 KB.
- **2026-11-11, later: RAM freed, 84 KB pool within the proven heap.** `-CacheFrames 4` at 72 KB did not help (`L10`), so the pool is the limit, not the decoder's
  speed. Then: the crossfade's blended samples are int16 and share one 2.3 KB buffer with the decoded frame (`mp3_sound.c`, was 2.3 + 4 KB); minimp3's state and
  scratch hold one channel and a 1440-byte frame under `MINIMP3_FM1_MONO_32K` (now set in `mp3_sound.h`, since it sizes `mp3dec_t`); the cache's table has one
  entry per KB of pool in the firmware (`SOUND_CACHE_ENTRIES`, 160 in the tests). Heap at 72 KB 113 260 -> 124 140 (10 880 freed). **`-CacheKB 84` now builds
  with the normal check** (heap 111 500, image 997 to spare): `dist\sf2player_live84.fwsc`, CRC32 `24D0C78C`. Same song in fm1-emu: `H1696 M89 E73 F276 L4 P0 W0`,
  as the 84 KB test with the check lowered: the smaller table cost nothing. All three PC tests pass with the same figures. **Not on the FM-1 yet.**
  More would need SRAM from the SDK's own buffers (effects 49 KB, Bluetooth audio 16 KB, groove engine) or the 4 KB `groove_project` scratch in `sf2player.c` (who uses it: not checked).
  **On the FM-1, 2026-11-11** (`sf2player_live84.fwsc`, CRC32 `24D0C78C`, installed with `install.html` from another computer with `gm_32k_packed.fm1b`;
  install.html needs the plain build as `firmware` and the live one as `--live-fwsc`, else it refuses an MP3 bank): it boots (heap 111 500) and plays. The info page
  after the user played (photo): `CACHE H1990 M27 E19 F0 L0 P0 W0`, `MP3 144 FR 6493 US/FR CYC 10 MAX 214 MS`. **No note lost.** The decoder is about 3 times
  slower than in fm1-emu: 6.5 ms per MP3 frame (36 ms of audio), so K is about 43 at 240 MHz with the audio interrupt's share in it (the design assumed 5 to 12):
  a sound decodes at about 5.5 times real time. The longest control cycle, 214 ms, is not explained (a decode is 2 frames, 13 ms); played (the user):
  REC first, then Never gonna give you up for 31 s and some of Eine kleine Nachtmusik, so fewer new sounds than the emulator's 60 s (27 decodes
  against 89; some sounds may have been in the cache from before REC). CPU and DROPPED figures not taken. Next: those, and a longer song with many new sounds, where the slower decoder may make notes late.
  **Live figures over MIDI** (2026-11-11): the `-LiveCache` build sends all its figures once a second as SysEx to USB (`FM1_STATS_MIDI`, RUNNING.md 3d);
  `stats.html` shows them and posts them to `dist\stats_log.jsonl` here (the `-Lan` server). Image 453 212 bytes, **709 to spare**; heap unchanged.
  `dist\sf2player_live84.fwsc` is now this build (CRC32 `E3468A2D`), packed in `dist\install` with the plain build. Boots and plays in fm1-emu; the MIDI
  output itself is not checked yet (the emulator has none).
  **The whole of Never gonna give you up on the FM-1 over MIDI** (2026-11-11, `stats.html` from the computer the FM-1 is on, log here in `dist\stats_log.jsonl`,
  255 messages, 214 s playing): 6 627 hits, 426 decodes, 407 sounds dropped, **373 no room, 25 notes late**, 2 482 MP3 frames. The late notes come in the busy
  parts (s 89-92, 132-157, 208-238) and always with no room: the pool is full of sounds that sounding voices pin (up to 26 pins, 31 of 32 voices), so at 84 KB the
  pool is the limit again for a whole song, not the decoder. 6.8 ms per frame **with the audio interrupt's time in it: the interrupt takes 63 % (median, peak
  83 %) while the song plays**, so the decoder itself takes about 2.5 ms a frame (K about 15, like fm1-emu; the 6.5 ms of the info page was not three times slower
  after all). At most 59 frames decoded in a second (1.7 times real time). The control cycle: 10 ms idle, 33 ms median while playing, and **about 90 ms (up to
  128) once every second** even when nothing is decoded: something else in the control task (the screen?) holds it, which slows how fast a new sound is
  ready. The voice limit: DROPPED held 502, tail 5 131, CPU guard 3. Next, in order: what the 90 ms cycle is; then RAM or fewer pins (a voice pins its sound
  for as long as it sounds) for the pool.
  **The 90 ms cycle is the screen** (message version 2 adds the longest control step and screen update a second): playing the song on the program page, the
  longest screen update was 75 ms (median a second; 1 ms idle), nearly all of the step's 79 and the cycle's 90. The program page shows the last note's
  zone, keys, sample, loop and envelope, so a new note changed 7 or 8 lines at once, about 10 ms each with the interrupt at 63 %. Now a screen update draws at most
  2 changed lines, in turn (`line_show`, `LINES_PER_UPDATE`; a cleared page draws whole; the plain build too). Same song on the FM-1 (`95FCED19`): longest
  cycle 90 -> 55 ms, screen 75 -> 39, cycle average 33 -> 26 ms. **The late notes did not go down** (25, 30, 33 in three runs; no room 373, 508, 660): they are
  the pool's, not the control task's. Two lines still take about 40 ms (each line is drawn and sent the full 240 pixels wide). Logs: `dist\stats_log_astley_v1.jsonl`,
  `_v2.jsonl` and `_v3.jsonl` (not in git). Next for the late notes: when there is no room, let a new note take the room of a sound that only releasing
  voices hold (cut their tails, as the voice limit does), rather than wait.
  The song scripts now end with all notes off (`test_emu\astley60_rec.script`, made by `midi_script.js` plus REC first and CC 123/64 at 63.5 s).

## Not done, in the order I would do it

0. (2026-11-11) Steps 1 and 2 are done (the build fits, the packed bank plays on the FM-1, 2026-11-08). Step 3 now has its page: flash `dist\sf2player_live72.fwsc`
   and run FM1_TEST.md section H; the user's figures go into `FORMAT.md` and here.

1. Get the `-LiveCache` build under the limit (above), flash `dist\sf2player_live.fwsc` (keep `fm1_open\sdk\firmware\FM-1_015.fwsc` at hand), and check it still boots and plays an ordinary bank... **but it refuses non-BFP8 banks**, so use a BFP8 bank (`node make_bank.js bfp8 32000 1mgm.sf2 dist/gm_32k_bfp8.fm1b`).
2. Make a packed bank: `node pack_bank.js dist/gm_32k_bfp8.fm1b dist/gm_32k_packed.fm1b` (`1mgm.sf2`: 241 520 bytes, 120 MP3 sounds, 45 lossless). Flash it as any bank.
3. **Measure on the FM-1:** the decoder's speed (K, million cycles per second of mono audio; the design assumes 5 to 12; the cache must decode 20 to 48 times real time) and the control task's cycle time (it decodes `-CacheFrames` MP3 frames per cycle),
   the latency of a note whose sound is not cached, notes lost with a 96/128/160 KB pool (`-CacheKB`; the heap allows about 72 KB, see above), the CPU load against the BFP8 bank in flash. The info page shows the cache's counters, µs per MP3 frame and the control cycle since 2026-11-11 (FM1_TEST.md H).
4. For the 4 MB font: `make_bank.js` writes all 138 presets (about 516 KB packed, 50 KB over); it needs an option for the 128 melodic presets + kit 0 (what the calculation used, 429 KB) — `index.html`'s `buildBank` takes a preset index list; `test_sound_cache.js` shows how to pick them.
5. Listening: re-render and listen to the 4 MB font from the real packed file on the device (the earlier tests were simulated on the PC).
6. Layered notes starting together; note-off while a voice still waits for its sound; a web page description of the bank format and the cache (the user asked "later").
7. Open a pull request from `claude/live-cache` when the user asks (PR #6 on `claude/laughing-turing-4md83a` is the earlier work and is ready for review).

## Facts about the hardware that the design rests on

The FM-1 is an **AC7911B8**: 1 MB flash in the package, **no SDRAM** (measured 2026-11-08: the SDRAM test restarted the FM-1, nothing answers at 0x04000000; the test is now removed from every build; public teardowns; the info page's SDRAM test was removed from the `-LiveCache` build but is in the normal one). SRAM is what there is. See `FORMAT.md`, "Proposal: packed sample data" and the update box.

## Working notes

- The user writes Swedish; docs and code comments are English. Keep `FORMAT.md`, `FM1_TEST.md`, `RUNNING.md`, `README.md` current. Commit trailers: see the repository's last commits.
- The user's checkout has `soundfonts/` (large SoundFonts, staged by mistake) — ignore it; `.gitignore` has `*.sf2`.
- Do not claim a build result without the user's output; the cloud session could not run `Build.ps1`.
