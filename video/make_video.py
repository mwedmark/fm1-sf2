#!/usr/bin/env python3
"""The FM1-SF2 walk-through video: the SF2 player firmware run on the PC (fm1_video), its screen set into the panel, with title
cards and captions, and the firmware's own audio as the soundtrack (Eine kleine Nachtmusik from the song player).

    python3 video/make_video.py <work_dir> [out.mp4] [--preview SECONDS]

<work_dir> must hold gm.fm1b (make_bank.js bfp8 32000), song.fm1s (fm1_song.js), converter.png and install.png
(screenshots.js); make_video.sh makes all of them. --preview renders only the first SECONDS, for a quick look.
Needs Pillow, numpy and ffmpeg with libx264."""
import math, os, subprocess, sys
from multiprocessing import Pool
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
W, H, FPS = 1920, 1080, 30
DURATION = 142.0

# ---- the timeline -------------------------------------------------------------------------------------------------------
# Panel buttons (fm1_controls.buttons bits) and encoders (fm1_controls.encoders), as sf2player.c reads them.
OCT_DOWN, OCT_UP, FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC = range(14)
SELECT, ALGORITHM, KNOB1, KNOB2, KNOB3, KNOB4, PRESETS = range(7)

# What the virtual hands do on the panel: (seconds, action, argument, count). fm1_video.c plays it into the firmware.
ACTIONS = [
    (0.4, 'press', PLAY, 0),
    (64.0, 'turn', KNOB3, 5),        # master reverb 30 -> 50
    (67.5, 'turn', KNOB4, 3),        # master chorus 30 -> 42
    (72.5, 'press', HOME, 0),        # the channel page
    (77.2, 'turn', KNOB1, 3),        # cursor to channel 4, the first violins
    (79.8, 'press', SEQ, 0),         # solo
    (86.0, 'press', SEQ, 0),         # no solo
    (89.0, 'turn', KNOB1, -2),       # channel 2, the contrabass
    (90.6, 'press', ARP, 0),         # mute
    (96.0, 'press', ARP, 0),         # unmute
    (98.2, 'turn', KNOB1, 2),        # channel 4 again
    (100.0, 'turn', SELECT, 33),     # 041 VIOLIN -> 074 FLUTE
    (106.8, 'turn', SELECT, -33),    # and back
    (110.6, 'press', HOME, 0),       # the program page
]

# Camera keyframes in stage pixels: (seconds, centre x, centre y, width). The stage is the panel at twice its size, the screen at
# four times (one LCD pixel = 4 stage pixels); see Stage below.
FULL = (1600, 900, 3200)
LCD = (1540, 720, 2400)
LCD_BUTTONS = (1940, 730, 2500)
LCD_KNOBS = (1900, 730, 2500)
LCD_SELECT = (1380, 730, 2500)
CAMERA = [
    (0.0, *FULL), (6.5, *FULL), (9.0, *FULL),
    (36.0, *LCD), (61.5, *LCD), (63.0, *LCD_KNOBS), (71.5, *LCD_KNOBS), (73.0, *LCD_BUTTONS),
    (97.8, *LCD_BUTTONS), (99.4, *LCD_SELECT), (109.8, *LCD_SELECT), (111.5, *FULL), (DURATION, *FULL),
]

CAPTIONS = [
    (7.0, 12.6, "Everything you hear is the FM-1 firmware playing Mozart's Eine kleine Nachtmusik from its own song memory."),
    (37.5, 42.3, 'The latest note, from the keys or any MIDI channel, and which zone of its preset played it.'),
    (42.5, 47.0, "That zone's key range, root note, sample length and loop."),
    (47.2, 51.2, 'Its envelope, and the level of the voice as it plays.'),
    (51.4, 55.6, 'Voices sounding out of 32, and the CPU load.'),
    (55.8, 61.2, 'The song in RAM: its name, where it plays, and how long it is.'),
    (63.0, 71.6, 'KNOB3 and KNOB4 set the master reverb and chorus.'),
    (73.0, 77.0, 'HOME opens the channel page: one row per MIDI channel. Green rows are playing.'),
    (77.2, 79.6, 'KNOB1 moves the cursor to channel 4, the first violins.'),
    (79.8, 85.8, 'SEQ solos the channel: only the first violins play.'),
    (86.0, 88.8, 'SEQ again: the whole orchestra.'),
    (89.0, 95.8, 'ARP mutes a channel: here the contrabass.'),
    (96.0, 98.0, 'ARP again brings it back.'),
    (98.2, 106.6, 'SELECT gives a channel another instrument: the first violins on a flute.'),
    (106.8, 110.2, 'And back to the violin.'),
    (110.8, 117.6, 'HOME goes back to the program page. The song plays on, from the FM-1\'s own flash and RAM.'),
]

# Boxes drawn around lines of the screen: (from, to, LCD y, LCD height).
HIGHLIGHTS = [
    (37.5, 42.3, 28, 18), (42.5, 47.0, 60, 39), (47.2, 51.2, 102, 34), (51.4, 55.6, 138, 11), (55.8, 61.2, 212, 11),
    (63.0, 71.6, 156, 11),
]

CHAPTERS = [(36.0, 61.8, '3', 'The program page'), (62.0, 72.0, '4', 'Reverb and chorus'), (72.2, 117.8, '5', 'The channel page')]

# Full-screen cards: (from, to, name). They fade in and out over the panel.
CARDS = [(0.0, 6.5, 'title'), (13.0, 25.6, 'convert'), (25.0, 36.6, 'install'), (117.8, 130.0, 'specs'), (129.4, DURATION + 1, 'end')]
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

    def render(self, lcd, leds, buttons, turned, glow, t):
        im = self.base.copy()
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
        if shot:
            s = Image.open(os.path.join(self.work, shot)).convert('RGB')
            s = s.resize((800, round(s.height * 800 / s.width)), Image.LANCZOS)
            self.frame_shot(im, s, (110, (H - s.height) // 2))
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

def render_frame(n):
    t = n / FPS
    raw = G['frames'][n * RECORD:(n + 1) * RECORD].tobytes()
    o = 240 * 240 * 2
    leds = int.from_bytes(raw[o:o + 8], 'little')
    buttons = int.from_bytes(raw[o + 8:o + 12], 'little')
    turned = np.frombuffer(raw[o + 16:o + 30], dtype='<i2').tolist()
    glow = list(raw[o + 30:o + 37])
    frame = None
    covered = 0.0
    for start, end, name in CARDS:
        a = min(fade_alpha(t, start, end, FADE), 1.0)
        if start == 0.0 and t < end - FADE:
            a = 1.0
        if a >= covered and a > 0:
            covered, card = a, (name, a)
    if covered < 1.0:
        stage_im = G['stage'].render(lcd_image(raw[:o]), leds, buttons, turned, glow, t)
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
    seconds = preview or DURATION
    count = int(seconds * FPS)
    fade_at = DURATION - 5
    ffmpeg = subprocess.Popen(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', str(FPS),
                               '-i', '-', '-i', os.path.join(work, 'audio.wav'), '-t', str(seconds),
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
