/* map.c - tilemap, collision and auto-tiling renderer.
 * Ported from CastleBoy map.cpp (c) 2016 dir3kt/jlauener (MIT). */
#include "castleboy.h"
#include "assets.h"

u8 map_width;
bool map_showBackground;
Entity* map_boss = NULL;

static const u8* tilemap;
static u8 map_height;
static u8 solidTileIndex;
static u8 mainTile, mainTileAlt, mainStartTile, mainStartTileAlt;
static bool endMainTile;
static u8 propTile, miscTile;

static u8 getTileAt(u8 x, u8 y)
{
    return (u8)((tilemap[(x * map_height + y) / 4] & (u8)(0x03 << ((y % 4) * 2))) >> ((y % 4) * 2));
}

void map_init(const u8* source)
{
    u8 temp, playerX, playerY, entityCount, i, entityType, x, y;
    bool hasBoss;
    Entity* entity;

    map_width = *source;
    map_height = *(++source);

    temp = *(++source);
    hasBoss = (temp & 0xC0) == 0xC0;

    if (temp & 0x80) {                      /* indoor */
        solidTileIndex = 3;
        mainTile = TILE_WALL; mainTileAlt = TILE_WALL_ALT;
        mainStartTile = TILE_WALL; mainStartTileAlt = TILE_WALL_ALT;
        endMainTile = false;
        miscTile = TILE_CHAIN;
        propTile = TILE_WINDOW;
        map_showBackground = false;
    } else {                                /* outdoor / cave */
        solidTileIndex = 2;
        mainTile = TILE_GROUND; mainTileAlt = TILE_GROUND_ALT;
        miscTile = TILE_WALL;
        if (temp & 0x40) {                  /* outdoor */
            mainStartTile = TILE_GROUND_START; mainStartTileAlt = TILE_GROUND_START_ALT;
            map_showBackground = true;
            propTile = TILE_GRAVE;
        } else {                            /* cave */
            mainStartTile = TILE_GROUND; mainStartTileAlt = TILE_GROUND_ALT;
            map_showBackground = false;
            propTile = TILE_GRAVE;
        }
        endMainTile = true;
    }

    playerY = temp & 0x0F;
    playerX = *(++source);
    player_init(playerX * TILE_WIDTH + HALF_TILE_WIDTH, playerY * TILE_HEIGHT + TILE_HEIGHT);

    tilemap = ++source;

    source += (u16)map_width * map_height / 4;
    entityCount = *source;
    map_boss = NULL;
    for (i = 0; i < entityCount; ++i) {
        temp = *(++source);
        entityType = (u8)((temp & 0xF0) >> 4);
        x = *(++source);
        y = (u8)(temp & 0x0F);
        entity = entities_add(entityType, x * TILE_WIDTH + HALF_TILE_WIDTH, y * TILE_HEIGHT + TILE_HEIGHT);
        if (hasBoss) map_boss = entity;
    }
}

bool map_collide(s16 x, s8 y, const Box* hitbox)
{
    s16 tx1, tx2; s8 ty1, ty2, iy; s16 ix;

    x -= hitbox->x;
    y -= hitbox->y;

    if (x < 0) return true;

    tx1 = x / TILE_WIDTH;
    ty1 = y / TILE_HEIGHT;
    tx2 = (x + hitbox->width - 1) / TILE_WIDTH;
    ty2 = (y + hitbox->height - 1) / TILE_HEIGHT;

    if (ty2 < 0 || ty2 >= (s8)map_height) return false;

    if (tx1 < 0) tx1 = 0;
    if (tx2 >= (s16)map_width) tx2 = map_width - 1;
    if (ty1 < 0) ty1 = 0;
    if (ty2 >= (s8)map_height) ty2 = map_height - 1;

    for (ix = tx1; ix <= tx2; ++ix)
        for (iy = ty1; iy <= ty2; ++iy)
            if (getTileAt((u8)ix, (u8)iy) >= solidTileIndex)
                if (collide_rect(ix * TILE_WIDTH, iy * TILE_HEIGHT, TILE_WIDTH, TILE_HEIGHT,
                                 x, y, hitbox->width, hitbox->height))
                    return true;
    return false;
}

void map_draw(void)
{
    s16 base;
    u8 start, ix, iy, tile;
    bool isMain, needToEndTile, tileEnded, useAlt;

    /* hardware X-scroll absorbs movement within the slack; only redraw the tile
       strip on a recharge (camera moved past it). */
    if (!plat_set_camera(cameraX)) return;

    base = plat_strip_px0();
    plat_clear_strip();
    /* the whole strip is repainted; any pending save-under is now stale */
    plat_drop_saves();

    if (map_showBackground) {
        /* Static (non-parallax) backdrop: tiled across the whole 192px strip so
           it always fills the visible window. Repainted only on recharge (no
           per-frame background work) and scrolls 1:1 with the world. */
        s16 bgx;
        u8  bgw = background_mountain[0];   /* 120px */
        for (bgx = 0; bgx < 192; bgx += bgw)
            blit_bg(bgx, 4, background_mountain, 0);
    }

    start = (u8)(base / 8);
    for (ix = start; ix < start + 26 && ix < map_width; ++ix) {
        u16 colbase = (u16)ix * map_height;
        isMain = false; needToEndTile = false; tileEnded = false;
        for (iy = 0; iy < map_height; ++iy) {
            tile = (u8)((tilemap[(colbase + iy) >> 2] >> ((iy & 3) << 1)) & 3);
            if (tile == TILE_DATA_EMPTY) {
                if (!tileEnded && needToEndTile) {
                    tile = (ix % 2 == 0) ? TILE_SOLID_END : TILE_SOLID_END_ALT;
                    needToEndTile = false; tileEnded = true;
                } else continue;
            } else if (tile == TILE_DATA_MISC) {
                tile = miscTile; isMain = false;
            } else if (tile == TILE_DATA_MAIN) {
                useAlt = (ix % 2 == 0 && iy % 2 == 1);
                if (isMain) {
                    tile = useAlt ? mainTileAlt : mainTile;
                } else {
                    tile = useAlt ? mainStartTileAlt : mainStartTile;
                    isMain = true;
                }
                if (endMainTile) needToEndTile = true;
            } else {
                tile = propTile; needToEndTile = false;
            }
            blit_tile8((s16)ix * TILE_WIDTH - base, iy * TILE_HEIGHT, tileset, tile);
        }
    }

    /* candles are part of the static background: drawn into the strip here, so
       they scroll with the world and are not repainted every frame. */
    entities_draw_static(base);
}
