# The walk-through video

`make_video.sh` makes a 2:22 walk-through of the SF2 player firmware (1920x1080, 30 fps, H.264 and AAC), ready for YouTube:

```bash
video/make_video.sh video/work video/work/fm1-sf2-walkthrough.mp4
```

It needs gcc, Node.js with Playwright, Python 3 with Pillow and numpy, and ffmpeg with libx264. The whole run takes a few minutes.

## How it is made

- **Screen and sound come from the firmware itself.** `fm1_video.c` builds the firmware's own sources (`sf2player.c`, `fm1b.c`,
  `sf2fx.c`, `sf2out.c`, `song.c`, `transfer.c`) unchanged for the PC, with a virtual FM-1 around them: the screen, the panel's
  buttons, knobs and LEDs, the millisecond clock, the sample flash and the audio interrupt. The firmware's MIDI file player
  plays the song, so the soundtrack is what the FM-1 would play.
- **The bank** is `soundfonts/1mgm.sf2` converted with `make_bank.js` to BFP8 at 32 kHz (129 instruments, 453.7 KB).
- **The music** is `MIDIs/Eine-Kleine-Nachtmusik-2.mid` (Mozart, K. 525), turned into a song by `fm1_song.js`. It plays from the
  start, and fades out at the end of the video.
- **The panel** is `docs/img/panel.png`, with the firmware's screen set into it, knobs that turn, buttons that light up when
  pressed, and the LEDs the firmware sets.
- **The converter and install pages** are screenshots taken by `screenshots.js` in a headless browser.
- **`make_video.py`** holds the timeline: what is pressed and turned when (`ACTIONS`), the camera (`CAMERA`), the captions, the
  boxes around lines of the screen, and the title cards. Change those lists to change the video.
  `--preview SECONDS` renders only the start, for a quick look.

The CPU load on the screen comes from a model of the FM-1's audio interrupt (3 % plus 2.1 % a voice): the PC renders far faster
than the FM-1, so its own timing would say nothing. The info page is not shown, because there is no firmware image here to take
its CRC32 from.
