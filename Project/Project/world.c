#include "world.h"
#include "battle.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ============ ZONE DEFINITIONS ============
const Zone zones[NUM_ZONES] = {
    { "SECTOR ALPHA", "- CALIBRATION FIELD -",  {255, 255, 255, 255}, 14, 5, -1, ZONE_BETA },
    { "SECTOR BETA",  "- INDUSTRIAL RUINS -",   {255, 220, 200, 255}, 18, 7, ZONE_ALPHA, ZONE_GAMMA },
    { "SECTOR GAMMA", "- APEX WASTELAND -",     {220, 200, 255, 255}, 22, 9, ZONE_BETA, -1 },
};

// ============ STARTERS ============
const StarterLine starters[NUM_STARTERS] = {
    {
        "NOVA", "Balanced frontline mech",
        { MODEL_NOVA, MODEL_RAZOR, MODEL_OBLIVION },
        { 0, 3, 8 },
        { 120, 230, 255, 255 },
        "Starts even across the board. Evolves into an aggressive blade fighter, then an apex predator. A safe, versatile pick."
    },
    {
        "BULWARK", "Armored siege platform",
        { MODEL_BULWARK, MODEL_LONGBOW, MODEL_OBLIVION },
        { 0, 3, 8 },
        { 120, 255, 160, 255 },
        "Slow, heavily armored brawler. Evolves into a railgun sniper that hits from long range, then an apex predator. For patient players."
    },
    {
        "WISP", "Fast electronic warfare scout",
        { MODEL_WISP, MODEL_STATIC, MODEL_HAVOC },
        { 0, 3, 8 },
        { 255, 240, 140, 255 },
        "Fragile but extremely fast, with scrambling tools. Evolves into a jammer platform, then a missile battery. Rewards clever play."
    },
};

int playerStarter = -1;
int starterStage = 0;
int starterSlot = 0;
int obtainedStarters = 0;

Trainer trainers[NUM_TRAINERS];

// Per-zone maps
static unsigned char zoneMaps[NUM_ZONES][MAP_H][MAP_W];

static int currentZone = ZONE_ALPHA;
static int px, py;
static float pxF, pyF;
static int facing;

static int moving = 0;
static float moveT = 0;
static float moveFromX, moveFromY;
static int lastDir = -1;
static int justEnteredZone = 0;

static char message[256] = { 0 };
static float messageTimer = 0;

#define TERMINAL_MESSAGE "[TERMINAL] Repair bay: team restored. Hack mechs to grow your team!"

static unsigned char (*map)[MAP_W] = zoneMaps[ZONE_ALPHA];

// ============ MAP GEN ============
static void genZoneMap(int zoneIdx) {
    unsigned char (*m)[MAP_W] = zoneMaps[zoneIdx];
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            m[y][x] = T_GRID;

    for (int x = 0; x < MAP_W; x++) { m[0][x] = T_BLOCK; m[MAP_H - 1][x] = T_BLOCK; }
    for (int y = 0; y < MAP_H; y++) { m[y][0] = T_BLOCK; m[y][MAP_W - 1] = T_BLOCK; }

    int plasmaY0 = 5 - zoneIdx, plasmaY1 = 10 + zoneIdx;
    int plasmaX0 = 28 - zoneIdx, plasmaX1 = 36;
    if (plasmaY0 < 2) plasmaY0 = 2;
    if (plasmaY1 > MAP_H - 2) plasmaY1 = MAP_H - 2;
    for (int y = plasmaY0; y < plasmaY1; y++)
        for (int x = plasmaX0; x < plasmaX1; x++)
            m[y][x] = T_PLASMA;

    srand(42 + zoneIdx * 1000);
    int clusters = zones[zoneIdx].ruinsCount;
    for (int i = 0; i < clusters; i++) {
        int cx = 5 + rand() % 25, cy = 3 + rand() % 20;
        for (int y = cy; y < cy + 3 && y < MAP_H - 1; y++)
            for (int x = cx; x < cx + 4 && x < MAP_W - 1; x++)
                if (m[y][x] == T_GRID) m[y][x] = T_RUINS;
    }

    for (int x = 3; x < MAP_W - 3; x++) m[15][x] = T_PAD;
    for (int y = 3; y < 25; y++) m[y][15] = T_PAD;

    m[6][8] = T_BUNKER; m[6][9] = T_BUNKER;
    m[7][8] = T_BUNKER; m[7][9] = T_BUNKER;
    m[20][30] = T_BUNKER; m[20][31] = T_BUNKER;
    m[21][30] = T_BUNKER; m[21][31] = T_BUNKER;

    m[14][15] = T_TERMINAL;

    if (zones[zoneIdx].gateWest >= 0)  m[15][1] = T_GATE;
    if (zones[zoneIdx].gateEast >= 0)  m[15][MAP_W - 2] = T_GATE;

    if (zones[zoneIdx].gateWest >= 0) { m[15][2] = T_PAD; }
    if (zones[zoneIdx].gateEast >= 0) { m[15][MAP_W - 3] = T_PAD; }
}

static int isSolid(int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 1;
    int t = map[y][x];
    return (t == T_BLOCK || t == T_PLASMA || t == T_BUNKER || t == T_TERMINAL);
}

// ============ TRAINERS ============
static void initTrainers(void) {
    trainers[0] = (Trainer){
        "PILOT RHEA", "IRON LEGION", 10, 12, ZONE_ALPHA, 0, { 255, 120, 200, 255 },
        "Hey rookie! Let's see what you've got!",
        "You're stronger than you look...",
        "Try the east route to reach Sector Beta.",
        0, 0, { ARCH_SKIRMISHER }, { 1 }, 1, 0
    };
    trainers[1] = (Trainer){
        "SCOUT DANE", "IRON LEGION", 22, 8, ZONE_ALPHA, 0, { 255, 200, 100, 255 },
        "Fast mechs win wars, rookie!",
        "Speed wasn't enough...",
        "Beta's got tougher pilots.",
        0, 0, { ARCH_PROWLER }, { 1 }, 1, 0
    };
    trainers[2] = (Trainer){
        "COMMANDER VOLK", "IRON LEGION", 25, 18, ZONE_BETA, 0, { 255, 180, 60, 255 },
        "You dare challenge the Iron Legion?",
        "IMPOSSIBLE! My mechs... destroyed!",
        "You've earned my respect, pilot.",
        1, 0, { ARCH_BRAWLER, ARCH_BERSERKER }, { 3, 3 }, 2, 0
    };
    trainers[3] = (Trainer){
        "ENGINEER KESS", "IRON LEGION", 8, 22, ZONE_BETA, 0, { 120, 220, 160, 255 },
        "My machines never break. Yours will.",
        "Fascinating... your tactics are... effective.",
        "Gamma is the final frontier.",
        1, 0, { ARCH_JAMMER, ARCH_SNIPER }, { 3, 3 }, 2, 0
    };
    // Zone Gamma - elite tier (Gamma's plasma lake covers rows 3-11 at x 26-35)
    trainers[4] = (Trainer){
        "WARDEN KRUX", "IRON LEGION", 14, 22, ZONE_GAMMA, 0, { 255, 60, 60, 255 },
        "Only the strongest reach me. Prepare to be crushed.",
        "...You ARE the apex. Well fought.",
        "The wasteland is yours. Go.",
        2, 0, { ARCH_BOMBARD, ARCH_BRAWLER, ARCH_ORDNANCE }, { 5, 5, 7 }, 3, 0
    };
    trainers[5] = (Trainer){
        "GHOST ECHO", "IRON LEGION", 30, 12, ZONE_GAMMA, 0, { 200, 100, 255, 255 },
        "You cannot hit what you cannot see.",
        "Even my stealth... failed.",
        "Krux awaits at the center.",
        2, 0, { ARCH_SKIRMISHER, ARCH_BERSERKER, ARCH_BOMBARD }, { 5, 6, 6 }, 3, 0
    };

    // Gamma boss: the machine itself. Firmware 3.0 with Recursive Targeting and Dead-Man Protocol.
    trainers[6] = (Trainer){
        "FACTORY OVERSEER", "BLACK BOX", 32, 24, ZONE_GAMMA, 0, { 200, 60, 255, 255 },
        "INTRUDER DETECTED. EXECUTING RECURSIVE TARGETING.",
        "CORE FAILURE... DEAD-MAN PROTOCOL... COMPLETE.",
        "...the Overseer's chassis sits silent.",
        3, 0, { ARCH_OVERSEER }, { 12 }, 1, 0
    };
}

void worldInit(void) {
    for (int z = 0; z < NUM_ZONES; z++) genZoneMap(z);
    initTrainers();
    currentZone = ZONE_ALPHA;
    map = zoneMaps[currentZone];
    px = 15; py = 16; facing = 0;
    pxF = (float)(px * TILE_SIZE); pyF = (float)(py * TILE_SIZE);
    justEnteredZone = 1;
}

void worldInitNewGame(int starterIdx) {
    if (starterIdx < 0 || starterIdx >= NUM_STARTERS) starterIdx = 0;
    playerStarter = starterIdx;
    starterStage = 0;
    starterSlot = 0;
    obtainedStarters = (1 << starterIdx);
    const StarterLine* line = &starters[starterIdx];
    int model = line->stages[0];

    teamSize = 0;
    activeTeamSlot = 0;
    team[0] = mechCreateStock(model, 0);
    snprintf(team[0].name, sizeof(team[0].name), "%s-01", line->name);

    if (starterIdx == 0)      firmwareInstall(&team[0].fw, 0, CHIP_PREDICTIVE_TARGETING);
    else if (starterIdx == 1) firmwareInstall(&team[0].fw, 0, CHIP_HARDENED_KERNEL);
    else                       firmwareInstall(&team[0].fw, 0, CHIP_COUNTER_INTRUSION);
    mechRepair(&team[0]);
    teamSize = 1;
}

int worldStarterSlot(void) { return starterSlot; }

static int checkStarterEvolution(void) {
    if (playerStarter < 0 || starterStage >= NUM_STARTER_STAGES - 1) return 0;
    if (starterSlot < 0 || starterSlot >= teamSize) return 0;
    Mech* m = &team[starterSlot];
    const StarterLine* line = &starters[playerStarter];
    int need = line->evolveLevel[starterStage + 1];
    if (need <= 0 || m->fw.revision < need) return 0;

    starterStage++;
    int newModel = line->stages[starterStage];
    int keptRevision = m->fw.revision;
    Mech evolved = mechCreateStock(newModel, keptRevision);
    snprintf(evolved.name, sizeof(evolved.name), "%s-%s",
        line->name, starterStage == 1 ? "MK2" : "PRIME");
    for (int s = 0; s < MAX_SOCKETS; s++) evolved.fw.chips[s] = m->fw.chips[s];
    evolved.fw.optPoints = m->fw.optPoints;
    for (int i = 0; i < NUM_OPTIMIZATIONS; i++) evolved.fw.optPicks[i] = m->fw.optPicks[i];
    mechRefreshStats(&evolved);
    mechRepair(&evolved);
    team[starterSlot] = evolved;

    showMessage(TextFormat(">> EVOLUTION! Your %s evolved into %s!", line->name,
        mechModel(&team[starterSlot])->name), 4.0f);
    return 1;
}

int worldTryStarterEvolution(void) { return checkStarterEvolution(); }

// Give the player the other starters as they progress through the trainer ladder.
// After 2 defeats: first missing starter. After 4 defeats: second missing starter.
// The player's own pick is always marked as obtained at new-game time, so we
// only gift the two they don't have.
void worldOfferAlternateStarters(void) {
    int totalDefeated = 0;
    for (int i = 0; i < NUM_TRAINERS; i++) if (trainers[i].defeated) totalDefeated++;

    // Find the two starters the player doesn't have yet
    int missing[2] = { -1, -1 };
    int missingCount = 0;
    for (int i = 0; i < NUM_STARTERS; i++) {
        if (!(obtainedStarters & (1 << i))) {
            if (missingCount < 2) missing[missingCount] = i;
            missingCount++;
        }
    }
    if (missingCount == 0) return;

    // After 2 trainer defeats, gift the first missing starter
    if (totalDefeated >= 2 && missing[0] >= 0 && teamSize < MAX_TEAM) {
        Mech m = mechCreateStock(starters[missing[0]].stages[0], 0);
        snprintf(m.name, sizeof(m.name), "%s-01", starters[missing[0]].name);
        // Give them a thematic starting chip too, so they feel complete
        int idx = missing[0];
        if (idx == 0)      firmwareInstall(&m.fw, 0, CHIP_PREDICTIVE_TARGETING);
        else if (idx == 1) firmwareInstall(&m.fw, 0, CHIP_HARDENED_KERNEL);
        else               firmwareInstall(&m.fw, 0, CHIP_COUNTER_INTRUSION);
        mechRepair(&m);
        if (rosterAdd(&m)) {
            obtainedStarters |= (1 << missing[0]);
            showMessage(TextFormat(">> FIELD RECOVERY: %s added to your team!", starters[missing[0]].name), 4.0f);
        }
    }

    // After 4 trainer defeats, gift the second missing starter
    if (totalDefeated >= 4 && missingCount >= 2 && missing[1] >= 0 && teamSize < MAX_TEAM) {
        Mech m = mechCreateStock(starters[missing[1]].stages[0], 0);
        snprintf(m.name, sizeof(m.name), "%s-01", starters[missing[1]].name);
        int idx = missing[1];
        if (idx == 0)      firmwareInstall(&m.fw, 0, CHIP_PREDICTIVE_TARGETING);
        else if (idx == 1) firmwareInstall(&m.fw, 0, CHIP_HARDENED_KERNEL);
        else               firmwareInstall(&m.fw, 0, CHIP_COUNTER_INTRUSION);
        mechRepair(&m);
        if (rosterAdd(&m)) {
            obtainedStarters |= (1 << missing[1]);
            showMessage(TextFormat(">> FIELD RECOVERY: %s added to your team!", starters[missing[1]].name), 4.0f);
        }
    }
}

int worldCurrentZone(void) { return currentZone; }
const char* worldCurrentZoneName(void) { return zones[currentZone].name; }
const char* worldCurrentZoneSubtitle(void) { return zones[currentZone].subtitle; }

void showMessage(const char* msg, float dur) {
    strncpy(message, msg, sizeof(message) - 1);
    message[sizeof(message) - 1] = 0;
    messageTimer = dur;
}

// ============ ZONE TRANSITION ============
static void enterZone(int zoneIdx, int fromEast) {
    currentZone = zoneIdx;
    map = zoneMaps[currentZone];
    if (fromEast) { px = MAP_W - 3; }
    else { px = 2; }
    py = 15;
    facing = fromEast ? 2 : 3;
    pxF = (float)(px * TILE_SIZE);
    pyF = (float)(py * TILE_SIZE);
    moving = 0;
    moveT = 0;
    justEnteredZone = 1;
    showMessage(TextFormat(">> %s  %s", zones[zoneIdx].name, zones[zoneIdx].subtitle), 2.5f);
}

static int tryGateTransition(int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 0;
    if (map[y][x] != T_GATE) return 0;
    if (x <= 1) {
        int target = zones[currentZone].gateWest;
        if (target >= 0) { enterZone(target, 0); return 1; }
    }
    else if (x >= MAP_W - 2) {
        int target = zones[currentZone].gateEast;
        if (target >= 0) { enterZone(target, 1); return 1; }
    }
    return 0;
}

// ============ BATTLE TRIGGER ============
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

    int arrived = 0;
    float carry = 0;
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

    if (arrived && !justEnteredZone && map[py][px] == T_RUINS) {
        if (rand() % 100 < zones[currentZone].baseEncounter) {
            battleStartWild();
            *state = STATE_BATTLE;
            return;
        }
    }
    justEnteredZone = 0;

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
            if (trainers[i].zone != currentZone) continue;
            if (trainers[i].x == fx && trainers[i].y == fy) {
                if (triggerTrainerEncounter(i)) { *state = STATE_BATTLE; return; }
                break;
            }
        }
        if (fx >= 0 && fy >= 0 && fx < MAP_W && fy < MAP_H && map[fy][fx] == T_TERMINAL)
            useTerminal();
        if (fx >= 0 && fy >= 0 && fx < MAP_W && fy < MAP_H && map[fy][fx] == T_GATE)
            tryGateTransition(fx, fy);
    }

    for (int d = 0; d < 4; d++) if (dirPressed(d)) lastDir = d;
    int dir = -1;
    if (lastDir >= 0 && dirDown(lastDir)) dir = lastDir;
    else for (int d = 0; d < 4; d++) if (dirDown(d)) { dir = d; break; }
    if (dir < 0 || messageTimer > 0) return;

    int fresh = dirPressed(dir) || arrived;
    int dx = (dir == 3) - (dir == 2);
    int dy = (dir == 0) - (dir == 1);
    int nx = px + dx, ny = py + dy;
    facing = dir;

    for (int i = 0; i < NUM_TRAINERS; i++) {
        if (trainers[i].zone != currentZone) continue;
        if (trainers[i].x == nx && trainers[i].y == ny) {
            if (fresh && triggerTrainerEncounter(i)) *state = STATE_BATTLE;
            return;
        }
    }

    if (map[ny][nx] == T_GATE) {
        if (fresh && tryGateTransition(nx, ny)) return;
        return;
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
        (Vector2) {
        screenX + 16.0f, screenY + 28.0f
    }, cape);
    DrawTriangle((Vector2) { screenX + 32.0f, screenY + 14.0f }, (Vector2) { screenX + 36.0f, screenY + 36.0f },
        (Vector2) {
        screenX + 24.0f, screenY + 28.0f
    }, cape);
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

static Color tint(Color c) {
    Color t = zones[currentZone].tint;
    return (Color) {
        (unsigned char)(c.r * t.r / 255),
            (unsigned char)(c.g * t.g / 255),
            (unsigned char)(c.b * t.b / 255),
            c.a
    };
}

static void drawTile(int x, int y, int screenX, int screenY) {
    int t = map[y][x];
    Rectangle r = { (float)screenX, (float)screenY, TILE_SIZE, TILE_SIZE };
    float pulse = 0.5f + 0.5f * sinf(glowTimer * 3.0f + x * 0.5f + y * 0.3f);
    switch (t) {
    case T_GRID:
        DrawRectangleRec(r, tint((Color) { 28, 32, 44, 255 }));
        DrawRectangleLines(screenX, screenY, TILE_SIZE, TILE_SIZE, tint((Color) { 45, 60, 90, 120 }));
        DrawRectangle(screenX + 18, screenY + 18, 4, 4, tint((Color) { 60, 140, 200, 80 }));
        break;
    case T_RUINS:
        DrawRectangleRec(r, tint((Color) { 50, 30, 30, 255 }));
        for (int i = 0; i < 5; i++)
            DrawRectangle(screenX + 4 + i * 8, screenY + 8, 5, 24, tint((Color) { 180, 80, 40, 200 }));
        DrawRectangleLines(screenX, screenY, TILE_SIZE, TILE_SIZE, tint((Color) { 255, 120, 40, 100 }));
        break;
    case T_BLOCK:
        DrawRectangleRec(r, tint((Color) { 55, 60, 80, 255 }));
        DrawRectangle(screenX + 4, screenY + 4, TILE_SIZE - 8, TILE_SIZE - 8, tint((Color) { 75, 82, 105, 255 }));
        DrawRectangle(screenX + 8, screenY + 8, TILE_SIZE - 16, TILE_SIZE - 16, tint((Color) { 45, 50, 70, 255 }));
        DrawRectangleLines(screenX + 4, screenY + 4, TILE_SIZE - 8, TILE_SIZE - 8, tint((Color) { 110, 130, 180, 200 }));
        break;
    case T_PLASMA:
        DrawRectangleRec(r, tint((Color) { 20, 10, 50, 255 }));
        DrawRectangle(screenX + 4, screenY + 4, TILE_SIZE - 8, TILE_SIZE - 8,
            tint((Color) { 100, 60, 255, (unsigned char)(140 + pulse * 80) }));
        DrawRectangle(screenX + 10, screenY + 10, TILE_SIZE - 20, TILE_SIZE - 20,
            tint((Color) { 180, 100, 255, (unsigned char)(120 + pulse * 80) }));
        DrawRectangleLines(screenX, screenY, TILE_SIZE, TILE_SIZE, tint((Color) { 220, 150, 255, 200 }));
        break;
    case T_PAD:
        DrawRectangleRec(r, tint((Color) { 20, 40, 55, 255 }));
        DrawRectangle(screenX + 6, screenY + 6, TILE_SIZE - 12, TILE_SIZE - 12,
            tint((Color) { 30, (unsigned char)(180 + pulse * 50), 220, 255 }));
        DrawRectangleLines(screenX + 6, screenY + 6, TILE_SIZE - 12, TILE_SIZE - 12, WHITE);
        DrawText("< >", screenX + 12, screenY + 12, 16, tint((Color) { 255, 255, 255, 180 }));
        break;
    case T_BUNKER:
        DrawRectangleRec(r, tint((Color) { 40, 45, 65, 255 }));
        DrawRectangle(screenX + 3, screenY + 3, TILE_SIZE - 6, TILE_SIZE - 6, tint((Color) { 70, 80, 110, 255 }));
        DrawRectangle(screenX + 8, screenY + 14, TILE_SIZE - 16, TILE_SIZE - 20, tint((Color) { 30, 35, 50, 255 }));
        DrawRectangleLines(screenX + 8, screenY + 14, TILE_SIZE - 16, TILE_SIZE - 20, tint((Color) { 140, 180, 220, 200 }));
        if ((int)(glowTimer * 2) % 2 == 0) DrawCircle(screenX + 20, screenY + 8, 2, RED);
        break;
    case T_TERMINAL:
        DrawRectangleRec(r, tint((Color) { 28, 32, 44, 255 }));
        DrawRectangle(screenX + 6, screenY + 6, TILE_SIZE - 12, TILE_SIZE - 12, tint((Color) { 20, 60, 60, 255 }));
        DrawRectangle(screenX + 10, screenY + 10, TILE_SIZE - 20, TILE_SIZE - 20,
            tint((Color) { 40, (unsigned char)(180 + pulse * 60), 160, 255 }));
        DrawText("T", screenX + 15, screenY + 10, 18, BLACK);
        break;
    case T_GATE:
        DrawRectangleRec(r, tint((Color) { 10, 20, 40, 255 }));
        {
            Color g = tint((Color) { 100, 240, 255, 255 });
            int glow = (int)(pulse * 90);
            DrawRectangle(screenX + 4, screenY + 4, TILE_SIZE - 8, TILE_SIZE - 8,
                (Color) {
                g.r, g.g, g.b, (unsigned char)(120 + glow)
            });
            DrawRectangleLinesEx(r, 2, WHITE);
            const char* arrow = (x <= 1) ? "<<" : ">>";
            DrawText(arrow, screenX + 6, screenY + 12, 16, (Color) { 10, 20, 40, 255 });
            DrawCircle(screenX + 20, screenY + 20, 3 + (int)(pulse * 2), WHITE);
        }
        break;
    }
}

static void drawHud(void) {
    Mech* m = rosterActive();
    const MechModel* model = mechModel(m);
    DrawRectangle(10, 10, 340, 146, (Color) { 15, 25, 45, 220 });
    DrawRectangleLines(10, 10, 340, 146, model->accent);
    DrawText("PILOT STATUS", 20, 15, 12, model->accent);
    DrawText(TextFormat("%s  FW %s", m->name, firmwareLabel(m->fw.revision)), 20, 30, 20, WHITE);
    DrawText(TextFormat("%s %s  -  %s", model->designation, model->name, roleName(mechRole(m))),
        20, 52, 12, (Color) { 180, 200, 220, 255 });
    const MechStats* s = &m->stats;
    drawIntegrityBar(20, 68, 320, 12, s->integrity, s->maxIntegrity);
    DrawText(TextFormat("INT %d/%d", s->integrity, s->maxIntegrity), 20, 82, 11, WHITE);
    drawArmorBar(20, 96, 320, 6, s->armor, s->maxArmor);
    DrawText(TextFormat("ARM %d/%d", s->armor, s->maxArmor), 180, 82, 11, (Color) { 150, 190, 240, 255 });
    drawDataBar(20, 110, 320, 6, m->fw.data, firmwareDataToNext(m->fw.revision));
    DrawText(TextFormat("DATA %d/%d", m->fw.data, firmwareDataToNext(m->fw.revision)),
        20, 119, 10, (Color) { 200, 170, 255, 255 });
    DrawText(TextFormat("FIRMWARE %s", firmwareLabel(m->fw.revision)), 20, 134, 12, (Color) { 200, 170, 255, 255 });

    int defeated = 0, total = 0;
    for (int i = 0; i < NUM_TRAINERS; i++) {
        if (trainers[i].zone == currentZone) {
            total++;
            if (trainers[i].defeated) defeated++;
        }
    }
    DrawRectangle(screenW - 240, 10, 230, 76, (Color) { 15, 25, 45, 200 });
    DrawRectangleLines(screenW - 240, 10, 230, 76, (Color) { 255, 200, 100, 180 });
    DrawText(zones[currentZone].name, screenW - 230, 16, 16, (Color) { 255, 220, 100, 255 });
    DrawText(zones[currentZone].subtitle, screenW - 230, 34, 10, (Color) { 200, 220, 240, 200 });
    DrawText(TextFormat("Legion: %d / %d", defeated, total), screenW - 230, 48, 12, WHITE);
    DrawText(TextFormat("SECTOR %02d-%02d", px, py), screenW - 230, 64, 11, (Color) { 150, 200, 255, 200 });

    const Zone* z = &zones[currentZone];
    int rx = screenW - 240;
    DrawRectangle(rx, 92, 230, 42, (Color) { 15, 25, 45, 200 });
    DrawRectangleLines(rx, 92, 230, 42, (Color) { 100, 220, 255, 180 });
    DrawText("ROUTES", rx + 8, 96, 10, (Color) { 100, 240, 255, 255 });
    int ry = 110;
    if (z->gateWest >= 0)
        DrawText(TextFormat("< WEST: %s", zones[z->gateWest].name), rx + 8, ry, 11, (Color) { 180, 240, 255, 255 });
    if (z->gateEast >= 0)
        DrawText(TextFormat("> EAST: %s", zones[z->gateEast].name), rx + 8, ry, 11, (Color) { 180, 240, 255, 255 });
    if (z->gateWest < 0 && z->gateEast < 0)
        DrawText("no routes", rx + 8, ry, 11, (Color) { 150, 150, 150, 255 });

    DrawText("[WASD/ARROWS] Move   [Z/ENTER] Talk/Enter Gate   [TAB] Team   [F1] Debug   [ESC] Menu",
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
        if (trainers[i].zone != currentZone) continue;
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