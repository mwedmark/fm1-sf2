# Plan: the second core and a clock setting (2026-11-05)

Research only: nothing here is built, and nothing has been run on the device. Sources read on 2026-11-05:
[hugelton/Felucca](https://github.com/hugelton/Felucca) (latest release v1.7.0, 2026-10-28; `firmware/src/fx.c`, `core.h`,
`console.c`, `BUILDING.md`, `CREDITS.md`) and its hardware layer [hugelton/felucca-bsp](https://github.com/hugelton/felucca-bsp)
(commit 41a6e86; `chip/fm1_cpu1.h`, `chip/fm1_clock.h`, `docs/CPU.md`). Where something is not in these sources it is marked
**unknown**.

## 1. Facts

### The second core exists and Felucca uses it
- The chip (JieLi AC79 family, PI32v2) has a second CPU, CPU1. Felucca builds it in (`FELUCCA_CPU2`, default 1) and switches it
  on at run time with MENU > SYSTEM > 2ND CORE, **OFF by default**. With `FELUCCA_CPU2=0` the code and RAM are the one-core ones.
- The work is split per *part* (a synth track): CPU0 hands CPU1 a job (`fm1_cpu1_run(fn, arg)`), renders its own parts, then waits
  (`fm1_cpu1_wait()`). `fx.c` `mix_split` picks the split of the last block's measured costs that leaves the busier core the least.
  Each core has its own render state and its own dry/send sums (`mx[0]`, `mx[1]`), added together afterwards: the cores share nothing
  while they render.
- API (`fm1_cpu1.h`): `fm1_cpu1_start()`, `_run()`, `_wait()`, `_busy()`, `_check()`, `_stop()`, `_sync()`, status in `fm1_cpu1_on`,
  `fm1_cpu1_gave_up`, `fm1_cpu1_stack_used()`.
- Start: the SDK's EnableOtherCpu sequence (CPU1 held, its interrupt bank off, entry address into the word at `0x01C7FFF8`,
  bits in `0x10008` and `C1_CON` `0x1EEE004`), at power-on **before** audio and timers run, with interrupts off. The idle loop is in RAM
  (`.c1_text`), the job runs from flash.

### It is not fully reliable (Felucca's own findings)
- On **some units** CPU1 now and then reads shared RAM wrong (e.g. `0x00200000` where there is 0), more often under load
  (melodee#17, fm1-x0x#10, measured by Jangada). Felucca runs CPU1 only on a job whose *check word* (function, argument, job number)
  matches, and treats a misread, a fault, a job later than `FM1_CPU1_TIMEOUT_US` (8 ms) or a silent start as a failure:
  default is a **restart** (the boot guard goes to safe mode after two failed starts); `FM1_CPU1_FALLBACK 1` retires CPU1 and
  carries on on one core, barred until power-off.
- `docs/CPU.md`: on the device, 64-bit multiplies (`r9_r8 = r3 * r15`) among 16-bit loads occasionally leave a register one bit off
  and crash the firmware (found at 360 MHz, in an AAC decoder; fix: keep them in `noinline` helpers; "whether the SDK's 320 MHz is
  clear of it" is **unknown**). Our DSP must be checked for this pattern before any clock change.

### The clock: not 240 MHz
- `fm1_clock.h` (read off FM-1s on 2026-10-23): under **M-VAVE's bootloader the CPU runs at 360 MHz** (PLL1 = 24/12*270 = 540 MHz,
  system clock 2/3 of it, `0x10014[7:0] = 6`, HSB /2, LSB /3 = 60 MHz); under **m-bootloader 1.2 the same unit ran at 320 MHz**
  (`0x10014 = 2`, PLL1 off), so every firmware was 11 % slower and the peripherals' clock (LCD SPI, UART dividers, set for 60 MHz)
  was 53 MHz. Felucca's start therefore moves the clock to the stock 360 MHz if it is not (`fm1_clock_stock()`, from RAM, interrupts
  off, steps taken from m-bootloader's `hal_clock_a`).
- Our emulator's "240 MHz" (`&mhz=240`, "The real FM-1 runs at 240 MHz" on its page) does **not** match these figures. **We do not
  know what the real device runs at under our firmware.** All our load/CPU numbers are in emulated 240 MHz.
- Consequence for the request: "up to 320 MHz" may be at or **below** what a unit already runs at (360 under M-VAVE's bootloader).
  First measure; then decide what the setting is for (probably: choose between known-good steps, 320 and 360, and show the
  measured value).

### Measured on the device (2026-11-05, package `sf2player_clock`, info page)
`SRC=2`, `LOOP=53.3/us`. Source 2 with PLL1 off is felucca-bsp's description of the 320 MHz clock (m-bootloader 1.2). The loop gives
the same answer: 53.3 turns/us x 6 cycles a turn = 320 MHz (the same loop would give 40 at 240 MHz and 60 at 360 MHz; the 6 cycles a
turn are an assumption, but 53.3 = 320/6 is exact). **So the device runs at 320 MHz, not the emulator's 240 MHz**; the unit is
33 % faster than every load figure in RUNNING.md. 360 MHz (the stock M-VAVE clock) would be 12.5 % more.

## 2. Unknowns to settle first (no code yet)
1. The real clock under our package, on the device: read `0x10014`, `0x10008`, `0x119A8`, `0x119AC`, `0x1000C` (as `fm1_clock.h`'s
   report does) and time a one-instruction loop against TIMER4 (24 MHz). Show it on the info page.
2. Whether our build (`fm1_open/sdk/Build.ps1`, JieLi toolchain, `FM1_OFFICIAL_OPEN`) can carry what CPU1 needs: `fm1_cpu1.S` (entry,
   stacks), a `.c1_text` section in the RAM code, `FM1_CPU1=1` in the fault handler, the protected word at `0x01C7FFF8`, early start
   before audio. Felucca's own build is `build.sh` + `app.ld`, not this SDK. **Unknown**; this decides the effort.
3. Licence: felucca-bsp is MPL-2.0 (file level; the CPU1 code is by Kerem Kilic/Melodee under agreement, Charles Vestal, Leo Kuroshita),
   Felucca's split follows Jangada (GPL-3.0). Check against our `LICENSING`/THIRD_PARTY before copying; the safer way is to reuse
   the start sequence with attribution and write our own split.
4. Which of our state is global/shared: the voice engine (`fm1b*.c`), the effects (reverb/chorus buses), the voice stealing and
   `DROPPED` counters, the limiter. A split needs separate state per core.

### What starting CPU1 needs, and what our build gives (read 2026-11-05, before any code)
- **Our link:** `Build.ps1` links our 20 objects with the *prebuilt* `sdk/lib/libfm1_engine.a` and a generated linker script
  (`sdk/config/linker.ld.in`), the SDK's start-up (`startup.S.o`) and its fault vectors. We can add an assembly file and new sections
  (the linker script is a template in the SDK tree: a `.c1_text` section in RAM needs a line there), but **not** change the SDK's
  exception handler.
- **The entry word:** the chip starts CPU1 from the word at `0x01C7FFF8`. Our RAM ends at `0x1C7FD4C` (RAM0, then 52 bytes of boot
  info) with `UPDATA_BEG` at `0x1C7FD80`, so `0x1C7FFF8` is in the protected top of RAM that the updater area owns. Felucca unlocks
  it (`FM1_C0_WR_LIMIT_EN`) and opens the PC limits (`FM1_PC_LIMIT0/1`) through the debug unit while CPU1 boots through the ROM.
  Those registers and the unlock sequence (`fm1__dbg_unlock`) are in felucca-bsp's `fm1_guard.h`/`fm1_sys.h`, not in our tree.
- **Faults:** CPU1's faults are raised on CPU0's exception path. Felucca hooks its own `fm1_fault_c` (`fm1_irq.h`) to hold CPU1 and
  record the fault. Ours would go to the SDK's handler: a CPU1 fault would be **an unhandled exception on CPU0** (a crash or hang).
  We would need to find the hook in the SDK's handler, or accept that a fault is a restart.
- **Stack/entry:** `fm1_cpu1.S` (32 lines): `cli`, `usp/sp/ssp` set, `reti = fm1_cpu1_main`, `rti`; 2.8 KB user stack + 256 B.
- **Size of the spike:** about 700 lines of felucca-bsp headers read, about 250 of them needed (`fm1_cpu1.h`, `fm1_cpu1.S`,
  `fm1_cpu1_stop.h`, the register defines). MPL-2.0 files: copying needs the notices kept (and the licence check in section 2).
- **Hardware risk:** the spike writes the debug unit's guards and the protected top of RAM; a mistake is a crash on the unit. The
  boot guard Felucca relies on for a unit that cannot start CPU1 (two failed starts: safe mode) is not in our firmware. The unit can
  still be reflashed (M-VAVE's loader / `install.html`), so it is recoverable, but it must be done knowingly.

### First test of CPU1 on the device (2026-11-05, `sf2player_cpu1`, info page ARP)
`CPU1 OK ALONE 2625 BOTH 2625 US`: CPU1 started from our build (felucca-bsp's sequence, our own `app/cpu1_entry.S` and
`app/cpu1_spike.c`), ran a 60 000-turn checksum job three times with the right result (checked against CPU0's own run each
time, no misread), and the job took 2 625 us on CPU0 alone and the same 2 625 us with both cores running one at the same time:
**the cores run in parallel with no measurable slowdown from the shared bus or caches** (for this RAM-only job; real voice
rendering reads sample data from the flash and will contend more). Not tested yet: CPU1 running with the debug unit's PC limits and
the top-of-RAM write guard put back (the test keeps them open), CPU1's faults, long runs, jobs that read the bank from flash.

## 3. Dual core for the SF2 player (proposal)
- **Split:** by *voices*, not parts. CPU0 renders voices 0..N/2 and the effects/limiter/I2S, CPU1 voices N/2..N into its **own** dry and
  send accumulators (like `mx[1]`); CPU0 adds them after `fm1_cpu1_wait()`. Choose N/2 by measured cost per block (as `mix_split`),
  since the heavy voices are the ones with interpolation and filter active, not the count. The effects bus stays on CPU0.
- **MIDI file player and control task** stay on CPU0 with the effects: they are what loses timing today; the point is to take voice
  rendering off that core.
- **Safety, as Felucca does:** OFF by default with a switch on the info page ("2ND CORE"), check word on every job, timeout, failure
  recorded and a boot guard; start before audio. Fall back to one core rather than restart on a live unit (`FM1_CPU1_FALLBACK 1`).
- **Test order:** (1) host: split the render in `test_core.js`/the wasm simulator logic and check the output is **bit exact** with
  one core (it must be, the sums are integers); (2) fm1-emu has no second core, so the split must also run both halves on one core
  behind a flag; (3) device: switch on, run `load32_*` and the heavy song, compare the CPU/DROPPED figures, soak test 1 h for
  misreads (`cpu2_bad`, `cpu2_gave_up` in the console as Felucca prints them).
- **Expected gain:** up to about 2x on the voice render, but unknown until measured; shared memory bandwidth and cache lines
  between the cores will take some.

## 4. Clock setting on the info page (proposal)
- Show the **measured** clock and the setting. Offer the values we know the registers for (320 MHz: `0x10014 = 2`, PLL1 off;
  360 MHz: PLL1 540 MHz, `0x10014 = 6`, per `fm1_clock.h`). Anything else (such as 280): **unknown**, the SDK's table of allowed PLL/divider
  combinations is not in these sources; do not invent steps.
- Changing it moves things that are set for 60 MHz: the LCD SPI divider, the UART dividers (**MIDI 31 250 baud**), probably the
  flash divider and the timers. Recompute them with the clock or restrict the change to a restart: save the choice in flash and
  apply it **at the start** (as `fm1_clock_stock()` does, from RAM, interrupts off) and never live.
- **Safe by construction:** default is the clock the unit boots at; a new value is tried once after a restart and kept only if the
  boot guard sees a clean start (otherwise back to the default; the existing two-failed-starts safe mode in the bsp is the model).
  Also the 64-bit multiply issue above: scan our DSP for it (functions whose disassembly has `rN_rM = rA * rB` beside `h[...]` loads)
  before going above what the unit runs at.
- Update the emulator's mhz handling to the real value once it is known.

## 5. Order of work
1. Measure the real clock and show it on the info page (small, safe, answers the biggest unknown).
2. Scan our DSP for the 64-bit multiply pattern.
3. Spike: can our build link and start CPU1 (an empty job that sums numbers), behind a flag, no audio change.
4. Per-core render state and the one-core bit-exact split on the host; then the real split.
5. The clock setting, last, only if step 1 shows there is room above the real clock.

## 6. The jobs in one audio block, and a split (2026-11-05, from the code and fm1-emu)
One block = 128 output frames = 2 902 us at 44.1 kHz (the player renders about 92 frames at 32 kHz, then resamples). The info page
(REC) now shows the average time of each stage in us: `E V L F R` (`sf2out_profile`, `fm1b_events_ticks`).

fm1-emu, 320 MHz, `load32_filter` (32 filtered voices, BFP8), CPU 35 % = 1 016 us a block:

| Stage | us | Share | Memory it touches |
| --- | --- | --- | --- |
| E: the event queue, program/control changes, note starts (`queue_drain`, `note_apply`, `gates_update`) | 0 (idle; a note start costs most) | | **flash** (preset, key map, zone, layer tables) |
| V: the voices (`voice_run`: sample decode, interpolation, envelope, LFO, filter, sends) | 848 (26.5 per voice) | 83 % | **flash** (the bank's samples, one stream per voice), SRAM (voice state, buses) |
| L: the limiter (`limit`, 64-bit multiplies) | 11 | 1 % | SRAM |
| F: reverb, chorus, pan (`sf2fx_mix_sends`) | 103 | 10 % | SRAM only (delay lines 16 KB) |
| R: the 32 -> 44.1 kHz resampler | 54 | 5 % | SRAM only (the table is a non-const array, so it is in RAM) |

At 640 MHz the same run reads CPU 17 %, V 423, F 51, R 27, L 6: exactly half. fm1-emu does not model flash wait states, so on the device V
will be larger; `sf2player_prof` shows the real figures.

Split: CPU1 takes a share of the voices (`voice_run` per voice into its own `melodic/reverb/chorus/side` buffers and its own `filter_block`);
CPU0 renders the rest, waits, adds the buffers, and does the limiter, effects, resampler, output mix, events, MIDI player and screen.
Voices are independent of each other (their own state) and the bank is read-only, so this is the natural cut; the share for CPU1 is
chosen from the measured cost per voice so the two cores finish together (about 60 % of the voices on CPU1 with the figures above:
CPU0 ~ 510 us, CPU1 ~ 510 us instead of 1 016 us on one core). The effects need the whole mix, so they stay after it (a pipeline that moves
them to CPU1 for the previous block would add 2.9 ms of delay and is not needed first). Note starts and stealing stay on CPU0 (single writer).
What must be made per core: `filter_block` (a static buffer shared today), the buses, and `voices_sounding` bookkeeping.
fm1-emu has one core only: the split is checked there by running both halves one after the other (bit exact with one core), and at
`&mhz=640` for the load (2 x the clock stands in for "perfectly parallel"); only the device shows contention, caches and misreads.
