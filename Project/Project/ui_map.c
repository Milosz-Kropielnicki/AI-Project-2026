#include "ui.h"
#include "world.h"
#include "game.h"
#include <math.h>

// Full-screen world map. Everything in the world drawn to one canvas, at a
// scale that fits nicely, with region names, trainer pins, the player marker
// and a legend. Opens with M from the overworld.

static float openTimer = 0;

void uiMapOpen(void) { openTimer = 0; }

void uiMapUpdate(GameState* state) {
    openTimer += GetFrameTime();
    if (IsKeyPressed(KEY_M) || IsKeyPressed(KEY_ESCAPE) ||
        IsKeyPressed(KEY_TAB) || confirmPressed() || clickPressed()) {
        consumeInput();
        *state = STATE_OVERWORLD;
    }
}

void uiMapDraw(void) {
    ClearBackground((Color) { 6, 10, 20, 255 });

    // Player position comes from the world module (px/py are private to world.c)
    int playerZone = 0, px = 0, py = 0;
    worldGetPlayer(&playerZone, &px, &py);

    // Subtle grid behind everything
    float scroll = fmodf(glowTimer * 15, 40);
    for (int x = 0; x <= screenW / 40 + 1; x++)
        DrawLine(x * 40, 0, x * 40, SCREEN_H, (Color) { 20, 35, 60, 80 });
    for (int y = -1; y <= SCREEN_H / 40 + 1; y++)
        DrawLine(0, (int)(y * 40 + scroll), screenW, (int)(y * 40 + scroll), (Color) { 20, 35, 60, 80 });

    BeginMode2D(layoutCamera());

    // Title bar
    DrawRectangle(0, 0, SCREEN_W, 60, (Color) { 12, 22, 42, 255 });
    DrawRectangleLines(0, 0, SCREEN_W, 60, (Color) { 100, 200, 255, 220 });
    DrawText(">> WORLD MAP", 30, 18, 26, (Color) { 150, 220, 255, 255 });
    int worldDefeated = 0;
    for (int i = 0; i < NUM_TRAINERS; i++) if (trainers[i].defeated) worldDefeated++;
    DrawText(TextFormat("Hostiles cleared: %d / %d", worldDefeated, NUM_TRAINERS),
        520, 22, 16, WHITE);

    // Big map area. Fit it to the canvas, leave room for the legend strip.
    int mapX = 30, mapY = 80;
    int mapW = SCREEN_W - 260, mapH = SCREEN_H - 160;
    // Keep the world's aspect ratio (120 x 60 = 2:1)
    if ((float)mapW / mapH > (float)MAP_W / MAP_H)
        mapW = (int)(mapH * (float)MAP_W / MAP_H);
    else
        mapH = (int)(mapW * (float)MAP_H / MAP_W);

    // Frame
    DrawRectangle(mapX - 4, mapY - 4, mapW + 8, mapH + 8, (Color) { 10, 15, 30, 255 });
    DrawRectangleLines(mapX - 4, mapY - 4, mapW + 8, mapH + 8, (Color) { 120, 220, 255, 200 });

    // The map itself
    worldDrawMinimap(mapX, mapY, mapW, mapH);

    // Region name labels drawn inside their bounding boxes
    float sx = (float)mapW / MAP_W, sy = (float)mapH / MAP_H;
    for (int i = 0; i < NUM_REGIONS; i++) {
        const Region* r = &regions[i];
        int cx = mapX + (int)((r->minX + r->maxX) * 0.5f * sx);
        int cy = mapY + (int)(r->minY * sy) - 12;
        if (cy < mapY + 4) cy = mapY + 4;
        int tw = MeasureText(r->name, 14);
        DrawRectangle(cx - tw / 2 - 6, cy - 2, tw + 12, 18, (Color) { 8, 14, 28, 220 });
        DrawRectangleLines(cx - tw / 2 - 6, cy - 2, tw + 12, 18, r->tint);
        DrawText(r->name, cx - tw / 2, cy, 14, r->tint);
    }

    // Player callout: ring + name so the dot is easy to find
    int plx = mapX + (int)(px * sx);
    int ply = mapY + (int)(py * sy);
    float pulse = 0.5f + 0.5f * sinf(glowTimer * 4);
    DrawCircleLines(plx, ply, 8 + pulse * 4, (Color) { 120, 240, 255, 180 });
    DrawCircle(plx, ply, 3, WHITE);

    // Right-side legend panel
    int lx = mapX + mapW + 20, ly = mapY;
    DrawRectangle(lx, ly, 220, SCREEN_H - 160, (Color) { 12, 20, 38, 230 });
    DrawRectangleLines(lx, ly, 220, SCREEN_H - 160, (Color) { 100, 200, 255, 200 });

    DrawText("LEGEND", lx + 12, ly + 10, 16, (Color) { 150, 220, 255, 255 });
    int k = ly + 36;
    struct { Color c; const char* label; } keys[] = {
        { { 40, 45, 65, 255 }, "Obstacle" },
        { { 90, 140, 190, 255 }, "Path" },
        { { 60, 120, 70, 255 }, "Grass" },
        { { 140, 70, 40, 255 }, "Ruins" },
        { { 100, 60, 200, 255 }, "Plasma" },
        { { 90, 100, 130, 255 }, "Structure" },
    };
    for (int i = 0; i < 6; i++, k += 20) {
        DrawRectangle(lx + 12, k + 4, 12, 12, keys[i].c);
        DrawRectangleLines(lx + 12, k + 4, 12, 12, (Color) { 200, 220, 240, 200 });
        DrawText(keys[i].label, lx + 34, k + 4, 12, (Color) { 200, 220, 240, 255 });
    }

    k += 12;
    DrawLine(lx + 12, k, lx + 208, k, (Color) { 60, 100, 150, 200 });
    k += 10;
    DrawText("SECTORS", lx + 12, k, 14, (Color) { 150, 220, 255, 255 });
    k += 22;
    for (int i = 0; i < NUM_REGIONS; i++) {
        DrawRectangle(lx + 12, k + 3, 12, 12, regions[i].tint);
        DrawRectangleLines(lx + 12, k + 3, 12, 12, WHITE);
        DrawText(regions[i].name, lx + 34, k + 2, 12, regions[i].tint);
        k += 18;
    }

    k += 8;
    DrawLine(lx + 12, k, lx + 208, k, (Color) { 60, 100, 150, 200 });
    k += 10;
    DrawText("ENCOUNTERS", lx + 12, k, 14, (Color) { 150, 220, 255, 255 });
    k += 22;
    for (int i = 0; i < NUM_TRAINERS && k < SCREEN_H - 80; i++) {
        Color dot = trainers[i].defeated ? (Color) { 90, 90, 100, 255 } : trainers[i].color;
        DrawRectangle(lx + 12, k + 3, 12, 12, dot);
        DrawRectangleLines(lx + 12, k + 3, 12, 12, (Color) { 200, 220, 240, 200 });
        Color tc = trainers[i].defeated ? (Color) { 120, 130, 150, 255 } : WHITE;
        DrawText(trainers[i].name, lx + 34, k + 2, 11, tc);
        if (trainers[i].defeated) DrawText("X", lx + 200, k + 2, 11, (Color) { 255, 100, 100, 255 });
        k += 16;
    }

    // Position + controls
    DrawText(TextFormat("POSITION  %d, %d", px, py), lx + 12, SCREEN_H - 90, 12,
        (Color) {
        180, 220, 255, 255
    });
    DrawText(">>", lx + 12, SCREEN_H - 70, 14, (Color) { 150, 220, 255, 255 });
    DrawText("[M/ESC/TAB] Close map", lx + 34, SCREEN_H - 70, 12, (Color) { 190, 220, 240, 255 });

    EndMode2D();
}