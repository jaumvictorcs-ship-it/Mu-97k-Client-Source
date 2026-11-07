import math, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops, ImageEnhance
OUT = sys.argv[1]; LOGO = sys.argv[2]
W, H = 820, 420
random.seed(7)
FONT = "/System/Library/Fonts/Supplemental/Copperplate.ttc"
def font(size, bold=True):
    return ImageFont.truetype(FONT, size, index=1 if bold else 0)

# ---- fundo: brasa e vermelho, gerado do zero
bg = Image.new("RGB", (W, H))
px = bg.load()
for y in range(H):
    for x in range(W):
        t = y / H
        r = int(38 + 46 * t); g = int(8 + 10 * t); b = int(8 + 4 * t)
        px[x, y] = (r, g, b)
def glow(img, cx, cy, rad, color, alpha):
    layer = Image.new("RGB", img.size, (0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.ellipse((cx - rad, cy - rad, cx + rad, cy + rad), fill=color)
    layer = layer.filter(ImageFilter.GaussianBlur(rad * 0.6))
    return ImageChops.add(img, ImageEnhance.Brightness(layer).enhance(alpha))
bg = glow(bg, 430, 130, 170, (255, 110, 25), 0.42)
bg = glow(bg, 90, 300, 140, (190, 40, 15), 0.30)
bg = glow(bg, 700, 230, 150, (230, 80, 20), 0.30)
# nevoa
noise = Image.effect_noise((W // 4, H // 4), 60).resize((W, H), Image.BICUBIC).filter(ImageFilter.GaussianBlur(18))
mist = Image.merge("RGB", (noise.point(lambda v: v * 0.55), noise.point(lambda v: v * 0.22), noise.point(lambda v: v * 0.08)))
bg = ImageChops.add(bg, ImageEnhance.Brightness(mist).enhance(0.38))
# raios de luz diagonais
rays = Image.new("RGB", (W, H)); d = ImageDraw.Draw(rays)
for i in range(7):
    x = random.randint(150, 700); w = random.randint(14, 40)
    d.polygon([(x, -10), (x + w, -10), (x + w - 260, H), (x - 260, H)], fill=(60, 26, 8))
bg = ImageChops.add(bg, rays.filter(ImageFilter.GaussianBlur(16)))
# montanhas e um castelo em silhueta
sil = Image.new("L", (W, H), 0); d = ImageDraw.Draw(sil)
def ridge(base, amp, step, seed):
    random.seed(seed); pts = [(0, H)]; y = base
    for x in range(0, W + step, step):
        y = max(base - amp, min(base + amp, y + random.randint(-amp // 2, amp // 2))); pts.append((x, y))
    pts.append((W, H)); return pts
d.polygon(ridge(300, 40, 30, 3), fill=110)
d.polygon(ridge(340, 30, 22, 5), fill=200)
cx = 640
for (x0, w0, h0) in [(cx, 26, 120), (cx + 34, 18, 90), (cx - 28, 18, 80), (cx + 60, 14, 60)]:
    d.rectangle((x0, 340 - h0, x0 + w0, 360), fill=230)
    d.polygon([(x0 - 4, 340 - h0), (x0 + w0 // 2, 340 - h0 - 30), (x0 + w0 + 4, 340 - h0)], fill=230)
sil = sil.filter(ImageFilter.GaussianBlur(1.2))
bg = Image.composite(Image.new("RGB", (W, H), (18, 6, 6)), bg, sil)
# faiscas
spark = Image.new("RGB", (W, H)); d = ImageDraw.Draw(spark)
random.seed(11)
for i in range(85):
    x, y = random.randint(0, W), random.randint(0, H); r = random.choice([1, 1, 1, 2, 2, 3])
    c = random.choice([(255, 180, 80), (255, 140, 40), (255, 220, 150)])
    d.ellipse((x - r, y - r, x + r, y + r), fill=c)
bg = ImageChops.add(bg, spark.filter(ImageFilter.GaussianBlur(1.6)))
bg = ImageChops.add(bg, ImageEnhance.Brightness(spark.filter(ImageFilter.GaussianBlur(5))).enhance(0.8))
# vinheta
vig = Image.new("L", (W, H), 0); d = ImageDraw.Draw(vig)
d.ellipse((-60, -60, W + 60, H + 120), fill=255)
vig = vig.filter(ImageFilter.GaussianBlur(90))
bg = Image.composite(bg, Image.new("RGB", (W, H), (0, 0, 0)), vig)

# ---- logotipo: MU do site + CLT dourado
logo = Image.open(LOGO).convert("RGBA")
lw = 270; logo = logo.resize((lw, int(logo.height * lw / logo.width)), Image.LANCZOS)
lx, ly = (W - lw) // 2 - 50, 40
shadow = Image.new("RGBA", (W, H)); sh = Image.new("RGBA", logo.size, (0, 0, 0, 255)); sh.putalpha(logo.split()[3])
shadow.paste(sh, (lx + 5, ly + 7), sh); shadow = shadow.filter(ImageFilter.GaussianBlur(8))
halo = Image.new("RGBA", (W, H)); hl = Image.new("RGBA", logo.size, (255, 120, 30, 255)); hl.putalpha(logo.split()[3])
halo.paste(hl, (lx, ly), hl); halo = halo.filter(ImageFilter.GaussianBlur(16))
bgA = bg.convert("RGBA"); bgA = Image.alpha_composite(bgA, halo); bgA = Image.alpha_composite(bgA, shadow)
bgA.paste(logo, (lx, ly), logo)
def gold_text(img, xy, text, f, anchor="la", glow_px=6):
    mask = Image.new("L", img.size, 0); ImageDraw.Draw(mask).text(xy, text, font=f, fill=255, anchor=anchor)
    box = mask.getbbox()
    grad = Image.new("RGBA", img.size)
    gd = ImageDraw.Draw(grad)
    for y in range(box[1], box[3] + 1):
        t = (y - box[1]) / max(1, box[3] - box[1])
        c = (int(255 - 40 * t), int(236 - 110 * t), int(160 - 140 * t), 255) if t < 0.55 else (int(214 + 30 * (t - .55)), int(150 - 40 * (t - .55)), int(40), 255)
        gd.line((box[0], y, box[2], y), fill=c)
    outline = mask.filter(ImageFilter.MaxFilter(5))
    dark = Image.new("RGBA", img.size, (40, 12, 4, 255)); dark.putalpha(outline)
    g = Image.new("RGBA", img.size, (255, 140, 30, 255)); g.putalpha(mask.filter(ImageFilter.GaussianBlur(glow_px)))
    img = Image.alpha_composite(img, g); img = Image.alpha_composite(img, dark)
    grad.putalpha(mask); return Image.alpha_composite(img, grad)
bgA = gold_text(bgA, (lx + lw + 8, ly + 78), "CLT", font(74), anchor="ls")
bgA = gold_text(bgA, (431, ly + 168), "SERVIDOR  0.97K", font(17, False), anchor="mm", glow_px=3)

# ---- painel do progresso (vidro escuro)
panel = Image.new("RGBA", (W, H)); d = ImageDraw.Draw(panel)
d.rounded_rectangle((34, 292, 548, 380), 8, fill=(10, 4, 4, 165), outline=(170, 120, 50, 200), width=1)
bgA = Image.alpha_composite(bgA, panel)

# ---- moldura
fr = Image.new("RGBA", (W, H)); d = ImageDraw.Draw(fr)
d.rectangle((0, 0, W - 1, H - 1), outline=(20, 14, 12, 255), width=10)
d.rectangle((4, 4, W - 5, H - 5), outline=(120, 96, 70, 255), width=2)
d.rectangle((10, 10, W - 11, H - 11), outline=(200, 156, 70, 255), width=1)
d.rectangle((0, H - 34, W, H), fill=(16, 10, 9, 255))
d.line((10, H - 34, W - 10, H - 34), fill=(200, 156, 70, 255), width=1)
for (x, y) in [(10, 10), (W - 11, 10), (10, H - 11), (W - 11, H - 11)]:
    d.polygon([(x, y - 9), (x + 9, y), (x, y + 9), (x - 9, y)], fill=(210, 165, 80, 255), outline=(60, 40, 20, 255))
d.polygon([(W // 2, 4), (W // 2 + 14, 14), (W // 2, 24), (W // 2 - 14, 14)], fill=(150, 30, 20, 255), outline=(210, 165, 80, 255))
bgA = Image.alpha_composite(bgA, fr)
ImageDraw.Draw(bgA).text((W // 2, H - 17), "https://muclt.online", font=font(14, False), fill=(214, 190, 150, 255), anchor="mm")
bg = bgA.convert("RGB")
bg.save(OUT + "/launcher_bg.png", optimize=True)

# ---- botoes (sprite opaco: cada estado sobre o recorte do fundo)
def button(rect, label, size, state, big):
    x0, y0, x1, y1 = rect; w, h = x1 - x0, y1 - y0
    tile = bg.crop(rect).convert("RGBA")
    b = Image.new("RGBA", (w, h)); d = ImageDraw.Draw(b)
    top, bot = {"normal": ((150, 34, 18), (70, 10, 6)), "hover": ((200, 60, 24), (110, 20, 8)), "off": ((70, 60, 56), (40, 34, 32))}[state]
    if not big: top, bot = {"normal": ((60, 20, 14), (24, 8, 6)), "hover": ((110, 36, 18), (50, 14, 8)), "off": ((50, 44, 42), (30, 26, 24))}[state]
    for y in range(h):
        t = y / max(1, h - 1); c = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,)
        d.line((0, y, w, y), fill=c)
    mask = Image.new("L", (w, h), 0); ImageDraw.Draw(mask).rounded_rectangle((0, 0, w - 1, h - 1), 6 if big else 4, fill=255)
    gold = (220, 175, 85, 255) if state != "off" else (130, 120, 110, 255)
    ImageDraw.Draw(b).rounded_rectangle((0, 0, w - 1, h - 1), 6 if big else 4, outline=(30, 16, 10, 255), width=2)
    ImageDraw.Draw(b).rounded_rectangle((3, 3, w - 4, h - 4), 4 if big else 3, outline=gold, width=2 if big else 1)
    if big:
        for (x, y) in [(3, h // 2), (w - 4, h // 2)]:
            ImageDraw.Draw(b).polygon([(x, y - 8), (x + 8, y), (x, y + 8), (x - 8, y)], fill=gold)
    tile.paste(b, (0, 0), mask)
    if state == "off":
        ImageDraw.Draw(tile).text((w // 2, h // 2 + 1), label, font=font(size), fill=(150, 140, 130, 255), anchor="mm")
    else:
        tile = gold_text(tile, (w // 2, h // 2 + 2), label, font(size), anchor="mm", glow_px=4 if state == "hover" else 2)
    return tile.convert("RGB")
PLAY, CFG = (586, 304, 790, 374), (586, 262, 790, 294)
states = ["normal", "hover", "off"]
pw, ph = PLAY[2] - PLAY[0], PLAY[3] - PLAY[1]; cw, chh = CFG[2] - CFG[0], CFG[3] - CFG[1]
sprite = Image.new("RGB", (pw * 3, ph + chh))
for i, s in enumerate(states):
    sprite.paste(button(PLAY, "JOGAR", 40, s, True), (i * pw, 0))
    sprite.paste(button(CFG, "Configurações", 17, s, False), (i * cw, ph))
sprite.save(OUT + "/launcher_buttons.png", optimize=True)
# previa do conjunto (para revisar)
prev = bg.copy(); prev.paste(sprite.crop((0, 0, pw, ph)), PLAY[:2]); prev.paste(sprite.crop((0, ph, cw, ph + chh)), CFG[:2])
prev.save(OUT + "/preview.png")
print("ok", bg.size, sprite.size)
