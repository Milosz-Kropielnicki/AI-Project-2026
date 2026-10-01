#include "world.h"
#include "battle.h"
#include "game.h"
#include "ui.h"
#include "transition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ============ REGIONS ============
// Five hubs. The chain alternates "wide" and "narrow" shapes so each region
// reads differently even though they all sit in the same 120x60 grid.
const Region regions[NUM_REGIONS] = {
    // name           subtitle                  tint                     enc  minX minY maxX maxY
    { "SECTOR ALPHA", "- CALIBRATION FIELD -", {255, 255, 255, 255}, 14,   3,  4, 26, 26 },
    { "SECTOR BETA",  "- INDUSTRIAL RUINS -",  {255, 220, 200, 255}, 18,  46,  4, 76, 24 },
    { "SECTOR GAMMA", "- APEX WASTELAND -",    {220, 200, 255, 255}, 20,  86,  4,116, 24 },
    { "SECTOR DELTA", "- FROZEN FOUNDRY -",    {200, 230, 255, 255}, 18,  46, 38, 76, 56 },
    { "SECTOR OMEGA", "- DEAD ZONE -",         {255, 180, 180, 255}, 24,  86, 38,116, 56 },
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

// Current region (kept for save/load compatibility)
static int currentZone = REGION_ALPHA;

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

// Intro sequence: which kind of encounter spotted us, and the phase.
//   spotKind 0 = none, 1 = trainer, 2 = wild pack
//   spotPhase 0 = none, 1 = bubble, 2 = wipe
static int spotKind = 0;
static int spottedTrainer = -1;
static float spotTimer = 0;             // counts up
static int spotPhase = 0;

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

// A route is a 2-tile-wide corridor. No more rubble frame — the surrounding
// wilderness stays solid T_BLOCK, so the corridor reads as a clean carved
// road through the map. A sparse scattering of ruins tiles is placed on the
// floor afterward for flavor and to keep encounters meaningful.
static void carveRouteRect(int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            setTile(x, y, T_GRASS);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if ((x - x0) == 0 || (y - y0) % 3 == 1)
                setTile(x, y, T_PAD);
}

static void carveRouteH(int x0, int x1, int y) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    carveRouteRect(x0, y, x1, y + 1);
}

static void carveRouteV(int y0, int y1, int x) {
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    carveRouteRect(x, y0, x + 1, y1);
}

// Sprinkles a small number of ruins tiles on the route floors. Called after
// every route is carved so the scattering spreads across the whole network
// without clustering. Uses its own seed so it's deterministic.
static void scatterRouteRuins(void) {
    srand(4242);
    // Try a lot of random positions and only place a ruins tile if the target
    // is currently part of a route (a T_GRASS or T_PAD outside any region).
    int placed = 0;
    int attempts = 0;
    while (placed < 40 && attempts < 4000) {
        attempts++;
        int x = rand() % MAP_W;
        int y = rand() % MAP_H;
        if (worldRegionAt(x, y) >= 0) continue;   // skip hubs
        int t = getTile(x, y);
        if (t != T_GRASS && t != T_PAD) continue;   // only route floor
        // Leave the pad stripe intact so the corridor still reads as a road
        if (t == T_PAD) continue;
        setTile(x, y, T_RUINS);
        placed++;
    }
}

// Fills a hub with its terrain features. Density and composition vary by
// region so the player immediately reads where they are. Only carves into the
// hub's interior (leaves a 1-tile margin of the region rect untouched so the
// route lead-ins can punch through cleanly).
static void decorateHub(int region) {
    const Region* r = &regions[region];
    int x0 = r->minX + 1, y0 = r->minY + 1;
    int x1 = r->maxX - 1, y1 = r->maxY - 1;
    if (x1 <= x0 || y1 <= y0) return;
    int w = x1 - x0, h = y1 - y0;

    // Every hub is a walkable floor first
    fillRect(x0, y0, x1, y1, T_GRID);

    srand(100 + region * 7919);

    switch (region) {
    case REGION_ALPHA: {
        for (int i = 0; i < 6; i++) {
            int cx = x0 + 1 + rand() % (w - 4);
            int cy = y0 + 1 + rand() % (h - 4);
            for (int y = cy; y < cy + 3; y++)
                for (int x = cx; x < cx + 4; x++)
                    if (getTile(x, y) == T_GRID) setTile(x, y, T_GRASS);
        }
        fillRect(x0 + 3, y0 + 3, x0 + 5, y0 + 4, T_BLOCK);
        fillRect(x1 - 5, y1 - 4, x1 - 3, y1 - 3, T_BLOCK);
        setTile(x0 + 8, (y0 + y1) / 2, T_TERMINAL);
        break;
    }
    case REGION_BETA: {
        for (int i = 0; i < 8; i++) {
            int cx = x0 + 1 + rand() % (w - 4);
            int cy = y0 + 1 + rand() % (h - 4);
            for (int y = cy; y < cy + 3; y++)
                for (int x = cx; x < cx + 4; x++)
                    if (getTile(x, y) == T_GRID) setTile(x, y, T_RUINS);
        }
        for (int i = 0; i < 4; i++)
            setTile(x0 + 2 + rand() % (w - 4), y0 + 2 + rand() % (h - 4), T_BUNKER);
        fillRect(x0 + w / 2, y0 + h / 2, x0 + w / 2 + 3, y0 + h / 2 + 2, T_PLASMA);
        setTile((x0 + x1) / 2, (y0 + y1) / 2 + 4, T_TERMINAL);
        break;
    }
    case REGION_GAMMA: {
        for (int i = 0; i < 7; i++) {
            int cx = x0 + 1 + rand() % (w - 3);
            int cy = y0 + 1 + rand() % (h - 3);
            for (int y = cy; y < cy + 2; y++)
                for (int x = cx; x < cx + 3; x++)
                    if (getTile(x, y) == T_GRID) setTile(x, y, T_RUINS);
        }
        for (int i = 0; i < 4; i++) {
            int cx = x0 + 1 + rand() % (w - 4);
            int cy = y0 + 1 + rand() % (h - 1);
            int len = 2 + rand() % 3;
            for (int j = 0; j < len; j++)
                if (getTile(cx + j, cy) == T_GRID) setTile(cx + j, cy, T_BLOCK);
        }
        fillRect(x0 + 2, y0 + 2, x0 + 5, y0 + 4, T_PLASMA);
        fillRect(x1 - 5, y1 - 4, x1 - 2, y1 - 2, T_PLASMA);
        setTile(x0 + 8, y0 + 2, T_TERMINAL);
        break;
    }
    case REGION_DELTA: {
        fillRect(x0 + 2, y0 + 3, x0 + 3, y1 - 3, T_BLOCK);
        fillRect(x1 - 3, y0 + 3, x1 - 2, y1 - 3, T_BLOCK);
        for (int i = 0; i < 5; i++)
            setTile(x0 + 3 + rand() % (w - 6), y0 + 3 + rand() % (h - 6), T_BUNKER);
        for (int i = 0; i < 4; i++) {
            int cx = x0 + 1 + rand() % (w - 5);
            int cy = y0 + 1 + rand() % (h - 4);
            for (int y = cy; y < cy + 3; y++)
                for (int x = cx; x < cx + 5; x++)
                    if (getTile(x, y) == T_GRID) setTile(x, y, T_GRASS);
        }
        setTile((x0 + x1) / 2, y1 - 2, T_TERMINAL);
        break;
    }
    default: {   // REGION_OMEGA
        int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        fillRect(cx - 3, cy - 1, cx + 3, cy + 1, T_PLASMA);
        for (int i = 0; i < 11; i++) {
            int bx = x0 + 1 + rand() % (w - 3);
            int by = y0 + 1 + rand() % (h - 3);
            for (int y = by; y < by + 2; y++)
                for (int x = bx; x < bx + 3; x++)
                    if (getTile(x, y) == T_GRID) setTile(x, y, T_RUINS);
        }
        for (int i = 0; i < 6; i++)
            setTile(x0 + 2 + rand() % (w - 4), y0 + 2 + rand() % (h - 4), T_BLOCK);
        setTile(x1 - 3, cy, T_TERMINAL);
        break;
    }
    }
}

// ============ MAP GEN ============
static void genWorld(void) {
    // Base fill: solid impassable wilderness everywhere
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            map[y][x] = T_BLOCK;

    // Decorate every hub first. This leaves the hub floor walkable but does
    // NOT carve through the hub's 1-tile border margin.
    for (int i = 0; i < NUM_REGIONS; i++) decorateHub(i);

    // Now carve the routes. Each route starts at the OUTER edge of one hub
    // and ends at the OUTER edge of the next, punching through the 1-tile
    // border on both sides so the corridor is truly continuous.
    //
    // Hub borders (region rects):
    //   ALPHA ( 3,  4)-( 26, 26)
    //   BETA  (46,  4)-( 76, 24)
    //   GAMMA (86,  4)-(116, 24)
    //   DELTA (46, 38)-( 76, 56)
    //   OMEGA (86, 38)-(116, 56)
    //
    // Layout:
    //     ALPHA ──R1── BETA ──R2── GAMMA
    //       │                        │
    //       R3 (L)                   R6
    //       │                        │
    //       └──east──┐               │
    //                ▼               │
    //               DELTA ──R4──────▶│
    //                │               │
    //                R5              │
    //                │               │
    //                ▼               ▼
    //               OMEGA ◀──────────┘

    // ---- ROUTE 1: ALPHA east wall -> BETA west wall ----
    carveRouteH(26, 46, 12);

    // ---- ROUTE 2: BETA east wall -> GAMMA west wall ----
    carveRouteH(76, 86, 12);

    // ---- ROUTE 3: ALPHA south wall -> DELTA west wall (L-shaped) ----
    carveRouteV(26, 48, 12);      // south leg
    carveRouteH(12, 46, 48);      // east leg to Delta's west wall

    // ---- ROUTE 4: DELTA east wall -> GAMMA south wall (L-shaped) ----
    carveRouteH(76, 100, 48);     // east leg
    carveRouteV(24, 48, 100);     // north leg to Gamma's south wall

    // ---- ROUTE 5: DELTA west wall -> OMEGA west wall ----
    carveRouteH(46, 86, 48);

    // ---- ROUTE 6: GAMMA south wall -> OMEGA north wall ----
    carveRouteV(24, 38, 100);

    // ---- LEAD-IN STUBS: connect each hub's interior to the routes ----
    // Alpha
    carveRouteH(20, 26, 12);
    carveRouteV(20, 26, 12);
    // Beta
    carveRouteH(46, 54, 12);
    carveRouteH(70, 76, 12);
    // Gamma
    carveRouteH(86, 94, 12);
    carveRouteV(24, 32, 100);
    // Delta
    carveRouteH(46, 54, 48);
    carveRouteH(70, 76, 48);
    // Omega
    carveRouteH(86, 94, 48);
    carveRouteV(38, 46, 100);

    // ---- Sparsely scatter ruins onto the route floors for encounters ----
    scatterRouteRuins();
}

static int isSolid(int x, int y) {
    int t = getTile(x, y);
    return (t == T_BLOCK || t == T_PLASMA || t == T_BUNKER || t == T_TERMINAL);
}

static int isEncounterTile(int x, int y) {
    int t = getTile(x, y);
    return (t == T_GRASS || t == T_RUINS);
}

static int isRouteTile(int x, int y) {
    if (worldRegionAt(x, y) >= 0) return 0;
    int t = getTile(x, y);
    return (t == T_GRASS || t == T_PAD || t == T_RUINS);
}

// ============ TRAINERS ============
// The final field of each initializer is sightRange: how many tiles ahead the
// trainer watches. Stepping into a watched tile starts the intro sequence.
static void initTrainers(void) {
    // --- Alpha hub ---
    trainers[0] = (Trainer){
        "PILOT RHEA", FAC_IRON_LEGION, 14, 20, 0, { 255, 120, 200, 255 },
        "Hey rookie! Let's see what you've got!",
        "You're stronger than you look...",
        "Head east or south when you're ready.",
        0, 0, { ARCH_SKIRMISHER }, { 1 }, 1, 0, 1
    };
    trainers[1] = (Trainer){
        "SCOUT DANE", FAC_IRON_LEGION, 20, 8, 0, { 255, 200, 100, 255 },
        "Fast mechs win wars, rookie!",
        "Speed wasn't enough...",
        "Route 1 is at the top of the map.",
        0, 0, { ARCH_PROWLER, ARCH_SKIRMISHER }, { 1, 1 }, 2, 0, 2
    };
    trainers[2] = (Trainer){
        "MECHANIC VOSS", FAC_CHROME_SYNDICATE, 8, 22, 0, { 120, 220, 255, 255 },
        "Nice frame. Let's see if it holds.",
        "Hmph. Not bad at all.",
        "Delta is south, past the west route.",
        1, 0, { ARCH_BRAWLER }, { 2 }, 1, 0, 1
    };

    // --- Route 1 (Alpha -> Beta) ---
    trainers[3] = (Trainer){
        "ROUTE GUARD", FAC_IRON_LEGION, 36, 12, 0, { 255, 160, 100, 255 },
        "No one passes this road without a fight.",
        "Fine... you've earned the crossing.",
        "Beta is straight ahead.",
        1, 0, { ARCH_BRAWLER }, { 2 }, 1, 0, 2
    };

    // --- Beta hub ---
    trainers[4] = (Trainer){
        "COMMANDER VOLK", FAC_IRON_LEGION, 60, 14, 0, { 255, 180, 60, 255 },
        "You dare challenge the Iron Legion?",
        "IMPOSSIBLE! My mechs... destroyed!",
        "Gamma lies east. Watch the ruins.",
        2, 0, { ARCH_BRAWLER, ARCH_BERSERKER, ARCH_SKIRMISHER }, { 3, 3, 3 }, 3, 0, 1
    };
    trainers[5] = (Trainer){
        "ENGINEER KESS", FAC_CHROME_SYNDICATE, 70, 20, 0, { 120, 220, 160, 255 },
        "My machines never break. Yours will.",
        "Fascinating... your tactics are... effective.",
        "The wasteland is further east still.",
        2, 0, { ARCH_JAMMER, ARCH_SNIPER, ARCH_BERSERKER, ARCH_BOMBARD }, { 4, 4, 4, 4 }, 4, 0, 1
    };
    trainers[6] = (Trainer){
        "FOREMAN GRELL", FAC_IRON_LEGION, 54, 20, 0, { 200, 180, 100, 255 },
        "This rubble is ours. Move along.",
        "You move well for a freelancer.",
        "Two entrances, don't get lost.",
        1, 0, { ARCH_ORDNANCE }, { 3 }, 1, 0, 1
    };

    // --- Route 2 (Beta -> Gamma) ---
    trainers[7] = (Trainer){
        "SIGNAL RELAY", FAC_CHROME_SYNDICATE, 81, 12, 0, { 100, 200, 255, 255 },
        "Transmission intercepted. Terminating.",
        "Transmission... lost.",
        "Gamma's just past me.",
        2, 0, { ARCH_PROWLER, ARCH_SKIRMISHER }, { 4, 4 }, 2, 0, 2
    };

    // --- Gamma hub ---
    trainers[8] = (Trainer){
        "GHOST ECHO", FAC_CHROME_SYNDICATE, 96, 12, 0, { 200, 100, 255, 255 },
        "You cannot hit what you cannot see.",
        "Even my stealth... failed.",
        "Delta is south. Omega is further.",
        2, 0, { ARCH_SKIRMISHER, ARCH_BERSERKER, ARCH_BOMBARD, ARCH_JAMMER }, { 5, 6, 6, 6 }, 4, 0, 2
    };
    trainers[9] = (Trainer){
        "IRON SENTINEL", FAC_IRON_LEGION, 110, 20, 0, { 220, 220, 100, 255 },
        "Perimeter breach. Terminating.",
        "Perimeter... lost.",
        "The long loop is down the east side.",
        2, 0, { ARCH_ORDNANCE, ARCH_BRAWLER }, { 5, 5 }, 2, 0, 2
    };

    // --- Route 4 (Delta -> Gamma, east leg) ---
    trainers[10] = (Trainer){
        "ROADBLOCK UNIT", FAC_CHROME_SYNDICATE, 88, 48, 0, { 255, 100, 100, 255 },
        "HALT. The wasteland is off-limits.",
        "AUTHORIZATION... REVOKED. Proceed.",
        "Gamma's just north.",
        2, 0, { ARCH_GUARDIAN, ARCH_BOMBARD }, { 5, 5 }, 2, 0, 2
    };

    // --- Route 3 (Alpha -> Delta) ---
    trainers[11] = (Trainer){
        "PATROL LEAD", FAC_IRON_LEGION, 12, 32, 0, { 200, 160, 100, 255 },
        "Halt. State your business.",
        "Business concluded. Move on.",
        "Delta's to the east from here.",
        1, 0, { ARCH_SKIRMISHER, ARCH_BRAWLER }, { 4, 4 }, 2, 0, 2
    };

    // --- Delta hub ---
    trainers[12] = (Trainer){
        "FORGE MASTER", FAC_IRON_LEGION, 60, 48, 0, { 220, 240, 255, 255 },
        "This foundry forges war. Care to test?",
        "The forge... dims.",
        "Two ways out: east and west.",
        2, 0, { ARCH_ORDNANCE, ARCH_BRAWLER, ARCH_BOMBARD }, { 6, 6, 6 }, 3, 0, 1
    };
    trainers[13] = (Trainer){
        "ICE RUNNER", FAC_CHROME_SYNDICATE, 70, 42, 0, { 180, 220, 255, 255 },
        "Cold steel cuts deepest.",
        "Frozen solid...",
        "Omega lies east.",
        2, 0, { ARCH_PROWLER, ARCH_SKIRMISHER }, { 5, 5 }, 2, 0, 1
    };

    // --- Route 5 (Delta -> Omega) ---
    trainers[14] = (Trainer){
        "SALVAGE TEAM", FAC_CHROME_SYNDICATE, 66, 48, 0, { 180, 240, 120, 255 },
        "That chassis is Legion issue. Drop it.",
        "Legion's really slipping...",
        "Omega is due east.",
        2, 0, { ARCH_JAMMER, ARCH_ORDNANCE }, { 6, 6 }, 2, 0, 1
    };

    // --- Omega hub ---
    trainers[15] = (Trainer){
        "WARDEN KRUX", FAC_IRON_LEGION, 108, 48, 0, { 255, 60, 60, 255 },
        "Only the strongest reach me. Prepare to be crushed.",
        "...You ARE the apex. Well fought.",
        "The wasteland is yours. Go.",
        3, 0, { ARCH_BOMBARD, ARCH_GUARDIAN, ARCH_ORDNANCE, ARCH_BRAWLER }, { 7, 7, 9, 7 }, 4, 0, 2
    };

    // --- Omega, far corner: the boss, the machine itself, flanked by escorts.
    // Firmware 3.0 with Recursive Targeting and Dead-Man Protocol.
    trainers[16] = (Trainer){
        "FACTORY OVERSEER", FAC_BLACK_BOX, 112, 53, 0, { 200, 60, 255, 255 },
        "INTRUDER DETECTED. EXECUTING RECURSIVE TARGETING.",
        "CORE FAILURE... DEAD-MAN PROTOCOL... COMPLETE.",
        "...the Overseer's chassis sits silent.",
        3, 0, { ARCH_SNIPER, ARCH_OVERSEER, ARCH_BOMBARD, ARCH_JAMMER }, { 10, 12, 10, 9 }, 4, 0, 2
    };
}

void worldInit(void) {
    genWorld();
    initTrainers();
    currentZone = REGION_ALPHA;
    px = 10; py = 15; facing = 3;
    pxF = (float)(px * TILE_SIZE);
    pyF = (float)(py * TILE_SIZE);
    justEnteredZone = 1;
    spotKind = 0;
    spottedTrainer = -1;
    spotTimer = 0;
    spotPhase = 0;
    transitionReset();
}

// ============ STARTERS ============
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

    px = 10; py = 15; facing = 3;
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
    partsAddFromMech(&evolved);
    evolved.fw = m->fw;
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

    if (totalDefeated >= 5 && missing[0] >= 0 && teamSize < MAX_TEAM) {
        Mech m = mechCreateStock(starters[missing[0]].stages[0], 0);
        snprintf(m.name, sizeof(m.name), "%s-01", starters[missing[0]].name);
        int idx = missing[0];
        if (idx == 0)      firmwareInstall(&m.fw, 0, CHIP_PREDICTIVE_TARGETING);
        else if (idx == 1) firmwareInstall(&m.fw, 0, CHIP_HARDENED_KERNEL);
        else               firmwareInstall(&m.fw, 0, CHIP_COUNTER_INTRUSION);
        mechRepair(&m);
        if (rosterAdd(&m)) {
            partsAddFromMech(&m);
            obtainedStarters |= (1 << missing[0]);
            showMessage(TextFormat(">> FIELD RECOVERY: %s added to your team!", starters[missing[0]].name), 4.0f);
        }
    }
    if (totalDefeated >= 10 && missingCount >= 2 && missing[1] >= 0 && teamSize < MAX_TEAM) {
        Mech m = mechCreateStock(starters[missing[1]].stages[0], 0);
        snprintf(m.name, sizeof(m.name), "%s-01", starters[missing[1]].name);
        int idx = missing[1];
        if (idx == 0)      firmwareInstall(&m.fw, 0, CHIP_PREDICTIVE_TARGETING);
        else if (idx == 1) firmwareInstall(&m.fw, 0, CHIP_HARDENED_KERNEL);
        else               firmwareInstall(&m.fw, 0, CHIP_COUNTER_INTRUSION);
        mechRepair(&m);
        if (rosterAdd(&m)) {
            partsAddFromMech(&m);
            obtainedStarters |= (1 << missing[1]);
            showMessage(TextFormat(">> FIELD RECOVERY: %s added to your team!", starters[missing[1]].name), 4.0f);
        }
    }
}

int worldCurrentZone(void) { return worldRegionAt(px, py) >= 0 ? worldRegionAt(px, py) : currentZone; }
const char* worldCurrentZoneName(void) { return regions[worldCurrentZone()].name; }
const char* worldCurrentZoneSubtitle(void) { return regions[worldCurrentZone()].subtitle; }

void worldGetPlayer(int* zone, int* x, int* y) {
    *zone = worldCurrentZone();
    *x = px;
    *y = py;
}

void worldSetPlayer(int zone, int x, int y) {
    if (x < 1) x = 1;
    if (y < 1) y = 1;
    if (x >= MAP_W - 1) x = MAP_W - 2;
    if (y >= MAP_H - 1) y = MAP_H - 2;
    currentZone = zone;
    px = x; py = y;
    pxF = (float)(px * TILE_SIZE);
    pyF = (float)(py * TILE_SIZE);
    moving = 0;
    justEnteredZone = 1;
}

int worldFindTrainer(const char* name) {
    for (int i = 0; i < NUM_TRAINERS; i++) if (strcmp(trainers[i].name, name) == 0) return i;
    return -1;
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

// Wild intro lines: a small pool of flavour text for the bubble. The line is
// picked when the pack is spotted so it stays consistent for the whole intro.
static const char* wildIntroLines[] = {
    "HOSTILE PACK detected. Engaging.",
    "Feral machines close in from the ruins!",
    "Rogue signatures converging. No pilots aboard.",
    "A scrapyard pack has our scent. Brace.",
    "Signal contact: unmanned units, weapons hot.",
    "Ambush! No time to pick the ground.",
};
#define NUM_WILD_LINES ((int)(sizeof(wildIntroLines) / sizeof(wildIntroLines[0])))

// ============ UPDATE ============
void worldUpdate(float dt, GameState* state) {
    if (IsKeyPressed(KEY_TAB)) { *state = STATE_TEAM; return; }
    if (IsKeyPressed(KEY_ESCAPE)) { *state = STATE_MENU; return; }
    if (IsKeyPressed(KEY_M)) { *state = STATE_MAP; return; }

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

    // ---- Intro sequence: bubble, then wipe, then battle ----
    if (spotPhase != 0) {
        spotTimer += dt;
        float introTime = spotKind == 1 ? TRAINER_INTRO_TIME : WILD_INTRO_TIME;
        if (spotPhase == 1) {
            // The comms bubble is up; allow the player to skip ahead.
            if (spotTimer >= introTime || confirmPressed()) {
                spotPhase = 2;
                spotTimer = 0;
                messageTimer = 0;
                consumeInput();
                // Hand the close off to the shared transition module so the
                // battle side can pick up where the world leaves off.
                if (spotKind == 1) {
                    transitionStartClose(trainers[spottedTrainer].name, trainers[spottedTrainer].color);
                }
                else {
                    // Wild: use the pack's threat colour (a hot red-orange).
                    transitionStartClose("HOSTILE CONTACT", (Color) { 255, 90, 70, 255 });
                }
            }
            return;
        }
        // spotPhase == 2: the shared transition is running the close.
        transitionUpdate(dt);
        if (transition.phase == 2 && transition.t >= TRANSITION_CLOSE_TIME + TRANSITION_HOLD_TIME) {
            int did = 0;
            if (spotKind == 1) {
                int t = spottedTrainer;
                if (triggerTrainerEncounter(t)) did = 1;
            }
            else if (spotKind == 2) {
                battleStartWild();
                did = 1;
            }
            spotKind = 0;
            spottedTrainer = -1;
            spotPhase = 0;
            spotTimer = 0;
            if (did) {
                // Switch the wipe to its opening half; the battle screen
                // draws it, so the reveal happens over the fight.
                transition.phase = 3;
                transition.t = 0;
                transition.cover = 1.0f;
                *state = STATE_BATTLE;
                return;
            }
            transitionReset();
        }
        return;
    }

    // ---- Trainer line of sight ----
    // A trainer watches the tile(s) it faces. Stepping into one starts the
    // intro: the trainer speaks, then the wipe plays.
    if (!moving) {
        for (int i = 0; i < NUM_TRAINERS; i++) {
            Trainer* t = &trainers[i];
            if (t->defeated || t->sightRange <= 0) continue;
            int sx = t->x, sy = t->y;
            int dx = (t->facing == 3) - (t->facing == 2);
            int dy = (t->facing == 0) - (t->facing == 1);
            for (int d = 0; d < t->sightRange; d++) {
                sx += dx;
                sy += dy;
                if (sx == px && sy == py) {
                    spotKind = 1;
                    spottedTrainer = i;
                    spotPhase = 1;
                    spotTimer = 0;
                    showMessage(TextFormat("\"%s\"", t->introLine), TRAINER_INTRO_TIME);
                    return;
                }
            }
        }
    }

    // ---- Wild encounter: roll the region's encounter rate when we arrive on
    // an encounter tile (skipping the tile we started the world on). A hit
    // starts the same intro as a trainer: bubble, wipe, battle.
    if (arrived && !justEnteredZone && isEncounterTile(px, py)) {
        int region = worldRegionAt(px, py);
        int rate;
        if (region >= 0) rate = regions[region].baseEncounter;
        else             rate = 12;
        if ((rand() % 100) < rate) {
            spotKind = 2;
            spotPhase = 1;
            spotTimer = 0;
            showMessage(wildIntroLines[rand() % NUM_WILD_LINES], WILD_INTRO_TIME);
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

        if (getTile(fx, fy) == T_TERMINAL) {
            *state = STATE_TERMINAL;
            return;
        }
    }

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

    // Trainers are solid; you can't walk onto them
    for (int i = 0; i < NUM_TRAINERS; i++) {
        if (trainers[i].x == nx && trainers[i].y == ny) return;
    }

    if (isSolid(nx, ny)) {
        if (fresh && getTile(nx, ny) == T_TERMINAL) *state = STATE_TERMINAL;
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

        // Sight cone: show which tiles the trainer is watching, so the player
        // can read the trigger before walking into it.
        if (t->sightRange > 0) {
            int dx = (t->facing == 3) - (t->facing == 2);
            int dy = (t->facing == 0) - (t->facing == 1);
            float blink = 0.5f + 0.5f * sinf(glowTimer * 3);
            for (int d = 0; d < t->sightRange; d++) {
                int tx = t->x + dx * (d + 1);
                int ty = t->y + dy * (d + 1);
                Rectangle r = { (float)(tx * TILE_SIZE + 4), (float)(ty * TILE_SIZE + 4),
                                (float)(TILE_SIZE - 8), (float)(TILE_SIZE - 8) };
                DrawRectangleLinesEx(r, 1, (Color) { primary.r, primary.g, primary.b, (unsigned char)(60 + 60 * blink) });
                // Center pip in the watched tile
                DrawRectangle((int)(r.x + r.width / 2 - 1), (int)(r.y + r.height / 2 - 1), 2, 2,
                    (Color) {
                    primary.r, primary.g, primary.b, (unsigned char)(120 + 80 * blink)
                });
            }
        }
    }
}

static Color regionTint(int tx, int ty, Color c) {
    int region = worldRegionAt(tx, ty);
    if (region < 0) {
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

void worldDrawMinimap(int mx, int my, int mw, int mh) {
    DrawRectangle(mx, my, mw, mh, (Color) { 10, 15, 30, 255 });
    DrawRectangleLines(mx, my, mw, mh, (Color) { 80, 160, 220, 200 });
    float sx = (float)mw / MAP_W, sy = (float)mh / MAP_H;
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
            DrawRectangle(mx + (int)(x * sx), my + (int)(y * sy),
                (int)(sx)+1, (int)(sy)+1, c);
        }
    }
    for (int i = 0; i < NUM_REGIONS; i++) {
        const Region* r = &regions[i];
        DrawRectangleLines(
            mx + (int)(r->minX * sx),
            my + (int)(r->minY * sy),
            (int)((r->maxX - r->minX + 1) * sx),
            (int)((r->maxY - r->minY + 1) * sy),
            regions[i].tint);
    }
    for (int i = 0; i < NUM_TRAINERS; i++) {
        if (trainers[i].defeated) continue;
        int dx = mx + (int)(trainers[i].x * sx);
        int dy = my + (int)(trainers[i].y * sy);
        DrawRectangle(dx - 1, dy - 1, 3, 3, trainers[i].color);
    }
    int bl = (int)(glowTimer * 4) % 2;
    int plx = mx + (int)(px * sx);
    int ply = my + (int)(py * sy);
    DrawRectangle(plx - 2, ply - 2, 5, 5, bl ? WHITE : (Color) { 120, 240, 255, 255 });
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
    DrawText(TextFormat("%d CR", credits), 250, 134, 12, (Color) { 255, 220, 100, 255 });

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
    DrawText(TextFormat("Encounters: %d / %d", worldDefeated, NUM_TRAINERS),
        screenW - 240, 52, 12, WHITE);
    DrawText(TextFormat("Position %3d,%2d", px, py),
        screenW - 240, 70, 11, (Color) { 150, 200, 255, 200 });

    DrawRectangle(screenW - 250, 116, 240, 26, (Color) { 15, 25, 45, 200 });
    DrawRectangleLines(screenW - 250, 116, 240, 26, (Color) { 100, 200, 255, 180 });
    DrawText("[M] OPEN MAP", screenW - 240, 122, 12, (Color) { 140, 220, 255, 255 });

    int jy = SCREEN_H - 50;
    for (int j = NUM_JOBS - 1; j >= 0; j--) {
        if (jobState[j] != JS_ACTIVE && jobState[j] != JS_READY) continue;
        int ready = jobState[j] == JS_READY;
        DrawText(TextFormat("%s %s: %s", ready ? "[READY]" : "[JOB]", jobDefs[j].title,
            ready ? "claim at any terminal" : jobObjective(j)), 12, jy, 12,
            ready ? (Color) { 120, 255, 160, 255 } : (Color) { 255, 220, 140, 255 });
        jy -= 16;
    }

    DrawText("[WASD/ARROWS] Move   [Z/ENTER] Talk/Terminal   [TAB] Team   [M] Map   [F1] Debug   [ESC] Menu",
        10, SCREEN_H - 28, 13, (Color) { 150, 220, 255, 220 });

    if (messageTimer > 0) {
        DrawRectangle(40, SCREEN_H - 130, screenW - 80, 90, (Color) { 15, 25, 45, 240 });
        DrawRectangleLines(40, SCREEN_H - 130, screenW - 80, 90, (Color) { 80, 220, 255, 220 });
        DrawText(">> COMMS", 55, SCREEN_H - 122, 13, (Color) { 100, 240, 255, 255 });
        DrawText(message, 55, SCREEN_H - 100, 20, (Color) { 200, 240, 255, 255 });
        // While an encounter bubble is up, hint that the player can skip ahead
        if (spotPhase == 1)
            DrawText("[Z/CLICK] to engage", screenW - 260, SCREEN_H - 60, 14, (Color) { 255, 220, 100, 255 });
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
        if (trainers[i].x < startX - 2 || trainers[i].x > endX + 2 ||
            trainers[i].y < startY - 2 || trainers[i].y > endY + 2) continue;
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

    // The shared wipe draws on top of the world, spanning the whole canvas
    // (raw canvas coordinates, not the fixed-layout camera).
    if (transition.active && transition.phase == 2) {
        transitionDraw();
    }
}