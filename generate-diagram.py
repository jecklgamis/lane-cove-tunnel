#!/usr/bin/env python3
from PIL import Image, ImageDraw, ImageFont

W, H = 960, 480
BG       = (245, 247, 250)
BOX_CLR  = (255, 255, 255)
HDR_CLR  = (45,  90,  160)
TUN_CLR  = (30, 140,  90)
UDP_CLR  = (180, 60,  40)
BORDER   = (180, 190, 210)
TEXT_CLR = (30,  40,  60)
WHITE    = (255, 255, 255)
ARROW    = (100, 110, 130)

img  = Image.new("RGB", (W, H), BG)
draw = ImageDraw.Draw(img)

try:
    font_bold  = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 15)
    font_reg   = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 13)
    font_small = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 11)
    font_title = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 18)
except:
    font_bold  = ImageFont.load_default()
    font_reg   = font_bold
    font_small = font_bold
    font_title = font_bold

def box(x, y, w, h, fill=BOX_CLR, outline=BORDER, radius=6):
    draw.rounded_rectangle([x, y, x+w, y+h], radius=radius, fill=fill, outline=outline, width=1)

def centered(text, cx, y, font, color=TEXT_CLR):
    bb = draw.textbbox((0, 0), text, font=font)
    tw = bb[2] - bb[0]
    draw.text((cx - tw // 2, y), text, font=font, fill=color)

LAYERS = [
    ("Application",        (230, 244, 255), (100, 160, 220)),
    ("Transport (TCP/UDP)",(255, 248, 230), (200, 150,  50)),
    ("Network (IP)",       (240, 255, 240), ( 60, 160,  80)),
    ("TUN interface",      (235, 235, 255), ( 90,  80, 200)),
    ("Physical",           (250, 240, 240), (180,  80,  80)),
]

BW, BH, GAP = 200, 38, 6
LX, RX      = 60, W - 60 - BW
TOP_Y       = 110

def draw_stack(cx, label, subnet, is_client):
    x = cx - BW // 2
    # header card
    box(x - 4, TOP_Y - 54, BW + 8, 46, fill=HDR_CLR, outline=HDR_CLR, radius=8)
    centered(label,  cx, TOP_Y - 50, font_title, WHITE)
    centered(subnet, cx, TOP_Y - 30, font_small, (190, 210, 255))

    for i, (name, fill, accent) in enumerate(LAYERS):
        y = TOP_Y + i * (BH + GAP)
        box(x, y, BW, BH, fill=fill, outline=(*accent, 255))
        # left accent bar
        draw.rounded_rectangle([x, y, x+4, y+BH], radius=2, fill=accent)
        centered(name, cx, y + BH//2 - 7, font_reg, TEXT_CLR)

draw_stack(LX + BW//2, "Client", "10.9.0.0/24",  True)
draw_stack(RX + BW//2, "Server", "10.10.0.0/24", False)

# Tunnel band
TUN_Y  = TOP_Y + 3 * (BH + GAP)          # sits at the TUN interface row
TUN_H  = BH
TX1    = LX + BW + 14
TX2    = RX - 14
TMX    = W // 2

box(TX1, TUN_Y, TX2 - TX1, TUN_H, fill=(225, 245, 235), outline=TUN_CLR, radius=6)
centered("lanecove.0  ←  AES-256-GCM / UDP  →  lanecove.0", TMX, TUN_Y + TUN_H//2 - 7, font_small, TUN_CLR)

# Arrows
AY = TUN_Y + TUN_H // 2
aw = 10
draw.polygon([(TX1, AY), (TX1 + aw, AY - 5), (TX1 + aw, AY + 5)], fill=TUN_CLR)
draw.polygon([(TX2, AY), (TX2 - aw, AY - 5), (TX2 - aw, AY + 5)], fill=TUN_CLR)

# Internet cloud label between stacks
cloud_y = TOP_Y + 4 * (BH + GAP) + 8
box(TMX - 80, cloud_y, 160, 32, fill=(240, 240, 250), outline=BORDER, radius=16)
centered("UDP / Internet", TMX, cloud_y + 8, font_small, (100, 100, 140))

# Vertical connectors from TUN row to Internet band
def dashed_vline(x, y1, y2, color):
    seg, gap = 6, 4
    y = y1
    while y < y2:
        draw.line([(x, y), (x, min(y + seg, y2))], fill=color, width=2)
        y += seg + gap

MID_LX = LX + BW + 4
MID_RX = RX - 4
conn_y1 = TUN_Y + TUN_H
conn_y2 = cloud_y

dashed_vline(MID_LX - 20, conn_y1, conn_y2, BORDER)
dashed_vline(MID_RX + 20, conn_y1, conn_y2, BORDER)

# Title
centered("lane-cove-tunnel — Layer 3 Overlay Network", W // 2, 18, font_title, HDR_CLR)
centered("Point-to-point VPN over UDP, inspired by WireGuard", W // 2, 42, font_small, (100, 110, 140))

img.save("diagram.png")
print("Saved diagram.png")
