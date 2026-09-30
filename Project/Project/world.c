#include "world.h"
#include "battle.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ============ REGIONS ============
// Regions are now compact "hub" areas: settlement-sized zones connected by
// narrow route corridors. The map is much bigger and mostly empty wilderness.
const Region regions[NUM_REGIONS] = {
    { "SECTOR ALPHA", "- CALIBRATION FIELD -",  {255, 255, 255, 255}, 14,  1, 1, 30, 28 },
    { "SECTOR BETA",  "- INDUSTRIAL RUINS -",   {255, 220, 200, 255}, 18, 46, 18, 76, 44 },
    { "SECTOR GAMMA", "- APEX WASTELAND -",     {220, 200, 255, 255}, 22, 90, 4, 118, 56 },
};

int worldRegionAt(int tileX, int tileY) {
    for (int i = 0; i < NUM_REGIONS; i++) {
        const Region* r = &regions[i];
        if (tileX >= r->minX && tileX <= r->maxX && tileY >= r->minY && tileY <= r->maxY)
            return i;
    }
    return -1;
}

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

// The one big map
static unsigned char map[MAP_H][MAP_W];

static int px, py;
static float pxF, pyF;
static int facing;

static int moving = 0;
static float moveT = 0;
static float moveFromX, moveFromY;
static int lastDir = -1;

static char message[256] = { 0 };
static float messageTimer = 0;

#define TERMINAL_MESSAGE "[TERMINAL] Repair bay: team restored. Hack mechs to grow your team!"

// ============ MAP GEN HELPERS ============
static void setTile(int x, int y, int t) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return;
    map[y][x] = (unsigned char)t;
}

static int getTile(int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return T_BLOCK;
    return map[y][x];
}

static void fillRect(int x0, int y0, int x1, int y1, int t) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            setTile(x, y, t);
}

// A route is a 2-tile-wide corridor. This carves one along a rectangle of
// grass with rubble framing, then re-carves the walkable floor. The rubble
// framing makes the corridor visually distinct from the open wilderness.
static void carveRouteRect(int x0, int y0, int x1, int y1) {
    // Frame: a 1-tile ring of rubble around the corridor rect
    for (int x = x0 - 1; x <= x1 + 1; x++) {
        if (getTile(x, y0 - 1) == T_GRID) setTile(x, y0 - 1, T_RUINS);
        if (getTile(x, y1 + 1) == T_GRID) setTile(x, y1 + 1, T_RUINS);
    }
    for (int y = y0 - 1; y <= y1 + 1; y++) {
        if (getTile(x0 - 1, y) == T_GRID) setTile(x0 - 1, y, T_RUINS);
        if (getTile(x1 + 1, y) == T_GRID) setTile(x1 + 1, y, T_RUINS);
    }
    // Floor: fill with grass first (so encounters roll), then overlay the
    // path so the corridor reads as walkable
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            setTile(x, y, T_GRASS);
    // A thin path stripe down the middle of the corridor
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if ((x - x0) == 0 || (y - y0) % 3 == 1)
                setTile(x, y, T_PAD);
}

// Cleaner helper for horizontal routes: 2 tiles tall
static void carveRouteH(int x0, int x1, int y) {
    carveRouteRect(x0, y, x1, y + 1);
}

// Cleaner helper for vertical routes: 2 tiles wide
static void carveRouteV(int y0, int y1, int x) {
    carveRouteRect(x, y0, x + 1, y1);
}

// ============ MAP GEN ============
static void genWorld(void) {
    // Base fill: solid impassable wilderness everywhere
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            map[y][x] = T_BLOCK;

    // Outer border stays solid (already is from the base fill)

    // ---- Alpha hub: an open settlement area ----
    // Interior floor
    fillRect(3, 4, 28, 26, T_GRID);
    // Grass patches inside the hub for wild encounters
    srand(1000);
    for (int i = 0; i < 6; i++) {
        int cx = 6 + rand() % 20;
        int cy = 6 + rand() % 18;
        for (int y = cy; y < cy + 3 && y < 26; y++)
            for (int x = cx; x < cx + 4 && x < 28; x++)
                setTile(x, y, T_GRASS);
    }
    // A couple of decorative walls
    fillRect(6, 8, 8, 8, T_BLOCK);
    fillRect(22, 20, 24, 21, T_BLOCK);
    // Terminal
    setTile(8, 15, T_TERMINAL);

    // ---- Route 1: Alpha -> Beta, narrow winding corridor ----
    // Goes east from Alpha's east edge, jogs south then east to Beta's west
    carveRouteH(28, 40, 8);      // east from Alpha at y=8
    carveRouteV(8, 20, 40);      // south along x=40
    carveRouteH(40, 46, 20);     // east into Beta at y=20

    // ---- Beta hub: industrial zone ----
    fillRect(46, 18, 76, 44, T_GRID);
    srand(2000);
    for (int i = 0; i < 8; i++) {
        int cx = 48 + rand() % 26;
        int cy = 20 + rand() % 22;
        for (int y = cy; y < cy + 3 && y < 44; y++)
            for (int x = cx; x < cx + 4 && x < 76; x++)
                setTile(x, y, T_RUINS);
    }
    // Bunkers scattered around Beta
    for (int i = 0; i < 8; i++) {
        int cx = 50 + rand() % 24;
        int cy = 20 + rand() % 22;
        setTile(cx, cy, T_BUNKER);
    }
    // Plasma pond
    fillRect(60, 26, 66, 30, T_PLASMA);
    // Terminal
    setTile(52, 30, T_TERMINAL);

    // ---- Route 2: Beta -> Gamma, long winding corridor ----
    carveRouteH(76, 90, 32);     // east from Beta at y=32
    carveRouteV(20, 32, 88);     // north along x=88
    carveRouteH(88, 92, 20);     // east into Gamma at y=20

    // ---- Gamma hub: apex wasteland ----
    fillRect(90, 4, 116, 56, T_GRID);
    srand(3000);
    for (int i = 0; i < 12; i++) {
        int cx = 92 + rand() % 22;
        int cy = 6 + rand() % 48;
        for (int y = cy; y < cy + 3 && y < 56; y++)
            for (int x = cx; x < cx + 4 && x < 116; x++)
                setTile(x, y, T_RUINS);
    }
    // Big plasma lakes
    fillRect(100, 8, 110, 14, T_PLASMA);
    fillRect(96, 38, 112, 48, T_PLASMA);
    // Solid jagged walls
    for (int i = 0; i < 10; i++) {
        int cx = 92 + rand() % 22;
        int cy = 6 + rand() % 48;
        int len = 2 + rand() % 4;
        for (int j = 0; j < len; j++)
            if (getTile(cx + j, cy) == T_GRID) setTile(cx + j, cy, T_BLOCK);
    }
    // Terminal
    setTile(94, 28, T_TERMINAL);

    // ---- Make sure the route corridors are still walkable ----
    // (random features above may have overwritten them)
    // Route 1
    carveRouteH(28, 40, 8);
    carveRouteV(8, 20, 40);
    carveRouteH(40, 46, 20);
    // Route 2
    carveRouteH(76, 90, 32);
    carveRouteV(20, 32, 88);
    carveRouteH(88, 92, 20);
}

static int isSolid(int x, int y) {
    int t = getTile(x, y);
    return (t == T_BLOCK || t == T_PLASMA || t == T_BUNKER || t == T_TERMINAL);
}

// An "encounter" tile is anything a mech might hide in
static int isEncounterTile(int x, int y) {
    int t = getTile(x, y);
    return (t == T_GRASS || t == T_RUINS);
}

// Is this tile inside a route corridor? Routes are marked by having a GRASS
// or PAD tile that sits outside every region rect. Used only for flavor
// (the HUD shows "ROUTE" instead of "WILDS" when the player is between hubs).
static int isRouteTile(int x, int y) {
    if (worldRegionAt(x, y) >= 0) return 0;
    int t = getTile(x, y);
    return (t == T_GRASS || t == T_PAD || t == T_RUINS);
}

// ============ TRAINERS ============
// Placed in hubs AND along the route corridors, so the player fights on the
// way from one region to the next.
static void initTrainers(void) {
    // --- Alpha hub (x 3..28, y 4..26) ---
    trainers[0] = (Trainer){
        "PILOT RHEA", "IRON LEGION", 14, 22, 0, { 255, 120, 200, 255 },
        "Hey rookie! Let's see what you've got!",
        "You're stronger than you look...",
        "Follow the east road. It leads to Route 1.",
        0, 0, { ARCH_SKIRMISHER }, { 1 }, 1, 0
    };
    trainers[1] = (Trainer){
        "SCOUT DANE", "IRON LEGION", 22, 12, 0, { 255, 200, 100, 255 },
        "Fast mechs win wars, rookie!",
        "Speed wasn't enough...",
        "Route 1's where the real fights start.",
        0, 0, { ARCH_PROWLER }, { 1 }, 1, 0
    };

    // --- Route 1 (between Alpha and Beta) ---
    trainers[2] = (Trainer){
        "ROUTE GUARD", "IRON LEGION", 34, 8, 0, { 255, 160, 100, 255 },
        "No one passes this road without a fight.",
        "Fine... you've earned the crossing.",
        "Keep heading east. Beta's not far.",
        1, 0, { ARCH_BRAWLER }, { 2 }, 1, 0
    };

    // --- Beta hub (x 46..76, y 18..44) ---
    trainers[3] = (Trainer){
        "COMMANDER VOLK", "IRON LEGION", 56, 24, 0, { 255, 180, 60, 255 },
        "You dare challenge the Iron Legion?",
        "IMPOSSIBLE! My mechs... destroyed!",
        "Route 2 leads to the wasteland beyond.",
        1, 0, { ARCH_BRAWLER, ARCH_BERSERKER }, { 3, 3 }, 2, 0
    };
    trainers[4] = (Trainer){
        "ENGINEER KESS", "IRON LEGION", 66, 38, 0, { 120, 220, 160, 255 },
        "My machines never break. Yours will.",
        "Fascinating... your tactics are... effective.",
        "Gamma is the final frontier.",
        1, 0, { ARCH_JAMMER, ARCH_SNIPER }, { 3, 3 }, 2, 0
    };

    // --- Route 2 (between Beta and Gamma) ---
    trainers[5] = (Trainer){
        "ROADBLOCK UNIT", "IRON LEGION", 88, 26, 0, { 255, 100, 100, 255 },
        "HALT. The wasteland is off-limits.",
        "AUTHORIZATION... REVOKED. Proceed.",
        "Warden Krux is waiting up north.",
        2, 0, { ARCH_BERSERKER, ARCH_BOMBARD }, { 4, 4 }, 2, 0
    };

    // --- Gamma hub (x 90..116, y 4..56) ---
    trainers[6] = (Trainer){
        "WARDEN KRUX", "IRON LEGION", 108, 30, 0, { 255, 60, 60, 255 },
        "Only the strongest reach me. Prepare to be crushed.",
        "...You ARE the apex. Well fought.",
        "The wasteland is yours. Go.",
        2, 0, { ARCH_BOMBARD, ARCH_BRAWLER, ARCH_ORDNANCE }, { 5, 5, 7 }, 3, 0
    };
}

void worldInit(void) {
    genWorld();
    initTrainers();
    px = 6; py = 15; facing = 3;   // start in Alpha's hub
    pxF = (float)(px * TILE_SIZE);
    pyF = (float)(py * TILE_SIZE);
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

    // Drop the player in the middle of Alpha's hub
    px = 6; py = 15; facing = 3;
    pxF = (float)(px * TILE_SIZE);
    pyF = (float)(py * TILE_SIZE);
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

void worldOfferAlternateStarters(void) {
    int totalDefeated = 0;
    for (int i = 0; i < NUM_TRAINERS; i++) if (trainers[i].defeated) totalDefeated++;

    int missing[2] = { -1, -1 };
    int missingCount = 0;
    for (int i = 0; i < NUM_STARTERS; i++) {
        if (!(obtainedStarters & (1 << i))) {
            if (missingCount < 2) missing[missingCount] = i;
            missingCount++;
        }
    }
    if (missingCount == 0) return;

    if (totalDefeated >= 2 && missing[0] >= 0 && teamSize < MAX_TEAM) {
        Mech m = mechCreateStock(starters[missing[0]].stages[0], 0);
        snprintf(m.name, sizeof(m.name), "%s-01", starters[missing[0]].name);
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

void showMessage(const char* msg, float dur) {
    strncpy(message, msg, sizeof(message) - 1);
    message[sizeof(message) - 1] = 0;
    messageTimer = dur;
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

    // Wild encounters: routes have their own encounter rate (a bit lower than
    // a region hub so corridors feel like travel, not a grind).
    if (arrived && isEncounterTile(px, py)) {
        int region = worldRegionAt(px, py);
        int rate;
        if (region >= 0) rate = regions[region].baseEncounter;
        else             rate = 12;   // routes: gentler than a full region
        if ((rand() % 100) < rate) {
            battleStartWild();
            *state = STATE_BATTLE;
            return;
        }
    }

    if (messageTimer > 0) {
        messageTimer -= dt;
        if (confirmPressed() || clickPressed()) { messageTimer = 0; consumeInput(); }
        return;
    }
    if (moving) return;

    // Interact with the tile in front of us
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
        if (getTile(fx, fy) == T_TERMINAL)
            useTerminal();
    }

    // Movement
    for (int d = 0; d < 4; d++) if (dirPressed(d)) lastDir = d;
    int dir = -1;
    if (lastDir >= 0 && dirDown(lastDir)) dir = lastDir;
    else for (int d = 0; d < 4; d++) if (dirDown(d)) { dir = d; break; }
    if (dir < 0) return;

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
        if (fresh && getTile(nx, ny) == T_TERMINAL) useTerminal();
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

// Tint a base color by the region at a given tile. Outside any region (i.e.
// on a route) tiles use a cool dim tint so corridors read as "in between".
static Color regionTint(int tx, int ty, Color c) {
    int region = worldRegionAt(tx, ty);
    if (region < 0) {
        // Route tint: dim blue-grey to make corridors feel enclosed
        return (Color) {
            (unsigned char)(c.r * 190 / 255),
                (unsigned char)(c.g * 200 / 255),
                (unsigned char)(c.b * 230 / 255),
                c.a
        };
    }
    Color t = regions[region].tint;
    return (Color) {
        (unsigned char)(c.r * t.r / 255),
            (unsigned char)(c.g * t.g / 255),
            (unsigned char)(c.b * t.b / 255),
            c.a
    };
}

static void drawTile(int x, int y, int screenX, int screenY) {
    int t = getTile(x, y);
    Rectangle r = { (float)screenX, (float)screenY, TILE_SIZE, TILE_SIZE };
    float pulse = 0.5f + 0.5f * sinf(glowTimer * 3.0f + x * 0.5f + y * 0.3f);
    switch (t) {
    case T_GRID:
        DrawRectangleRec(r, regionTint(x, y, (Color) { 28, 32, 44, 255 }));
        DrawRectangleLines(screenX, screenY, TILE_SIZE, TILE_SIZE, regionTint(x, y, (Color) { 45, 60, 90, 120 }));
        DrawRectangle(screenX + 18, screenY + 18, 4, 4, regionTint(x, y, (Color) { 60, 140, 200, 80 }));
        break;
    case T_GRASS:
        DrawRectangleRec(r, regionTint(x, y, (Color) { 45, 90, 55, 255 }));
        for (int i = 0; i < 6; i++) {
            int gx = screenX + 3 + (i * 6) % (TILE_SIZE - 6);
            int gy = screenY + 8 + (i * 11) % (TILE_SIZE - 12);
            DrawRectangle(gx, gy, 2, 6, regionTint(x, y, (Color) { 80, 160, 80, 255 }));
        }
        DrawRectangleLines(screenX, screenY, TILE_SIZE, TILE_SIZE, regionTint(x, y, (Color) { 90, 180, 90, 160 }));
        break;
    case T_RUINS:
        DrawRectangleRec(r, regionTint(x, y, (Color) { 50, 30, 30, 255 }));
        for (int i = 0; i < 5; i++)
            DrawRectangle(screenX + 4 + i * 8, screenY + 8, 5, 24, regionTint(x, y, (Color) { 180, 80, 40, 200 }));
        DrawRectangleLines(screenX, screenY, TILE_SIZE, TILE_SIZE, regionTint(x, y, (Color) { 255, 120, 40, 100 }));
        break;
    case T_BLOCK:
        DrawRectangleRec(r, regionTint(x, y, (Color) { 55, 60, 80, 255 }));
        DrawRectangle(screenX + 4, screenY + 4, TILE_SIZE - 8, TILE_SIZE - 8, regionTint(x, y, (Color) { 75, 82, 105, 255 }));
        DrawRectangle(screenX + 8, screenY + 8, TILE_SIZE - 16, TILE_SIZE - 16, regionTint(x, y, (Color) { 45, 50, 70, 255 }));
        DrawRectangleLines(screenX + 4, screenY + 4, TILE_SIZE - 8, TILE_SIZE - 8, regionTint(x, y, (Color) { 110, 130, 180, 200 }));
        break;
    case T_PLASMA:
        DrawRectangleRec(r, regionTint(x, y, (Color) { 20, 10, 50, 255 }));
        DrawRectangle(screenX + 4, screenY + 4, TILE_SIZE - 8, TILE_SIZE - 8,
            regionTint(x, y, (Color) { 100, 60, 255, (unsigned char)(140 + pulse * 80) }));
        DrawRectangle(screenX + 10, screenY + 10, TILE_SIZE - 20, TILE_SIZE - 20,
            regionTint(x, y, (Color) { 180, 100, 255, (unsigned char)(120 + pulse * 80) }));
        DrawRectangleLines(screenX, screenY, TILE_SIZE, TILE_SIZE, regionTint(x, y, (Color) { 220, 150, 255, 200 }));
        break;
    case T_PAD:
        DrawRectangleRec(r, regionTint(x, y, (Color) { 20, 40, 55, 255 }));
        DrawRectangle(screenX + 6, screenY + 6, TILE_SIZE - 12, TILE_SIZE - 12,
            regionTint(x, y, (Color) { 30, (unsigned char)(180 + pulse * 50), 220, 255 }));
        DrawRectangleLines(screenX + 6, screenY + 6, TILE_SIZE - 12, TILE_SIZE - 12, WHITE);
        DrawText("< >", screenX + 12, screenY + 12, 16, regionTint(x, y, (Color) { 255, 255, 255, 180 }));
        break;
    case T_BUNKER:
        DrawRectangleRec(r, regionTint(x, y, (Color) { 40, 45, 65, 255 }));
        DrawRectangle(screenX + 3, screenY + 3, TILE_SIZE - 6, TILE_SIZE - 6, regionTint(x, y, (Color) { 70, 80, 110, 255 }));
        DrawRectangle(screenX + 8, screenY + 14, TILE_SIZE - 16, TILE_SIZE - 20, regionTint(x, y, (Color) { 30, 35, 50, 255 }));
        DrawRectangleLines(screenX + 8, screenY + 14, TILE_SIZE - 16, TILE_SIZE - 20, regionTint(x, y, (Color) { 140, 180, 220, 200 }));
        if ((int)(glowTimer * 2) % 2 == 0) DrawCircle(screenX + 20, screenY + 8, 2, RED);
        break;
    case T_TERMINAL:
        DrawRectangleRec(r, regionTint(x, y, (Color) { 28, 32, 44, 255 }));
        DrawRectangle(screenX + 6, screenY + 6, TILE_SIZE - 12, TILE_SIZE - 12, regionTint(x, y, (Color) { 20, 60, 60, 255 }));
        DrawRectangle(screenX + 10, screenY + 10, TILE_SIZE - 20, TILE_SIZE - 20,
            regionTint(x, y, (Color) { 40, (unsigned char)(180 + pulse * 60), 160, 255 }));
        DrawText("T", screenX + 15, screenY + 10, 18, BLACK);
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

    // Location label: region, route, or wilderness
    int region = worldRegionAt(px, py);
    int onRoute = (region < 0) && isRouteTile(px, py);
    int worldDefeated = 0;
    for (int i = 0; i < NUM_TRAINERS; i++) if (trainers[i].defeated) worldDefeated++;

    DrawRectangle(screenW - 250, 10, 240, 96, (Color) { 15, 25, 45, 200 });
    if (region >= 0) {
        DrawRectangleLines(screenW - 250, 10, 240, 96, regions[region].tint);
        DrawText(regions[region].name, screenW - 240, 16, 16, regions[region].tint);
        DrawText(regions[region].subtitle, screenW - 240, 34, 10, (Color) { 200, 220, 240, 200 });
    }
    else if (onRoute) {
        DrawRectangleLines(screenW - 250, 10, 240, 96, (Color) { 120, 200, 255, 200 });
        DrawText("ROUTE", screenW - 240, 16, 16, (Color) { 140, 220, 255, 255 });
        DrawText("- TRANSIT CORRIDOR -", screenW - 240, 34, 10, (Color) { 180, 210, 230, 200 });
    }
    else {
        DrawRectangleLines(screenW - 250, 10, 240, 96, (Color) { 120, 120, 120, 180 });
        DrawText("WILDS", screenW - 240, 16, 16, (Color) { 200, 200, 200, 255 });
        DrawText("- NO MAN'S LAND -", screenW - 240, 34, 10, (Color) { 180, 180, 180, 200 });
    }
    DrawText(TextFormat("Legion: %d / %d", worldDefeated, NUM_TRAINERS),
        screenW - 240, 52, 12, WHITE);
    DrawText(TextFormat("Position %3d,%2d", px, py),
        screenW - 240, 70, 11, (Color) { 150, 200, 255, 200 });

    // Minimap
    int mmX = screenW - 250, mmY = 116;
    int mmW = 240, mmH = 120;
    DrawRectangle(mmX, mmY, mmW, mmH, (Color) { 10, 15, 30, 220 });
    DrawRectangleLines(mmX, mmY, mmW, mmH, (Color) { 80, 160, 220, 200 });
    float sx = (float)mmW / MAP_W, sy = (float)mmH / MAP_H;
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            int t = getTile(x, y);
            Color c;
            if (t == T_BLOCK) c = (Color){ 40, 45, 65, 255 };
            else if (t == T_PLASMA) c = (Color){ 100, 60, 200, 255 };
            else if (t == T_BUNKER || t == T_TERMINAL) c = (Color){ 90, 100, 130, 255 };
            else if (t == T_PAD) c = (Color){ 90, 140, 190, 255 };
            else if (t == T_GRASS) c = (Color){ 60, 120, 70, 255 };
            else if (t == T_RUINS) c = (Color){ 140, 70, 40, 255 };
            else c = (Color){ 40, 50, 70, 255 };
            DrawRectangle(mmX + (int)(x * sx), mmY + (int)(y * sy),
                (int)(sx)+1, (int)(sy)+1, c);
        }
    }
    // Region bounding boxes for orientation
    for (int i = 0; i < NUM_REGIONS; i++) {
        const Region* r = &regions[i];
        DrawRectangleLines(
            mmX + (int)(r->minX * sx),
            mmY + (int)(r->minY * sy),
            (int)((r->maxX - r->minX + 1) * sx),
            (int)((r->maxY - r->minY + 1) * sy),
            regions[i].tint);
    }
    // Trainers
    for (int i = 0; i < NUM_TRAINERS; i++) {
        if (trainers[i].defeated) continue;
        int dx = mmX + (int)(trainers[i].x * sx);
        int dy = mmY + (int)(trainers[i].y * sy);
        DrawRectangle(dx - 1, dy - 1, 3, 3, trainers[i].color);
    }
    // Player
    int bl = (int)(glowTimer * 4) % 2;
    int plx = mmX + (int)(px * sx);
    int ply = mmY + (int)(py * sy);
    DrawRectangle(plx - 2, ply - 2, 5, 5, bl ? WHITE : (Color) { 120, 240, 255, 255 });

    DrawText("[WASD/ARROWS] Move   [Z/ENTER] Talk   [TAB] Team   [F1] Debug   [ESC] Menu",
        10, SCREEN_H - 28, 14, (Color) { 150, 220, 255, 220 });

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