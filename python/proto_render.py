#!/usr/bin/env python3
# proto_render.py - validate the Arduboy->Supervision rendering pipeline using
# the REAL CastleBoy assets, before committing it to C.
#  - parses assets.h arrays we need
#  - builds a 128x64 1bpp Arduboy-style framebuffer (vertical pages)
#  - draws the real tileset (auto-tiling from map.cpp) + player plus_mask sprite
#  - converts the framebuffer to a 160x160 2bpp Supervision VRAM image -> PNG
import re, struct, zlib

ASRC = open('/home/claude/CastleBoy/CastleBoy/assets.h').read()

def get_array(name):
    m = re.search(r'const uint8_t %s\[\]\s*=\s*\{(.*?)\};' % re.escape(name), ASRC, re.S)
    if not m: raise KeyError(name)
    body = re.sub(r'//[^\n]*', '', m.group(1))   # strip line comments
    out = []
    for tok in body.split(','):
        tok = tok.strip()
        if tok:
            out.append(int(tok, 0) & 0xFF)
    return out

tileset = get_array('tileset')
player_pm = get_array('player_plus_mask')
stage = get_array('stage_1_1')
bg = get_array('background_mountain')

# ---- Arduboy-style framebuffer: 128x64, 8 pages of 128 bytes (bit0=top) ----
W, H = 128, 64
fb = bytearray(W * (H//8))   # 1024 bytes

def fb_set(x, y, on):
    if x < 0 or x >= W or y < 0 or y >= H: return
    idx = (y >> 3) * W + x
    bit = 1 << (y & 7)
    if on: fb[idx] |= bit
    else:  fb[idx] &= ~bit

def spr_dims(sheet): return sheet[0], sheet[1]

def draw_overwrite(x, y, sheet, frame):
    w, h = sheet[0], sheet[1]
    pages = (h + 7) // 8
    fsize = w * pages
    off = 2 + frame * fsize
    for p in range(pages):
        for cx in range(w):
            b = sheet[off + p*w + cx]
            for bit in range(8):
                yy = p*8 + bit
                if yy >= h: break
                fb_set(x+cx, y+yy, (b >> bit) & 1)

def draw_plus_mask(x, y, sheet, frame):
    w, h = sheet[0], sheet[1]
    pages = (h + 7) // 8
    fsize = w * pages * 2
    off = 2 + frame * fsize
    for p in range(pages):
        for cx in range(w):
            d = sheet[off + (p*w + cx)*2]
            m = sheet[off + (p*w + cx)*2 + 1]
            for bit in range(8):
                yy = p*8 + bit
                if yy >= h: break
                if (m >> bit) & 1:
                    fb_set(x+cx, y+yy, (d >> bit) & 1)

# ---- map (port of map.cpp) ----
TILE_WALL,TILE_WALL_ALT,TILE_SOLID_END,TILE_SOLID_END_ALT=0,1,2,3
TILE_GROUND_START,TILE_GROUND,TILE_GROUND_START_ALT,TILE_GROUND_ALT=4,5,6,7
TILE_GRAVE,TILE_CHAIN,TILE_WINDOW=8,9,10

width = stage[0]; height = stage[1]
meta = stage[2]
playerY = meta & 0x0F
playerX = stage[3]
tilemap_off = 4
def getTileAt(x,y):
    return (stage[tilemap_off + (x*height+y)//4] >> ((y%4)*2)) & 3

# outdoor style (stage_1_1 meta 0x45)
solidTileIndex=2; mainTile=TILE_GROUND; mainTileAlt=TILE_GROUND_ALT
mainStartTile=TILE_GROUND_START; mainStartTileAlt=TILE_GROUND_START_ALT
miscTile=TILE_WALL; endMainTile=True; propTile=TILE_GRAVE; showBackground=True

cameraX = 0
def map_draw():
    start = cameraX//8
    for ix in range(start, start+17):
        if ix >= width: break
        isMain=False; needToEndTile=False; tileEnded=False
        for iy in range(height):
            tile = getTileAt(ix,iy)
            if tile == 0:
                if (not tileEnded) and needToEndTile:
                    tile = TILE_SOLID_END if ix%2==0 else TILE_SOLID_END_ALT
                    needToEndTile=False; tileEnded=True
                else:
                    continue
            elif tile == 2:
                tile = miscTile; isMain=False
            elif tile == 3:
                useAlt = (ix%2==0 and iy%2==1)  # second term is always 0 in original
                if isMain:
                    tile = mainTileAlt if useAlt else mainTile
                else:
                    tile = mainStartTileAlt if useAlt else mainStartTile
                    isMain=True
                if endMainTile: needToEndTile=True
            else:  # PROP
                tile = propTile; needToEndTile=False
            draw_overwrite(ix*8 - cameraX, iy*8, tileset, tile)

# background parallax
if showBackground:
    draw_overwrite(16 - cameraX//28, 4, bg, 0)
map_draw()
# player: pos from map.init = (playerX*8+4, playerY*8+8); sprite origin (8,16)
ppx = playerX*8+4; ppy = playerY*8+8
draw_plus_mask(ppx - 8 - cameraX, ppy - 16, player_pm, 0)

# ---- convert 128x64 fb to 160x160 2bpp VRAM, viewport at (16,48) ----
BYTES_PER_LINE=48; VX=16; VY=48
PAL=[255,170,85,0]
vram = bytearray(0x2000)
for i in range(0x2000): vram[i]=0xFF  # black background (Arduboy bg)
for y in range(H):
    for bx in range(W//4):  # 32 bytes
        b=0
        for i in range(4):
            col=4*bx+i
            pix=(fb[(y>>3)*W+col]>>(y&7))&1   # 1=lit
            shade = 0 if pix else 3            # lit->white(0), else black(3)
            b |= shade << (i*2)
        vram[(VY+y)*BYTES_PER_LINE + (VX>>2) + bx] = b

# decode to PNG
def png(name):
    rows=[]
    for y in range(160):
        line=bytearray()
        for x in range(160):
            v=(vram[y*BYTES_PER_LINE + (x>>2)] >> ((x&3)*2)) & 3
            line.append(PAL[v])
        rows.append(b'\x00'+bytes(line))
    raw=b''.join(rows)
    def ch(t,d):
        c=t+d; return struct.pack('>I',len(d))+c+struct.pack('>I',zlib.crc32(c)&0xffffffff)
    out=b'\x89PNG\r\n\x1a\n'
    out+=ch(b'IHDR',struct.pack('>IIBBBBB',160,160,8,0,0,0,0))
    out+=ch(b'IDAT',zlib.compress(raw,9))
    out+=ch(b'IEND',b'')
    open(name,'wb').write(out); print("wrote",name)
png('proto.png')
print("player at", ppx, ppy, "map", width,"x",height)
