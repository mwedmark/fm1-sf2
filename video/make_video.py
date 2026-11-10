#!/usr/bin/env python3
"""The FM1-SF2 walk-through video: the SF2 player firmware run on the PC (fm1_video), its screen set into the panel, with title
cards and captions, and the firmware's own audio as the soundtrack (Eine kleine Nachtmusik from the song player).

    python3 video/make_video.py <work_dir> [out.mp4] [--preview SECONDS]

<work_dir> must hold gm.fm1b (make_bank.js bfp8 32000), song.fm1s (fm1_song.js), and converter.png, install.png,
switch_in.png and switch_out.png (screenshots.js); make_video.sh makes all of them. --preview renders only the first SECONDS, for a quick look.
Needs Pillow, numpy and ffmpeg with libx264."""
import math, os, subprocess, sys
from multiprocessing import Pool
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
W, H, FPS = 1920, 1080, 30
DURATION = 202.0

# ---- the timeline -------------------------------------------------------------------------------------------------------
# Panel buttons (fm1_controls.buttons bits) and encoders (fm1_controls.encoders), as sf2player.c reads them.
OCT_DOWN, OCT_UP, FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC = range(14)
SELECT, ALGORITHM, KNOB1, KNOB2, KNOB3, KNOB4, PRESETS = range(7)

def key(note):
    """The panel key (0..26, F3..G5) for a MIDI note."""
    return note - 53

# The opening of Eine kleine Nachtmusik on the keys: (eighth notes from the start, MIDI note, length in eighths).
THEME_1 = [(0, 67, 2), (3, 62, 1), (4, 67, 2), (7, 62, 1), (8, 67, 1), (9, 62, 1), (10, 67, 1), (11, 71, 1), (12, 74, 4)]
THEME_2 = [(16, 72, 2), (19, 69, 1), (20, 72, 2), (23, 69, 1), (24, 72, 1), (25, 69, 1), (26, 66, 1), (27, 69, 1), (28, 62, 4)]
EIGHTH = 0.21

def play(start, notes):
    return [(start + at * EIGHTH, 'key', key(note), round(length * EIGHTH * 900)) for at, note, length in notes]

# What the virtual hands do on the panel: (seconds, action, argument, count). fm1_video.c plays it into the firmware.
ACTIONS = [
    (0.4, 'press', PLAY, 0),
    (78.0, 'turn', KNOB3, 5),        # master reverb 30 -> 50
    (81.5, 'turn', KNOB4, 3),        # master chorus 30 -> 42
    (86.5, 'press', HOME, 0),        # the channel page
    (91.2, 'turn', KNOB1, 3),        # cursor to channel 4, the first violins
    (93.8, 'press', SEQ, 0),         # solo
    (100.0, 'press', SEQ, 0),        # no solo
    (103.0, 'turn', KNOB1, -2),      # channel 2, the contrabass
    (104.6, 'press', ARP, 0),        # mute
    (110.0, 'press', ARP, 0),        # unmute
    (112.2, 'turn', KNOB1, 2),       # channel 4 again
    (114.0, 'turn', SELECT, 33),     # 041 VIOLIN -> 074 FLUTE
    (120.8, 'turn', SELECT, -33),    # and back
    # layering
    (124.5, 'press', PLAY, 0),       # stop the song
    (128.8, 'press', EDIT, 0),       # arm row 4, the violins
    (132.8, 'turn', KNOB1, 5),       # row 9, the piano
    (133.6, 'press', EDIT, 0),       # arm it
    *play(136.8, THEME_1 + THEME_2),
    (144.0, 'press', GLO, 0),        # row 9 listens on MIDI channel 2
    (144.5, 'press', GLO, 0),        # and 3
    *play(145.4, THEME_1),
    (149.2, 'press', EDIT, 0),       # disarm row 9
    (149.6, 'turn', KNOB1, -5),      # row 4
    (150.4, 'press', EDIT, 0),       # disarm it
    (152.6, 'press', PLAY, 0),       # the song from the top
    (156.4, 'press', HOME, 0),       # the program page
]

# Safe mode is a second run of the firmware, started with OCT- and OCT+ held (SAFE_START: where it begins in the video;
# SAFE_ACTIONS: its own times, from power on). Before it the screen is dark: the FM-1 is switched off.
POWER_OFF, SAFE_START = 162.5, 165.0
SAFE_ACTIONS = [(0, 'hold', OCT_DOWN, 2500), (0, 'hold', OCT_UP, 2500), (4.0, 'key', key(67), 350), (4.6, 'key', key(71), 350),
                (5.2, 'key', key(74), 600)]

# The soundtrack's level: (seconds, gain). Silent while the FM-1 is off and in safe mode.
AUDIO_GAIN = [(0, 1), (POWER_OFF, 1), (POWER_OFF + 0.8, 0), (178.2, 0), (179.8, 1)]

# Camera keyframes in stage pixels: (seconds, centre x, centre y, width). The stage is the panel at twice its size, the screen at
# four times (one LCD pixel = 4 stage pixels); see Stage below.
FULL = (1600, 900, 3200)
LCD = (1540, 720, 2400)
LCD_BUTTONS = (1940, 730, 2500)
LCD_KNOBS = (1900, 730, 2500)
LCD_LEFT = (1380, 730, 2500)
LCD_KEYS = (1900, 930, 2800)
CAMERA = [
    (0.0, *FULL), (6.5, *FULL), (9.0, *FULL),
    (50.0, *LCD), (75.5, *LCD), (77.0, *LCD_KNOBS), (85.5, *LCD_KNOBS), (87.0, *LCD_BUTTONS),
    (111.8, *LCD_BUTTONS), (113.4, *LCD_LEFT), (123.8, *LCD_LEFT), (125.6, *LCD_KEYS), (155.8, *LCD_KEYS), (157.6, *FULL),
    (162.0, *FULL), (164.0, *LCD_LEFT), (DURATION, *LCD_LEFT),
]

CAPTIONS = [
    (7.0, 12.6, "Everything you hear is the FM-1 firmware playing Mozart's Eine kleine Nachtmusik from its own song memory."),
    (51.5, 56.3, 'The latest note, from the keys or any MIDI channel, and which zone of its preset played it.'),
    (56.5, 61.0, "That zone's key range, root note, sample length and loop."),
    (61.2, 65.2, 'Its envelope, and the level of the voice as it plays.'),
    (65.4, 69.6, 'Voices sounding out of 32, and the CPU load.'),
    (69.8, 75.2, 'The song in RAM: its name, where it plays, and how long it is.'),
    (77.0, 85.6, 'KNOB3 and KNOB4 set the master reverb and chorus.'),
    (87.0, 91.0, 'HOME opens the channel page: one row per MIDI channel. Green rows are playing.'),
    (91.2, 93.6, 'KNOB1 moves the cursor to channel 4, the first violins.'),
    (93.8, 99.8, 'SEQ solos the channel: only the first violins play.'),
    (100.0, 102.8, 'SEQ again: the whole orchestra.'),
    (103.0, 109.8, 'ARP mutes a channel: here the contrabass.'),
    (110.0, 112.0, 'ARP again brings it back.'),
    (112.2, 120.6, 'SELECT gives a channel another instrument: the first violins on a flute.'),
    (120.8, 124.2, 'And back to the violin.'),
    (124.5, 128.6, 'Layering: one key plays several instruments. PLAY stops the song first.'),
    (128.8, 132.6, 'EDIT arms the selected row: channel 4, the violins.'),
    (132.8, 136.6, 'KNOB1 goes to channel 9, the piano, and EDIT arms it too.'),
    (136.8, 143.8, 'Now the keys play both rows: violin and piano together.'),
    (144.0, 149.0, 'GLO sets the MIDI channel an armed row listens on, so a keyboard on that channel plays the layer too.'),
    (149.2, 152.4, 'EDIT again disarms a row.'),
    (152.6, 156.2, 'PLAY starts the song again from the top.'),
    (156.6, 162.2, "HOME goes back to the program page. The song plays from the FM-1's own flash and RAM."),
    (POWER_OFF, 166.6, 'If a bank or firmware ever keeps the FM-1 from starting: switch it off, then hold OCT- and OCT+ while switching it on.'),
    (166.8, 172.4, 'SAFE MODE: the bank is not used and the keys make no sound, but USB-MIDI works as usual.'),
    (172.6, 178.0, 'So the install page can put a working firmware or bank on it again. Switch off and on without the buttons to leave.'),
]

# Boxes drawn around lines of the screen: (from, to, LCD y, LCD height).
HIGHLIGHTS = [
    (51.5, 56.3, 28, 18), (56.5, 61.0, 60, 39), (61.2, 65.2, 102, 34), (65.4, 69.6, 138, 11), (69.8, 75.2, 212, 11),
    (77.0, 85.6, 156, 11),
    (128.8, 149.0, 72, 10), (133.6, 149.0, 127, 10),             # the armed rows: channel 4 and channel 9
    (166.8, 172.4, 86, 43),                                       # SAFE MODE and the line under it
]

CHAPTERS = [(50.0, 75.8, '4', 'The program page'), (76.0, 86.0, '5', 'Reverb and chorus'), (86.2, 124.3, '6', 'The channel page'),
            (124.4, 156.3, '7', 'Layering'), (POWER_OFF - 0.1, 178.2, '8', 'Safe mode')]

# Full-screen cards: (from, to, name). They fade in and out over the panel.
CARDS = [(0.0, 6.5, 'title'), (13.0, 25.6, 'convert'), (25.0, 36.6, 'install'), (36.0, 50.6, 'switch'),
         (178.2, 190.4, 'specs'), (189.8, DURATION + 1, 'end')]
FADE = 0.6

# ---- drawing helpers ----------------------------------------------------------------------------------------------------
FONT_DIR = '/usr/share/fonts/opentype/inter/'
def font(name, size):
    for path in (FONT_DIR + name, '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'):
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()

INK, MUTED, ACCENT, BG = (236, 236, 230), (160, 160, 152), (255, 224, 0), (14, 16, 20)
LCD_YELLOW = (255, 224, 0)

def smooth(x):
    x = min(1.0, max(0.0, x))
    return x * x * (3 - 2 * x)

def fade_alpha(t, start, end, fade=0.35):
    if t < start or t > end:
        return 0.0
    return min(1.0, (t - start) / fade, (end - t) / fade)

def wrap(draw, text, fnt, width):
    lines, line = [], ''
    for word in text.split():
        trial = (line + ' ' + word).strip()
        if draw.textlength(trial, font=fnt) > width and line:
            lines.append(line)
            line = word
        else:
            line = trial
    return lines + [line]

def background():
    """A dark vertical gradient with a soft glow at the top."""
    y = np.linspace(0, 1, H)[:, None]
    x = np.linspace(-1, 1, W)[None, :]
    glow = np.exp(-((x * 0.9) ** 2 + ((y - 0.15) * 2.2) ** 2)) * 26
    base = np.stack([14 + 8 * (1 - y) + glow * 0.5 + 0 * x, 16 + 9 * (1 - y) + glow * 0.8 + 0 * x, 20 + 14 * (1 - y) + glow + 0 * x], -1)
    return Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))

# ---- the stage: the panel with the firmware's screen in it ---------------------------------------------------------------
PANEL_X, PANEL_Y = 320, 80                   # where the panel (2x) sits on the stage
STAGE_W, STAGE_H = 3200, 1800
LCD_X, LCD_Y = PANEL_X + 740, PANEL_Y + 84   # the screen's top left on the stage; 240 LCD pixels = 960 stage pixels

def panel_rect(x0, y0, x1, y1):
    """A rectangle of docs/img/panel.png (1280x820) in stage pixels."""
    return (PANEL_X + 2 * x0, PANEL_Y + 2 * y0, PANEL_X + 2 * x1, PANEL_Y + 2 * y1)

BUTTON_RECTS = {OCT_DOWN: (62, 398, 174, 442), OCT_UP: (186, 398, 298, 442)}
for i in range(6):
    BUTTON_RECTS[FX + i] = (906 + 58 * i, 232, 956 + 58 * i, 296)
    BUTTON_RECTS[HOME + i] = (906 + 58 * i, 309, 956 + 58 * i, 372)
# The keys of docs/img/panel.png: white keys in the lower row, black keys in the upper one.
WHITE_X = [92 + 73 * i for i in range(16)]
BLACK_X = [129, 202, 275, 421, 494, 640, 713, 786, 932, 1005, 1151]
KEY_RECTS = []
_white = _black = 0
for _k in range(27):
    if (_k + 5) % 12 in (1, 3, 6, 8, 10):                  # F3 is key 0: the sharps
        x = BLACK_X[_black]; _black += 1
        KEY_RECTS.append((x - 31, 576, x + 31, 667))
    else:
        x = WHITE_X[_white]; _white += 1
        KEY_RECTS.append((x - 31, 681, x + 31, 786))
KNOBS = {SELECT: (251, 138, 45), ALGORITHM: (251, 298, 45), PRESETS: (111, 298, 45),
         KNOB1: (935, 122, 37), KNOB2: (1027, 122, 37), KNOB3: (1119, 122, 37), KNOB4: (1211, 122, 37)}

def clean_panel():
    """docs/img/panel.png without the emulator's marks: the green frame by PLAY and REC, and the label under the screen."""
    im = Image.open(os.path.join(ROOT, 'docs/img/panel.png')).convert('RGB')
    a = np.array(im).astype(int)
    region = a[296:380, 1130:1250]
    left = a[296:380, 1130 - 116:1250 - 116]
    green = (region[..., 1] > region[..., 0] + 18) & (region[..., 1] > region[..., 2] + 10)
    region[green] = left[green]
    a[296:380, 1130:1250] = region
    a[543:563, 580:700] = a[543:563, 470:590]            # the plain strip beside the label
    return Image.fromarray(a.astype(np.uint8))

class Stage:
    def __init__(self):
        panel = clean_panel().resize((2560, 1640), Image.LANCZOS)
        self.base = Image.new('RGB', (STAGE_W, STAGE_H))
        bg = background().resize((STAGE_W, STAGE_H), Image.BILINEAR)
        self.base.paste(bg, (0, 0))
        shadow = Image.new('L', (STAGE_W, STAGE_H), 0)
        ImageDraw.Draw(shadow).rounded_rectangle((PANEL_X + 10, PANEL_Y + 30, PANEL_X + 2570, PANEL_Y + 1680), 60, fill=150)
        shadow = shadow.filter(ImageFilter.GaussianBlur(40))
        self.base.paste((0, 0, 0), (0, 0), shadow)
        mask = Image.new('L', panel.size, 0)
        ImageDraw.Draw(mask).rounded_rectangle((28, 28, 2532, 1612), 56, fill=255)
        self.base.paste(panel, (PANEL_X, PANEL_Y), mask)
        self.knob_patches = {}
        for e, (x, y, r) in KNOBS.items():
            box = (PANEL_X + 2 * (x - r), PANEL_Y + 2 * (y - r), PANEL_X + 2 * (x + r), PANEL_Y + 2 * (y + r))
            patch = self.base.crop(box)
            m = Image.new('L', patch.size, 0)
            ImageDraw.Draw(m).ellipse((6, 6, patch.size[0] - 6, patch.size[1] - 6), fill=255)
            self.knob_patches[e] = (box, patch, m.filter(ImageFilter.GaussianBlur(1.5)))

    def render(self, lcd, leds, buttons, keys, turned, glow, t):
        im = self.base.copy()
        if lcd is None:                                       # switched off
            im.paste((6, 7, 9), (LCD_X, LCD_Y, LCD_X + 960, LCD_Y + 960))
        else:
            im.paste(lcd.resize((960, 960), Image.NEAREST), (LCD_X, LCD_Y))
        over = Image.new('RGBA', (STAGE_W, STAGE_H), (0, 0, 0, 0))
        d = ImageDraw.Draw(over)
        for e, (box, patch, m) in self.knob_patches.items():
            if turned[e]:
                im.paste(patch.rotate(-15 * turned[e], Image.BICUBIC), box[:2], m)
            if glow[e]:
                a = int(255 * min(1, glow[e] / 6))
                x0, y0, x1, y1 = box
                d.ellipse((x0 - 8, y0 - 8, x1 + 8, y1 + 8), outline=ACCENT + (a,), width=6)
        for b, rect in BUTTON_RECTS.items():
            x0, y0, x1, y1 = panel_rect(*rect)
            if (leds >> b) & 1:
                d.rounded_rectangle((x0 - 6, y0 - 6, x1 + 6, y1 + 6), 18, outline=(80, 220, 110, 255), width=6)
            if (buttons >> b) & 1:
                d.rounded_rectangle((x0, y0, x1, y1), 14, fill=(255, 255, 255, 70), outline=ACCENT + (255,), width=6)
        for k, rect in enumerate(KEY_RECTS):
            if (keys >> k) & 1:
                x0, y0, x1, y1 = panel_rect(*rect)
                d.rounded_rectangle((x0, y0, x1, y1), 60, fill=(255, 224, 0, 90), outline=ACCENT + (255,), width=6)
        for start, end, y, h in HIGHLIGHTS:
            a = fade_alpha(t, start, end)
            if a:
                d.rounded_rectangle((LCD_X - 14, LCD_Y + 4 * y - 10, LCD_X + 974, LCD_Y + 4 * (y + h) + 6), 14,
                                    outline=ACCENT + (int(255 * a),), width=7)
        im.paste(over, (0, 0), over)
        return im

def camera(t):
    for (t0, *a), (t1, *b) in zip(CAMERA, CAMERA[1:]):
        if t0 <= t <= t1:
            k = smooth((t - t0) / (t1 - t0)) if t1 > t0 else 1
            return [p + (q - p) * k for p, q in zip(a, b)]
    return CAMERA[-1][1:]

def shoot(stage_im, t):
    cx, cy, w = camera(t)
    h = w * 9 / 16
    x0 = min(max(cx - w / 2, 0), STAGE_W - w)
    y0 = min(max(cy - h / 2, 0), STAGE_H - h)
    return stage_im.resize((W, H), Image.BILINEAR, box=(x0, y0, x0 + w, y0 + h))

# ---- cards --------------------------------------------------------------------------------------------------------------
class Cards:
    def __init__(self, work, stage):
        self.work = work
        blurred = shoot(stage.base, 0).filter(ImageFilter.GaussianBlur(18))
        self.dim = Image.blend(blurred, Image.new('RGB', (W, H), BG), 0.55)
        self.plain = background()
        self.title = self.make_title()
        self.convert_shot = Image.open(os.path.join(work, 'converter.png')).convert('RGB')
        self.install = self.make_side('install.png', '2', 'Install over USB',
                                      ['The install page puts the firmware and the bank on the FM-1 in one go, over USB-MIDI, from Chrome or Edge.',
                                       'It skips what the FM-1 already has, and checks the bank\'s CRC32 when it is written.'],
                                      'mwedmark.github.io/fm1-sf2/install.html')
        self.convert_text = self.make_side(None, '1', 'Convert a SoundFont',
                                           ['Drop an .sf2 file on the converter page. It runs in the browser; the file never leaves your computer.',
                                            'This General MIDI bank: 129 instruments in 453.7 KB, 8-bit BFP8 at 32 kHz. That is 99.5 % of the FM-1\'s 456 KB of sample flash.'],
                                           'mwedmark.github.io/fm1-sf2/converter.html')
        self.switch = self.make_side(['switch_in.png', 'switch_out.png'], '3', 'Switch firmware, both ways',
                                     ['In: the install page sees what the FM-1 runs. From M-VAVE\'s original firmware V15 it installs the SF2 player in one step; '
                                      'from other firmware it puts V15 on first, in the same go.',
                                      'Out: one tick puts V15 and its 128 factory voices back. To come back later, run the page again.'],
                                     'mwedmark.github.io/fm1-sf2/install.html')
        self.specs = self.make_specs()
        self.end = self.make_end()

    def make_title(self):
        im = self.dim.copy()
        d = ImageDraw.Draw(im)
        big, sub, small = font('InterDisplay-Bold.otf', 170), font('Inter-Medium.otf', 52), font('Inter-Regular.otf', 34)
        d.text((W / 2, 400), 'FM1-SF2', font=big, fill=INK, anchor='mm')
        d.text((W / 2, 530), 'A SoundFont player for the M-VAVE FM-1', font=sub, fill=INK, anchor='mm')
        d.rounded_rectangle((W / 2 - 60, 600, W / 2 + 60, 606), 3, fill=ACCENT)
        d.text((W / 2, 668), '32 voices  ·  16 MIDI channels  ·  General MIDI  ·  open firmware', font=small, fill=MUTED, anchor='mm')
        return im

    def make_side(self, shot, number, heading, body, url):
        """A card with room for a screenshot on the left and text on the right."""
        im = self.plain.copy()
        d = ImageDraw.Draw(im)
        x = 1010
        d.text((x, 250), number, font=font('InterDisplay-Bold.otf', 120), fill=ACCENT, anchor='ls')
        d.text((x, 340), heading, font=font('InterDisplay-SemiBold.otf', 66), fill=INK, anchor='ls')
        y = 410
        f = font('Inter-Regular.otf', 36)
        for para in body:
            for line in wrap(d, para, f, 800):
                d.text((x, y), line, font=f, fill=INK)
                y += 50
            y += 26
        d.text((x, y + 10), url, font=font('Inter-Medium.otf', 32), fill=ACCENT)
        shots = [shot] if isinstance(shot, str) else shot or []
        images = []
        for name in shots:
            s = Image.open(os.path.join(self.work, name)).convert('RGB')
            images.append(s.resize((800, round(s.height * 800 / s.width)), Image.LANCZOS))
        y = (H - sum(s.height for s in images) - 40 * (len(images) - 1)) // 2
        for s in images:
            self.frame_shot(im, s, (110, y))
            y += s.height + 40
        return im

    @staticmethod
    def frame_shot(im, s, at):
        x, y = at
        sh = Image.new('L', (W, H), 0)
        ImageDraw.Draw(sh).rounded_rectangle((x + 6, y + 18, x + s.width + 6, y + s.height + 18), 22, fill=170)
        im.paste((0, 0, 0), (0, 0), sh.filter(ImageFilter.GaussianBlur(22)))
        m = Image.new('L', s.size, 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, s.width - 1, s.height - 1), 18, fill=255)
        im.paste(s, at, m)

    def convert(self, t):
        """The converter page, scrolling from the file to the result while the card is up."""
        im = self.convert_text.copy()
        s = self.convert_shot
        scale = 800 / s.width
        view_h = round(860 / scale)
        k = smooth((t - 15.0) / 8.0)
        top = round(k * (1290 - 60))                          # to the line that says how big the bank is
        crop = s.crop((0, top, s.width, top + view_h)).resize((800, 860), Image.LANCZOS)
        self.frame_shot(im, crop, (110, 110))
        return im

    def make_specs(self):
        im = self.plain.copy()
        d = ImageDraw.Draw(im)
        d.text((160, 230), 'What it does', font=font('InterDisplay-SemiBold.otf', 76), fill=INK, anchor='ls')
        items = [('32 voices, 16 MIDI channels', 'General MIDI, with a program per channel and a drum kit on channel 10'),
                 ('Banks from any SoundFont', 'Key and velocity zones, layers, loops, envelopes, filter, vibrato and tremolo'),
                 ('Reverb, chorus and a limiter', 'Master levels on the knobs, sends per channel'),
                 ('MIDI over USB, DIN and Bluetooth', 'Plus a MIDI file player that plays a song from the FM-1\'s own RAM'),
                 ('Back to the original any time', 'The install page puts M-VAVE\'s firmware V15 back')]
        y = 340
        for head, text in items:
            d.rounded_rectangle((160, y + 8, 172, y + 56), 6, fill=ACCENT)
            d.text((206, y), head, font=font('Inter-SemiBold.otf', 44), fill=INK)
            d.text((206, y + 60), text, font=font('Inter-Regular.otf', 32), fill=MUTED)
            y += 140
        return im

    def make_end(self):
        im = self.dim.copy()
        d = ImageDraw.Draw(im)
        d.text((W / 2, 330), 'Try it in your browser', font=font('InterDisplay-SemiBold.otf', 84), fill=INK, anchor='mm')
        rows = [('Emulator and MIDI player', 'mwedmark.github.io/fm1-sf2'),
                ('Install on your FM-1', 'mwedmark.github.io/fm1-sf2/install.html'),
                ('Source', 'github.com/mwedmark/fm1-sf2')]
        y = 470
        for label, url in rows:
            d.text((W / 2 - 30, y), label, font=font('Inter-Regular.otf', 38), fill=MUTED, anchor='rm')
            d.text((W / 2 + 30, y), url, font=font('Inter-Medium.otf', 40), fill=ACCENT, anchor='lm')
            y += 80
        f = font('Inter-Regular.otf', 26)
        for i, line in enumerate(['Screen and sound: the SF2 player firmware\'s own code, run on a PC.  Bank: 1mgm.sf2 (BFP8, 32 kHz).',
                                  'Music: W. A. Mozart, Eine kleine Nachtmusik, K. 525, played by the firmware\'s MIDI file player.']):
            d.text((W / 2, 900 + 40 * i), line, font=f, fill=MUTED, anchor='mm')
        return im

    def get(self, name, t):
        return self.convert(t) if name == 'convert' else getattr(self, name)

# ---- overlays on the finished frame ------------------------------------------------------------------------------------------
CAPTION_FONT = font('Inter-Medium.otf', 40)
CHAPTER_FONT = font('Inter-SemiBold.otf', 30)

def overlays(frame, t):
    over = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    for start, end, text in CAPTIONS:
        a = fade_alpha(t, start, end)
        if not a:
            continue
        lines = wrap(d, text, CAPTION_FONT, 1500)
        widest = max(d.textlength(l, font=CAPTION_FONT) for l in lines)
        h = 58 * len(lines) + 34
        y0 = H - 70 - h
        d.rounded_rectangle((W / 2 - widest / 2 - 36, y0, W / 2 + widest / 2 + 36, y0 + h), 20, fill=(8, 10, 14, int(200 * a)))
        for i, line in enumerate(lines):
            d.text((W / 2, y0 + 17 + 58 * i + 29), line, font=CAPTION_FONT, fill=INK + (int(255 * a),), anchor='mm')
    for start, end, number, title in CHAPTERS:
        a = fade_alpha(t, start, end, 0.5)
        if not a:
            continue
        label = f'{number}   {title}'
        w = d.textlength(label, font=CHAPTER_FONT)
        d.rounded_rectangle((60, 54, 60 + w + 56, 116), 31, fill=(8, 10, 14, int(190 * a)))
        d.text((88, 85), number, font=CHAPTER_FONT, fill=ACCENT + (int(255 * a),), anchor='lm')
        d.text((88 + d.textlength(number + '   ', font=CHAPTER_FONT), 85), title, font=CHAPTER_FONT, fill=INK + (int(255 * a),), anchor='lm')
    frame.paste(over, (0, 0), over)
    return frame

# ---- frames ---------------------------------------------------------------------------------------------------------
RECORD = 240 * 240 * 2 + 8 + 4 + 4 + 14 + 7

def lcd_image(raw):
    px = np.frombuffer(raw, dtype='<u2').reshape(240, 240).astype(np.uint32)
    rgb = np.stack([((px >> 11) & 31) * 255 // 31, ((px >> 5) & 63) * 255 // 63, (px & 31) * 255 // 31], -1)
    return Image.fromarray(rgb.astype(np.uint8))

G = {}
def init_worker(work):
    G['stage'] = Stage()
    G['cards'] = Cards(work, G['stage'])
    G['frames'] = np.memmap(os.path.join(work, 'frames.bin'), dtype=np.uint8, mode='r')
    G['safe'] = np.memmap(os.path.join(work, 'safe', 'frames.bin'), dtype=np.uint8, mode='r')

def render_frame(n):
    t = n / FPS
    o = 240 * 240 * 2
    if t < POWER_OFF:
        raw = G['frames'][n * RECORD:(n + 1) * RECORD].tobytes()
    else:                                                     # off, then the safe-mode run
        m = max(0, round((t - SAFE_START) * FPS))
        raw = G['safe'][m * RECORD:(m + 1) * RECORD].tobytes()
    leds = int.from_bytes(raw[o:o + 8], 'little')
    buttons = int.from_bytes(raw[o + 8:o + 12], 'little')
    keys = int.from_bytes(raw[o + 12:o + 16], 'little')
    turned = np.frombuffer(raw[o + 16:o + 30], dtype='<i2').tolist()
    glow = list(raw[o + 30:o + 37])
    lcd = lcd_image(raw[:o])
    if POWER_OFF <= t < SAFE_START:
        lcd, leds, buttons, keys = None, 0, 0, 0
    frame = None
    covered = 0.0
    for start, end, name in CARDS:
        a = min(fade_alpha(t, start, end, FADE), 1.0)
        if start == 0.0 and t < end - FADE:
            a = 1.0
        if a >= covered and a > 0:
            covered, card = a, (name, a)
    if covered < 1.0:
        stage_im = G['stage'].render(lcd, leds, buttons, keys, turned, glow, t)
        frame = overlays(shoot(stage_im, t), t)
    if covered > 0:
        name, a = card
        c = G['cards'].get(name, t)
        frame = c if frame is None else Image.blend(frame, c, a)
    if t < 0.8:                                               # from black
        frame = Image.blend(Image.new('RGB', (W, H)), frame, t / 0.8)
    if t > DURATION - 1.5:                                    # to black
        frame = Image.blend(frame, Image.new('RGB', (W, H)), min(1, (t - (DURATION - 1.5)) / 1.5))
    return frame.tobytes()

def main():
    args = sys.argv[1:]
    preview = None
    if '--preview' in args:
        i = args.index('--preview')
        preview = float(args[i + 1])
        del args[i:i + 2]
    work = os.path.abspath(args[0])
    out = os.path.abspath(args[1]) if len(args) > 1 else os.path.join(work, 'fm1-sf2-walkthrough.mp4')
    with open(os.path.join(work, 'script.txt'), 'w') as f:
        for seconds, what, a, b in ACTIONS:
            f.write(f'{round(seconds * 1000)} {what} {a} {b}\n')
    subprocess.run([os.path.join(HERE, 'fm1_video'), os.path.join(work, 'gm.fm1b'), os.path.join(work, 'song.fm1s'),
                    os.path.join(work, 'script.txt'), str(DURATION), work], check=True)
    safe = os.path.join(work, 'safe')
    os.makedirs(safe, exist_ok=True)
    with open(os.path.join(safe, 'script.txt'), 'w') as f:
        for seconds, what, a, b in SAFE_ACTIONS:
            f.write(f'{round(seconds * 1000)} {what} {a} {b}\n')
    subprocess.run([os.path.join(HERE, 'fm1_video'), os.path.join(work, 'gm.fm1b'), os.path.join(work, 'song.fm1s'),
                    os.path.join(safe, 'script.txt'), str(DURATION - SAFE_START + 1), safe], check=True)
    # the soundtrack at the levels AUDIO_GAIN sets
    with open(os.path.join(work, 'audio.wav'), 'rb') as f:
        header, pcm = f.read(44), np.frombuffer(f.read(), dtype='<i2').reshape(-1, 2)
    times, gains = zip(*AUDIO_GAIN)
    gain = np.interp(np.arange(len(pcm)) / 44100, times, gains)[:, None]
    with open(os.path.join(work, 'audio_mix.wav'), 'wb') as f:
        f.write(header)
        f.write((pcm * gain).round().astype('<i2').tobytes())
    seconds = preview or DURATION
    count = int(seconds * FPS)
    fade_at = DURATION - 5
    ffmpeg = subprocess.Popen(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', str(FPS),
                               '-i', '-', '-i', os.path.join(work, 'audio_mix.wav'), '-t', str(seconds),
                               '-af', f'afade=t=in:d=0.3,afade=t=out:st={fade_at}:d=4.5,loudnorm=I=-16:TP=-1.5:LRA=11',
                               '-c:v', 'libx264', '-preset', 'medium', '-crf', '18', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
                               '-c:a', 'aac', '-b:a', '192k', '-ar', '48000', out], stdin=subprocess.PIPE)
    with Pool(os.cpu_count(), initializer=init_worker, initargs=(work,)) as pool:
        for i, data in enumerate(pool.imap(render_frame, range(count), chunksize=8)):
            ffmpeg.stdin.write(data)
            if i % 300 == 0:
                print(f'{i / FPS:6.1f} s of {seconds:.0f}', flush=True)
    ffmpeg.stdin.close()
    if ffmpeg.wait():
        sys.exit('ffmpeg failed')
    print('wrote', out)

if __name__ == '__main__':
    main()
