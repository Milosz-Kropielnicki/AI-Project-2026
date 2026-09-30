#include "ui.h"
#include "mech.h"
#include "world.h"
#include "game.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Starter picker. Three big cards side by side, each showing the chassis
// sprite, name, tagline, and the full evolution line as a preview strip.
//
// Controls:
//   A / LEFT      previous starter
//   D / RIGHT     next starter
//   Z / ENTER / CLICK   confirm pick
//   Mouse hover   also selects

static int pickSel = 0;
static float pickedTimer = -1.0f;
static int pickedIdx = -1;

static Rectangle cardRect(int i) {
    float w = 230, h = 400;
    float gap = 24;
    float totalW = NUM_STARTERS * w + (NUM_STARTERS - 1) * gap;
    float x0 = (SCREEN_W - totalW) / 2;
    return (Rectangle) { x0 + i * (w + gap), 110, w, h };
}

// Word-wraps `text` into lines that fit `maxW` at `fontSize`, up to `maxLines`.
// Fills `lines` with pointers into `buf` (which is modified in place).
static int wrapText(char* buf, int bufSize, const char* text, int fontSize, int maxW,
    const char** lines, int maxLines) {
    strncpy(buf, text, bufSize - 1);
    buf[bufSize - 1] = 0;

    int count = 0;
    char* p = buf;
    while (*p && count < maxLines) {
        // Skip leading spaces
        while (*p == ' ') p++;
        if (!*p) break;

        // Find the furthest word boundary that fits
        char* lineStart = p;
        char* lastFit = NULL;
        char* scan = p;
        while (*scan) {
            // Advance to end of this word
            while (*scan && *scan != ' ') scan++;
            char saved = *scan;
            *scan = 0;
            if (MeasureText(lineStart, fontSize) <= maxW) {
                lastFit = scan;
            }
            else {
                *scan = saved;
                break;
            }
            *scan = saved;
            if (!*scan) { lastFit = scan; break; }
            scan++; // skip the space
        }
        if (!lastFit) lastFit = scan;
        if (*lastFit == ' ') *lastFit = 0;

        lines[count++] = lineStart;
        if (!*lastFit) break;
        p = lastFit + 1;
    }
    return count;
}

void uiStarterOpen(void) {
    pickSel = 0;
    pickedTimer = -1.0f;
    pickedIdx = -1;
}

void uiStarterUpdate(float dt, GameState* state) {
    // Confirm animation: lock input, then deploy
    if (pickedTimer >= 0) {
        pickedTimer -= dt;
        if (pickedTimer <= 0) {
            gameChooseStarter(pickedIdx);
            *state = STATE_OVERWORLD;
        }
        return;
    }

    // Direct key reads so A/D always work even if a previous screen
    // left inputConsumed set on this frame.
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A))
        pickSel = (pickSel + NUM_STARTERS - 1) % NUM_STARTERS;
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))
        pickSel = (pickSel + 1) % NUM_STARTERS;

    // Mouse hover selects
    for (int i = 0; i < NUM_STARTERS; i++) {
        if (mouseMoved() && mouseOver(cardRect(i))) pickSel = i;
    }
    // Mouse click confirms
    for (int i = 0; i < NUM_STARTERS; i++) {
        if (clickedOn(cardRect(i))) {
            consumeInput();
            pickSel = i;
            pickedIdx = i;
            pickedTimer = 1.4f;
            return;
        }
    }
    // Keyboard confirm
    if (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        consumeInput();
        pickedIdx = pickSel;
        pickedTimer = 1.4f;
    }
}

// Draws a numbered evolution strip using single-character stage markers
// instead of full words, so nothing overflows the card width.
static void drawStageStrip(int x, int y, int w, const StarterLine* line, int highlightStage) {
    // Column centres: three evenly spaced cells inside the strip
    int cellW = w / 3;

    // Small headers row
    DrawText("STAGE 1", x + cellW * 0 / 2 + 6, y, 9, (Color) { 150, 180, 210, 220 });
    DrawText("STAGE 2", x + cellW * 1 + 6, y, 9, (Color) { 150, 180, 210, 220 });
    DrawText("STAGE 3", x + cellW * 2 + 6, y, 9, (Color) { 150, 180, 210, 220 });

    for (int s = 0; s < NUM_STARTER_STAGES; s++) {
        int cx = x + cellW * s + cellW / 2;
        int model = line->stages[s];
        drawMechBattle(model, cx, y + 32, 9, 0);

        // Level label centred under the sprite
        const char* lvText = s == 0 ? "LV 1" : TextFormat("LV %d", line->evolveLevel[s]);
        Color col = s <= highlightStage ? line->accent : (Color) { 110, 120, 140, 255 };
        int tw = MeasureText(lvText, 11);
        DrawText(lvText, cx - tw / 2, y + 62, 11, col);

        // Arrow between stages, vertically centred on the sprites
        if (s < NUM_STARTER_STAGES - 1) {
            int ax = x + cellW * (s + 1);
            DrawText(">", ax - 4, y + 26, 16, (Color) { 150, 220, 255, 200 });
        }
    }
}

static void drawCard(int i, int selected) {
    Rectangle r = cardRect(i);
    const StarterLine* line = &starters[i];

    Color fill = selected ? (Color) { 30, 60, 100, 240 } : (Color) { 15, 25, 45, 220 };
    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, selected ? 3.0f : 1.0f,
        selected ? line->accent : (Color) { 60, 120, 180, 200 });

    // Header band
    Rectangle header = { r.x, r.y, r.width, 52 };
    DrawRectangleRec(header, (Color) { line->accent.r / 3, line->accent.g / 3, line->accent.b / 3, 255 });
    DrawRectangleLinesEx(header, 1, line->accent);

    int nameW = MeasureText(line->name, 24);
    DrawText(line->name, (int)(r.x + r.width / 2 - nameW / 2), (int)r.y + 8, 24, line->accent);
    int tagW = MeasureText(line->tagline, 10);
    DrawText(line->tagline, (int)(r.x + r.width / 2 - tagW / 2), (int)r.y + 34, 10,
        (Color) {
        200, 220, 240, 220
    });

    // Hero sprite in a pulsing ring
    int cx = (int)(r.x + r.width / 2);
    int cy = (int)(r.y + 118);
    float p = 0.5f + 0.5f * sinf(glowTimer * 3 + i);
    DrawCircle(cx, cy, 56 + (int)(p * 6), (Color) { line->accent.r, line->accent.g, line->accent.b, 40 });
    DrawCircle(cx, cy, 44, (Color) { line->accent.r, line->accent.g, line->accent.b, 60 });
    DrawCircleLines(cx, cy, 62, line->accent);
    drawMechBattle(line->stages[0], cx, cy, 13, 0);

    // Description: wrapped to fit the card width
    {
        char buf[256];
        const char* lines[4];
        int n = wrapText(buf, sizeof(buf), line->desc, 10,
            (int)r.width - 20, lines, 4);
        for (int k = 0; k < n; k++)
            DrawText(lines[k], (int)r.x + 10, (int)r.y + 176 + k * 12, 10,
                (Color) {
            200, 220, 240, 220
        });
    }

    // Evolution line strip
    int stripY = (int)r.y + 230;
    int stripH = 100;
    DrawRectangle((int)r.x + 8, stripY, (int)r.width - 16, stripH,
        (Color) {
        10, 20, 35, 220
    });
    DrawRectangleLines((int)r.x + 8, stripY, (int)r.width - 16, stripH,
        (Color) {
        line->accent.r / 2, line->accent.g / 2, line->accent.b / 2, 200
    });
    DrawText("EVOLUTION PATH", (int)r.x + 14, stripY + 4, 10, line->accent);
    drawStageStrip((int)r.x + 8, stripY + 18, (int)r.width - 16, line, 0);

    // Confirm hint
    if (selected) {
        int blink = (int)((sinf(glowTimer * 4) * 0.5f + 0.5f) * 200 + 55);
        Color hintCol = { line->accent.r, line->accent.g, line->accent.b, (unsigned char)blink };
        const char* hint = "[Z] SELECT";
        int hw = MeasureText(hint, 12);
        DrawText(hint, (int)(r.x + r.width / 2 - hw / 2), (int)r.y + stripY + stripH + 6, 12, hintCol);
    }
}

void uiStarterDraw(void) {
    ClearBackground((Color) { 6, 8, 18, 255 });

    float scroll = fmodf(glowTimer * 30, TILE_SIZE);
    for (int x = 0; x <= screenW / TILE_SIZE + 1; x++)
        DrawLine(x * TILE_SIZE, 0, x * TILE_SIZE, SCREEN_H, (Color) { 30, 50, 90, 90 });
    for (int y = -1; y <= SCREEN_H / TILE_SIZE + 1; y++)
        DrawLine(0, (int)(y * TILE_SIZE + scroll), screenW, (int)(y * TILE_SIZE + scroll),
            (Color) {
        30, 50, 90, 90
    });

    BeginMode2D(layoutCamera());

    const char* title = "CHOOSE YOUR MECH";
    int tw = MeasureText(title, 40);
    DrawText(title, SCREEN_W / 2 - tw / 2 + 2, 30, 40, (Color) { 20, 60, 120, 255 });
    DrawText(title, SCREEN_W / 2 - tw / 2, 28, 40, (Color) { 120, 230, 255, 255 });
    const char* sub = "Each chassis has its own evolution path and combat style.";
    int sw = MeasureText(sub, 13);
    DrawText(sub, SCREEN_W / 2 - sw / 2, 74, 13, (Color) { 200, 220, 240, 220 });

    for (int i = 0; i < NUM_STARTERS; i++) drawCard(i, i == pickSel);

    // Confirm animation overlay
    if (pickedTimer >= 0) {
        int a = (int)((1.0f - pickedTimer / 1.4f) * 255);
        if (a > 200) a = 200;
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color) { 0, 0, 0, (unsigned char)a });
        const StarterLine* line = &starters[pickedIdx];
        const char* msg = TextFormat(">> %s ONLINE <<", line->name);
        int mw = MeasureText(msg, 32);
        DrawText(msg, SCREEN_W / 2 - mw / 2, SCREEN_H / 2 - 20, 32, line->accent);
        const char* hint = "Deploying to Sector Alpha...";
        int hw = MeasureText(hint, 16);
        DrawText(hint, SCREEN_W / 2 - hw / 2, SCREEN_H / 2 + 30, 16, WHITE);
    }

    EndMode2D();

    // Footer controls drawn OUTSIDE the layout camera so they anchor to the
    // canvas regardless of aspect ratio / letterboxing.
    const char* footer = "[A/D or ARROWS] Switch     [Z / ENTER / CLICK] Confirm";
    int fw = MeasureText(footer, 15);
    int fx = screenW / 2 - fw / 2;
    int fy = SCREEN_H - 26;
    DrawRectangle(fx - 12, fy - 6, fw + 24, 24, (Color) { 10, 15, 30, 220 });
    DrawRectangleLines(fx - 12, fy - 6, fw + 24, 24, (Color) { 80, 220, 255, 200 });
    DrawText(footer, fx, fy, 15, (Color) { 180, 230, 255, 255 });
}