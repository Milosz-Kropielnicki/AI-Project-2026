#include "world.h"
#include "battle.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

Trainer trainers[NUM_TRAINERS];

static unsigned char map[MAP_H][MAP_W];
static int px, py;
static float pxF, pyF;
static int facing;
static int encounterChance = 14;

static int moving = 0;           // walking between two tiles
static float moveT = 0;          // 0..1 progress of the current step
static float moveFromX, moveFromY;
static int lastDir = -1;         // most recently pressed direction (facing convention)

static char message[256] = { 0 };
static float messageTimer = 0;

#define TERMINAL_MESSAGE "[TERMINAL] Repair bay: team restored. Hack mechs to grow your team!"

// ============ MAP GEN ============
static void genMap(void) {
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            map[y][x] = T_GRID;
    for (int x = 0; x < MAP_W; x++) { map[0][x] = T_BLOCK; map[MAP_H - 1][x] = T_BLOCK; }
    for (int y = 0; y < MAP_H; y++) { map[y][0] = T_BLOCK; map[y][MAP_W - 1] = T_BLOCK; }
    for (int y = 5; y < 10; y++)
        for (int x = 28; x < 36; x++) map[y][x] = T_PLASMA;
    srand(42);
    for (int i = 0; i < 5; i++) {
        int cx = 5 + rand() % 25, cy = 3 + rand() % 20;
        for (int y = cy; y < cy + 3 && y < MAP_H - 1; y++)
            for (int x = cx; x < cx + 4 && x < MAP_W - 1; x++)
                if (map[y][x] == T_GRID) map[y][x] = T_RUINS;
    }
    for (int x = 3; x < MAP_W - 3; x++) map[15][x] = T_PAD;
    for (int y = 3; y < 25; y++) map[y][15] = T_PAD;
    map[6][8] = T_BUNKER; map[6][9] = T_BUNKER;
    map[7][8] = T_BUNKER; map[7][9] = T_BUNKER;
    map[20][30] = T_BUNKER; map[20][31] = T_BUNKER;
    map[21][30] = T_BUNKER; map[21][31] = T_BUNKER;
    map[14][15] = T_TERMINAL;
}

static int isSolid(int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 1;
    int t = map[y][x];
    return (t == T_BLOCK || t == T_PLASMA || t == T_BUNKER || t == T_TERMINAL);
}

// ============ TRAINERS ============
static void initTrainers(void) {
    trainers[0] = (Trainer){
        "PILOT RHEA", "IRON LEGION", 10, 12, 0, { 255, 120, 200, 255 },
        "Hey rookie! Let's see what you've got!",
        "You're stronger than you look...",
        "Good luck out there, pilot.",
        0, 0,
        { MODEL_WISP }, { 1 }, 1, 0
    };
    trainers[1] = (Trainer){
        "COMMANDER VOLK", "IRON LEGION", 25, 18, 0, { 255, 180, 60, 255 },
        "You dare challenge the Iron Legion?",
        "IMPOSSIBLE! My mechs... destroyed!",
        "You've earned my respect, pilot.",
        1, 0,
        { MODEL_BULWARK, MODEL_RAZOR }, { 2, 2 }, 2, 0
    };
    trainers[2] = (Trainer){
        "WARDEN KRUX", "IRON LEGION", 14, 22, 0, { 255, 60, 60, 255 },
        "Only the strongest reach me. Prepare to be crushed.",
        "...You ARE the apex. Well fought.",
        "The wasteland is yours. Go.",
        2, 0,
        { MODEL_HAVOC, MODEL_BULWARK, MODEL_OBLIVION }, { 4, 4, 6 }, 3, 0
    };
}

void worldInit(void) {
    genMap();
    initTrainers();
    px = 15; py = 16; facing = 0;
    pxF = (float)(px * TILE_SIZE); pyF = (float)(py * TILE_SIZE);
}

void showMessage(const char* msg, float dur) {
    strncpy(message, msg, sizeof(message) - 1);
    message[sizeof(message) - 1] = 0;
    messageTimer = dur;
}

// Returns 1 if a battle started
static int triggerTrainerEncounter(int trainerIdx) {
    Trainer* t = &trainers[trainerIdx];
    if (t->defeated) {
        char buf[128];
        snprintf(buf, sizeof(buf), "\"%s\"", t->postLine);
        showMessage(buf, 3.0f);
        return 0;
    }
    battleStartTrainer(trainerIdx);
    return 1;
}

static void useTerminal(void) {
    for (int i = 0; i < teamSize; i++) mechRepair(&team[i]);
    showMessage(TERMINAL_MESSAGE, 3.5f);
}

// ============ UPDATE ============
void worldUpdate(float dt, GameState* state) {
    if (IsKeyPressed(KEY_TAB)) { *state = STATE_TEAM; return; }
    if (IsKeyPressed(KEY_ESCAPE)) { *state = STATE_MENU; return; }

    // Advance the current step between tiles
    int arrived = 0;
    float carry = 0;   // leftover step progress, so continuous walking doesn't stutter
    if (moving) {
        moveT += dt / MOVE_TIME;
        if (moveT >= 1) {
            carry = moveT - 1;
            moveT = 1;
            moving = 0;
            arrived = 1;
        }
        pxF = moveFromX + (px * TILE_SIZE - moveFromX) * moveT;
        pyF = moveFromY + (py * TILE_SIZE - moveFromY) * moveT;
    }

    if (arrived && map[py][px] == T_RUINS && rand() % 100 < encounterChance) {
        battleStartWild();
        *state = STATE_BATTLE;
        return;
    }
    if (messageTimer > 0) {
        messageTimer -= dt;
        if (confirmPressed() || clickPressed()) { messageTimer = 0; consumeInput(); }
        return;
    }
    if (moving) return;

    if (confirmPressed()) {
        consumeInput();
        int fx = px, fy = py;
        if (facing == 0) fy++;
        else if (facing == 1) fy--;
        else if (facing == 2) fx--;
        else if (facing == 3) fx++;
        for (int i = 0; i < NUM_TRAINERS; i++) {
            if (trainers[i].x == fx && trainers[i].y == fy) {
                if (triggerTrainerEncounter(i)) { *state = STATE_BATTLE; return; }
                break;
            }
        }
        if (fx >= 0 && fy >= 0 && fx < MAP_W && fy < MAP_H && map[fy][fx] == T_TERMINAL)
            useTerminal();
    }

    // Pick a direction: the most recently pressed one wins while it is held
    for (int d = 0; d < 4; d++) if (dirPressed(d)) lastDir = d;
    int dir = -1;
    if (lastDir >= 0 && dirDown(lastDir)) dir = lastDir;
    else for (int d = 0; d < 4; d++) if (dirDown(d)) { dir = d; break; }
    if (dir < 0 || messageTimer > 0) return;

    // Bumping into things only reacts to a fresh press or walking into them,
    // not to a key that is still held from before (e.g. after a battle).
    int fresh = dirPressed(dir) || arrived;
    int dx = (dir == 3) - (dir == 2);
    int dy = (dir == 0) - (dir == 1);
    int nx = px + dx, ny = py + dy;
    facing = dir;

    for (int i = 0; i < NUM_TRAINERS; i++) {
        if (trainers[i].x == nx && trainers[i].y == ny) {
            if (fresh && triggerTrainerEncounter(i)) *state = STATE_BATTLE;
            return;
        }
    }
    if (isSolid(nx, ny)) {
        if (fresh && map[ny][nx] == T_TERMINAL) useTerminal();
        return;
    }
    moveFromX = (float)(px * TILE_SIZE);
    moveFromY = (float)(py * TILE_SIZE);
    px = nx; py = ny;
    moving = 1;
    moveT = carry;
    pxF = moveFromX + (px * TILE_SIZE - moveFromX) * moveT;
    pyF = moveFromY + (py * TILE_SIZE - moveFromY) * moveT;
}

// ============ DRAW ============
static void drawTrainer(Trainer* t, int screenX, int screenY) {
    Color primary = t->color;
    DrawRectangle(screenX + 10, screenY + 26, 8, 12, (Color) { 30, 30, 40, 255 });
    DrawRectangle(screenX + 22, screenY + 26, 8, 12, (Color) { 30, 30, 40, 255 });
    DrawRectangle(screenX + 8, screenY + 12, 24, 16, primary);
    DrawRectangle(screenX + 8, screenY + 12, 24, 4, (Color) { 255, 255, 255, 80 });
    DrawRectangle(screenX + 4, screenY + 12, 6, 8, (Color) { primary.r / 2, primary.g / 2, primary.b / 2, 255 });
    DrawRectangle(screenX + 30, screenY + 12, 6, 8, (Color) { primary.r / 2, primary.g / 2, primary.b / 2, 255 });
    Color cape = { primary.r / 3, primary.g / 3, primary.b / 3, 220 };
    DrawTriangle((Vector2) { screenX + 8.0f, screenY + 14.0f }, (Vector2) { screenX + 4.0f, screenY + 36.0f },
        (Vector2) { screenX + 16.0f, screenY + 28.0f }, cape);
    DrawTriangle((Vector2) { screenX + 32.0f, screenY + 14.0f }, (Vector2) { screenX + 36.0f, screenY + 36.0f },
        (Vector2) { screenX + 24.0f, screenY + 28.0f }, cape);
    DrawRectangle(screenX + 12, screenY + 4, 16, 10, (Color) { 60, 60, 70, 255 });
    DrawRectangle(screenX + 12, screenY + 4, 16, 2, primary);
    DrawRectangle(screenX + 14, screenY + 8, 12, 3, (Color) { 255, 255, 220, 255 });
    if (!t->defeated) {
        float bounce = sinf(glowTimer * 4) * 3;
        DrawText("!", screenX + 16, (int)(screenY - 14 + bounce), 22, (Color) { 255, 220, 80, 255 });
        if ((int)(glowTimer * 3) % 2 == 0)
            DrawCircle(screenX + 20, screenY - 4, 3, (Color) { 255, 80, 80, 255 });
    }
}

static void drawTile(int x, int y, int screenX, int screenY) {
    int t = map[y][x];
    Rectangle r = { (float)screenX, (float)screenY, TILE_SIZE, TILE_SIZE };
    float pulse = 0.5f + 0.5f * sinf(glowTimer * 3.0f + x * 0.5f + y * 0.3f);
    switch (t) {
    case T_GRID:
        DrawRectangleRec(r, (Color) { 28, 32, 44, 255 });
        DrawRectangleLines(screenX, screenY, TILE_SIZE, TILE_SIZE, (Color) { 45, 60, 90, 120 });
        DrawRectangle(screenX + 18, screenY + 18, 4, 4, (Color) { 60, 140, 200, 80 });
        break;
    case T_RUINS:
        DrawRectangleRec(r, (Color) { 50, 30, 30, 255 });
        for (int i = 0; i < 5; i++)
            DrawRectangle(screenX + 4 + i * 8, screenY + 8, 5, 24, (Color) { 180, 80, 40, 200 });
        DrawRectangleLines(screenX, screenY, TILE_SIZE, TILE_SIZE, (Color) { 255, 120, 40, 100 });
        break;
    case T_BLOCK:
        DrawRectangleRec(r, (Color) { 55, 60, 80, 255 });
        DrawRectangle(screenX + 4, screenY + 4, TILE_SIZE - 8, TILE_SIZE - 8, (Color) { 75, 82, 105, 255 });
        DrawRectangle(screenX + 8, screenY + 8, TILE_SIZE - 16, TILE_SIZE - 16, (Color) { 45, 50, 70, 255 });
        DrawRectangleLines(screenX + 4, screenY + 4, TILE_SIZE - 8, TILE_SIZE - 8, (Color) { 110, 130, 180, 200 });
        break;
    case T_PLASMA:
        DrawRectangleRec(r, (Color) { 20, 10, 50, 255 });
        DrawRectangle(screenX + 4, screenY + 4, TILE_SIZE - 8, TILE_SIZE - 8,
            (Color) { 100, 60, 255, (unsigned char)(140 + pulse * 80) });
        DrawRectangle(screenX + 10, screenY + 10, TILE_SIZE - 20, TILE_SIZE - 20,
            (Color) { 180, 100, 255, (unsigned char)(120 + pulse * 80) });
        DrawRectangleLines(screenX, screenY, TILE_SIZE, TILE_SIZE, (Color) { 220, 150, 255, 200 });
        break;
    case T_PAD:
        DrawRectangleRec(r, (Color) { 20, 40, 55, 255 });
        DrawRectangle(screenX + 6, screenY + 6, TILE_SIZE - 12, TILE_SIZE - 12,
            (Color) { 30, (unsigned char)(180 + pulse * 50), 220, 255 });
        DrawRectangleLines(screenX + 6, screenY + 6, TILE_SIZE - 12, TILE_SIZE - 12, WHITE);
        DrawText("< >", screenX + 12, screenY + 12, 16, (Color) { 255, 255, 255, 180 });
        break;
    case T_BUNKER:
        DrawRectangleRec(r, (Color) { 40, 45, 65, 255 });
        DrawRectangle(screenX + 3, screenY + 3, TILE_SIZE - 6, TILE_SIZE - 6, (Color) { 70, 80, 110, 255 });
        DrawRectangle(screenX + 8, screenY + 14, TILE_SIZE - 16, TILE_SIZE - 20, (Color) { 30, 35, 50, 255 });
        DrawRectangleLines(screenX + 8, screenY + 14, TILE_SIZE - 16, TILE_SIZE - 20, (Color) { 140, 180, 220, 200 });
        if ((int)(glowTimer * 2) % 2 == 0) DrawCircle(screenX + 20, screenY + 8, 2, RED);
        break;
    case T_TERMINAL:
        DrawRectangleRec(r, (Color) { 28, 32, 44, 255 });
        DrawRectangle(screenX + 6, screenY + 6, TILE_SIZE - 12, TILE_SIZE - 12, (Color) { 20, 60, 60, 255 });
        DrawRectangle(screenX + 10, screenY + 10, TILE_SIZE - 20, TILE_SIZE - 20,
            (Color) { 40, (unsigned char)(180 + pulse * 60), 160, 255 });
        DrawText("T", screenX + 15, screenY + 10, 18, BLACK);
        break;
    }
}

static void drawHud(void) {
    Mech* m = rosterActive();
    const MechModel* model = mechModel(m);
    DrawRectangle(10, 10, 300, 124, (Color) { 15, 25, 45, 220 });
    DrawRectangleLines(10, 10, 300, 124, model->accent);
    DrawText("PILOT STATUS", 20, 15, 12, model->accent);
    DrawText(TextFormat("%s  FW %s", m->name, firmwareLabel(m->fw.revision)), 20, 30, 20, WHITE);
    DrawText(TextFormat("%s %s  -  %s", model->designation, model->name, roleNames[model->role]),
        20, 52, 12, (Color) { 180, 200, 220, 255 });
    int maxI = mechMaxIntegrity(m), maxA = mechMaxArmor(m);
    drawIntegrityBar(20, 68, 280, 12, m->integrity, maxI);
    DrawText(TextFormat("INT %d/%d", m->integrity, maxI), 20, 82, 11, WHITE);
    drawArmorBar(20, 96, 280, 6, m->armor, maxA);
    DrawText(TextFormat("ARM %d/%d", m->armor, maxA), 160, 82, 11, (Color) { 150, 190, 240, 255 });
    drawDataBar(20, 110, 280, 6, m->fw.data, firmwareDataToNext(m->fw.revision));
    DrawText(TextFormat("DATA %d/%d", m->fw.data, firmwareDataToNext(m->fw.revision)),
        20, 119, 10, (Color) { 200, 170, 255, 255 });

    // Trainer tracker
    int defeated = 0;
    for (int i = 0; i < NUM_TRAINERS; i++) if (trainers[i].defeated) defeated++;
    DrawRectangle(screenW - 200, 10, 190, 60, (Color) { 15, 25, 45, 200 });
    DrawRectangleLines(screenW - 200, 10, 190, 60, (Color) { 255, 200, 100, 180 });
    DrawText("IRON LEGION", screenW - 190, 16, 14, (Color) { 255, 220, 100, 255 });
    DrawText(TextFormat("Defeated: %d / %d", defeated, NUM_TRAINERS), screenW - 190, 36, 14, WHITE);
    DrawText(TextFormat("SECTOR %02d-%02d", px, py), screenW - 190, 54, 12, (Color) { 150, 200, 255, 200 });

    DrawText("[WASD/ARROWS] Move   [Z/ENTER] Talk   [TAB] Team   [F1] Debug   [ESC] Menu",
        10, SCREEN_H - 28, 16, (Color) { 150, 220, 255, 220 });

    if (messageTimer > 0) {
        DrawRectangle(40, SCREEN_H - 130, screenW - 80, 90, (Color) { 15, 25, 45, 240 });
        DrawRectangleLines(40, SCREEN_H - 130, screenW - 80, 90, (Color) { 80, 220, 255, 220 });
        DrawText(">> COMMS", 55, SCREEN_H - 122, 13, (Color) { 100, 240, 255, 255 });
        DrawText(message, 55, SCREEN_H - 100, 20, (Color) { 200, 240, 255, 255 });
    }
}

void worldDraw(void) {
    Camera2D camera = { 0 };
    camera.zoom = 1.0f;
    camera.offset = (Vector2){ screenW / 2.0f, SCREEN_H / 2.0f };
    camera.target = (Vector2){ pxF + TILE_SIZE / 2, pyF + TILE_SIZE / 2 };

    BeginMode2D(camera);
    int startX = (int)((camera.target.x - screenW / 2) / TILE_SIZE) - 1;
    int startY = (int)((camera.target.y - SCREEN_H / 2) / TILE_SIZE) - 1;
    int endX = startX + screenW / TILE_SIZE + 3;
    int endY = startY + SCREEN_H / TILE_SIZE + 3;
    if (startX < 0) startX = 0;
    if (startY < 0) startY = 0;
    if (endX > MAP_W) endX = MAP_W;
    if (endY > MAP_H) endY = MAP_H;

    for (int y = startY; y < endY; y++)
        for (int x = startX; x < endX; x++)
            drawTile(x, y, x * TILE_SIZE, y * TILE_SIZE);

    for (int i = 0; i < NUM_TRAINERS; i++) {
        if (!trainers[i].defeated)
            drawTrainer(&trainers[i], trainers[i].x * TILE_SIZE, trainers[i].y * TILE_SIZE);
        else {
            DrawRectangle(trainers[i].x * TILE_SIZE + 10, trainers[i].y * TILE_SIZE + 20,
                20, 12, (Color) { 60, 60, 70, 150 });
            DrawCircle(trainers[i].x * TILE_SIZE + 20, trainers[i].y * TILE_SIZE + 14,
                4, (Color) { 100, 100, 110, 150 });
        }
    }

    drawMechOverworld(rosterActive()->model, (int)pxF, (int)pyF, facing, glowTimer);
    EndMode2D();

    drawHud();
}
