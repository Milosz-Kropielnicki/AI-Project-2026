#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ============ CONFIG ============
// SCREEN_W is the width of the fixed-layout screens (battle, team, menus).
// The actual canvas is SCREEN_H tall and screenW wide, depending on aspect ratio.
#define SCREEN_W 800
#define SCREEN_H 600
#define TILE_SIZE 40
#define MOVE_TIME 0.14f   // seconds to walk one tile
#define SETTINGS_FILE "settings.cfg"
#define MAP_W 40
#define MAP_H 30

#define MAX_ENERGY 3
#define ENERGY_PER_TURN 3
#define MAX_TEAM 6

enum { FX_NONE = 0, FX_PULSE, FX_BEAM, FX_SCAN, FX_NOVA, FX_ARC, FX_BLADE, FX_JAM, FX_MISSILE };
enum { T_GRID, T_RUINS, T_BLOCK, T_PLASMA, T_PAD, T_BUNKER, T_TERMINAL };
enum { STATE_OVERWORLD, STATE_BATTLE, STATE_LEVELUP, STATE_TEAM, STATE_MENU, STATE_SETTINGS };

enum { DLG_NONE = 0, DLG_INTRO, DLG_DEFEAT };

// ============ MECH SPECIES ============
// Each species has a distinct look (drawn procedurally), color, and stat baseline.
#define NUM_SPECIES 6
typedef struct {
    char name[32];
    char desc[64];
    Color body, accent, glow;
    // stat multipliers applied to a base level
    float hpMult, atkMult, defMult, spdMult;
    int rarity;  // 1 = common, 2 = uncommon, 3 = rare
} Species;

static Species species[NUM_SPECIES] = {
    // 0: Basic starter-like mech (blue)
    { "NOVA", "A balanced frontline mech.",
      {60,120,200,255}, {100,200,255,255}, {80,220,255,255},
      1.00f, 1.00f, 1.00f, 1.00f, 1 },
      // 1: Tanky armored mech (green)
      { "BULWARK", "Heavy armor, slow but sturdy.",
        {60,140,90,255}, {120,220,140,255}, {140,255,160,255},
        1.30f, 0.85f, 1.40f, 0.75f, 1 },
        // 2: Fast scout mech (yellow)
        { "WISP", "Extremely fast, fragile frame.",
          {220,200,60,255}, {255,240,120,255}, {255,240,140,255},
          0.75f, 0.90f, 0.70f, 1.50f, 2 },
          // 3: Aggressive attacker (red)
          { "RAZOR", "Blade-armed, hits hard and fast.",
            {180,60,60,255}, {240,110,90,255}, {255,140,120,255},
            0.90f, 1.35f, 0.85f, 1.15f, 2 },
            // 4: Ranged artillery (purple)
            { "HAVOC", "Missile platform, high offense.",
              {110,60,160,255}, {190,120,240,255}, {210,150,255,255},
              0.95f, 1.25f, 0.90f, 1.00f, 3 },
              // 5: Legendary apex mech (dark w/ red)
              { "OBLIVION", "Apex predator. Devastating power.",
                {50,25,50,255}, {220,50,80,255}, {255,80,80,255},
                1.15f, 1.45f, 1.10f, 1.20f, 3 }
};

// ============ MECH INSTANCE ============
typedef struct {
    char name[32];      // custom name (e.g. "NOVA-7")
    int speciesIdx;     // index into species[]
    int maxHP, hp, atk, def, spd;
    int level, exp, expToNext;
} Mech;

// ============ MOVE SETS ============
typedef struct {
    int moveDamage[4], moveCost[4], moveAccuracy[4], moveEffect[4];
    Color moveColor[4];
    char moveNames[4][32];
    int pp[4], ppMax[4], numMoves;
} MoveSet;

static MoveSet playerMoveSet; // player's active mech's current moveset (copied from a template per battle)

// ============ BATTLE ============
typedef struct {
    Mech m;
    MoveSet moves;
} Battler;

// ============ EVOLUTION ============
typedef struct {
    char name[32];
    int requiredLevel;   // 0 = no evolution
    int toSpeciesIdx;    // -1 = none
    Color primary, accent, glowColor;
    int hpBonus, atkBonus, defBonus, spdBonus;
    const char* evolveMsg;
} EvolutionStage;

// For simplicity: the player's "partner" mech (slot 0) evolves through stages.
// Other caught mechs don't evolve in this version.
#define NUM_STAGES 3
static EvolutionStage stages[NUM_STAGES] = {
    { "NOVA-7",     0, -1, {40,100,180,255}, {90,190,240,255}, {80,220,255,255},
      0, 0, 0, 0, "NOVA-7 online." },
    { "NOVA-X",     3,  3, {50,90,170,255}, {240,90,60,255}, {255,150,80,255},
      8, 3, 2, 1, "POWER SURGE! NOVA-7 evolved into NOVA-X!" },
    { "NOVA-PRIME", 8,  5, {60,30,60,255}, {255,60,90,255}, {255,80,80,255},
      14, 5, 4, 2, "CRITICAL UPGRADE! NOVA-X evolved into NOVA-PRIME!" }
};

// ============ TRAINER ============
typedef struct {
    char name[32];
    char team[32];
    int x, y;
    int facing;
    Color color;
    const char* introLine;
    const char* defeatLine;
    const char* postLine;
    int tier;
    int defeated;
    // team of mech species indices
    int teamSpecies[MAX_TEAM];
    int teamLevels[MAX_TEAM];
    int numMechs;
    int numDefeated;
} Trainer;

#define NUM_TRAINERS 3
static Trainer trainers[NUM_TRAINERS];

// ============ EFFECTS ============
#define MAX_PARTICLES 512
typedef struct { Vector2 pos, vel; float life, maxLife, size; Color color; int active; } Particle;
static Particle particles[MAX_PARTICLES];

typedef struct {
    int active, kind, owner;
    float t, duration;
    Vector2 from, to;
    Color color;
    int damageShown, damage, hit;
} Effect;
#define MAX_EFFECTS 8
static Effect effects[MAX_EFFECTS];

typedef struct { int active; Vector2 pos; float life; int damage, healing; Color color; } DamageNum;
#define MAX_DAMAGE_NUMS 16
static DamageNum damageNums[MAX_DAMAGE_NUMS];

static float shakeAmount = 0, shakeTimer = 0;
static Vector2 playerMechPos, enemyMechPos;

// ============ WORLD ============
static unsigned char map[MAP_H][MAP_W];
static int px, py;
static float pxF, pyF;
static int facing;
static int encounterChance = 14;

static Battler playerBattler, enemyBattler;
static char battleLog[256];
static int battleMenuSel = 0, battleState = 0;
static float battleTimer = 0;
static int playerEnergy = MAX_ENERGY, turnNumber = 1, playerEndedTurn = 0;
static int pendingPlayerMove = -1;
static int pendingEnemyActions[4], pendingEnemyMoveCount = 0, pendingEnemyIdx = 0;

static char message[256] = { 0 };
static float messageTimer = 0;

// ============ TEAM ============
static Mech team[MAX_TEAM];
static int teamSize = 0;
static int activeTeamSlot = 0;   // which mech is currently deployed
static int playerStage = 0;      // for slot 0 only

// ============ GLOBAL STATE ============
static int lastXpGained = 0;
static int leveledThisBattle = 0, evolvedThisBattle = 0;
static int caughtThisBattle = 0;
static float levelUpTimer = 0;
static int levelUpSubState = 0;
static float glowTimer = 0;

static int inTrainerBattle = 0;
static int activeTrainerIdx = -1;

static int dialoguePhase = DLG_NONE;
static char dialogueText[256] = { 0 };

// ============ OVERWORLD MOVEMENT ============
static int moving = 0;           // walking between two tiles
static float moveT = 0;          // 0..1 progress of the current step
static float moveFromX, moveFromY;
static int lastDir = -1;         // most recently pressed direction (facing convention)

// ============ MENUS ============
static int menuSel = 0, settingsSel = 0, teamSel = 0;
static int gameStarted = 0;
static int quitRequested = 0;

// ============ DISPLAY ============
// Everything is drawn to an offscreen canvas SCREEN_H pixels tall whose width follows
// the aspect ratio, then scaled to the window. Fixed-layout screens are centered in it.
typedef struct { const char* label; int aw, ah, winW, winH; } AspectPreset;
#define NUM_ASPECTS 4
static AspectPreset aspects[NUM_ASPECTS] = {
    { "4:3",    4,  3,  800, 600 },
    { "16:9",  16,  9, 1280, 720 },
    { "16:10", 16, 10, 1280, 800 },
    { "21:9",  21,  9, 1680, 720 },
};
static int settingFullscreen = 0;
static int settingAspect = 0;
static int screenW = SCREEN_W;
static RenderTexture2D canvas;
static float canvasScale = 1.0f;
static Vector2 canvasOffset;

// Horizontal offset that centers a SCREEN_W-wide layout on the canvas
static float layoutX(void) { return (float)((screenW - SCREEN_W) / 2); }

static Camera2D layoutCamera(void) {
    Camera2D c = { 0 };
    c.offset = (Vector2){ layoutX(), 0 };
    c.zoom = 1.0f;
    return c;
}

static void updateCanvasTransform(void) {
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    canvasScale = fminf(sw / screenW, sh / SCREEN_H);
    canvasOffset = (Vector2){ (sw - screenW * canvasScale) / 2, (sh - SCREEN_H * canvasScale) / 2 };
}

static void applyDisplaySettings(void) {
    int mon = GetCurrentMonitor();
    int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    Vector2 mpos = GetMonitorPosition(mon);
    int borderless = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    int newW;
    if (settingFullscreen) {
        if (!borderless) ToggleBorderlessWindowed();
        newW = SCREEN_H * mw / mh;
    }
    else {
        if (borderless) ToggleBorderlessWindowed();
        AspectPreset* a = &aspects[settingAspect];
        int w = a->winW, h = a->winH;
        // Shrink the window if it would not fit on this monitor
        if (w > mw * 9 / 10 || h > mh * 9 / 10) {
            float s = fminf(mw * 0.9f / w, mh * 0.9f / h);
            w = (int)(w * s); h = (int)(h * s);
        }
        SetWindowSize(w, h);
        SetWindowPosition((int)mpos.x + (mw - w) / 2, (int)mpos.y + (mh - h) / 2);
        newW = SCREEN_H * a->aw / a->ah;
    }
    if (canvas.id == 0 || newW != screenW) {
        if (canvas.id != 0) UnloadRenderTexture(canvas);
        screenW = newW;
        canvas = LoadRenderTexture(screenW, SCREEN_H);
        SetTextureFilter(canvas.texture, TEXTURE_FILTER_BILINEAR);
    }
    updateCanvasTransform();
}

static void loadSettings(void) {
    FILE* f = fopen(SETTINGS_FILE, "r");
    if (!f) return;
    int fs = 0, asp = 0;
    if (fscanf(f, "fullscreen=%d aspect=%d", &fs, &asp) == 2) {
        settingFullscreen = fs ? 1 : 0;
        if (asp >= 0 && asp < NUM_ASPECTS) settingAspect = asp;
    }
    fclose(f);
}

static void saveSettings(void) {
    FILE* f = fopen(SETTINGS_FILE, "w");
    if (!f) return;
    fprintf(f, "fullscreen=%d\naspect=%d\n", settingFullscreen, settingAspect);
    fclose(f);
}

// ============ INPUT ============
// Once an input has triggered something this frame, it is consumed so the same
// key press / click can't also trigger whatever screen comes next.
static int inputConsumed = 0;
static void consumeInput(void) { inputConsumed = 1; }

static int confirmPressed(void) {
    return !inputConsumed && (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER));
}
static int clickPressed(void) { return !inputConsumed && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

// Mouse position in fixed-layout (SCREEN_W x SCREEN_H) coordinates
static Vector2 layoutMouse(void) {
    Vector2 m = GetMousePosition();
    return (Vector2){ (m.x - canvasOffset.x) / canvasScale - layoutX(), (m.y - canvasOffset.y) / canvasScale };
}
static int mouseOver(Rectangle r) { return CheckCollisionPointRec(layoutMouse(), r); }
static int mouseMoved(void) { Vector2 d = GetMouseDelta(); return d.x != 0 || d.y != 0; }
static int clickedOn(Rectangle r) { return clickPressed() && mouseOver(r); }

// Directions use the facing convention: 0 = down, 1 = up, 2 = left, 3 = right
static int dirDown(int d) {
    switch (d) {
    case 0: return IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S);
    case 1: return IsKeyDown(KEY_UP) || IsKeyDown(KEY_W);
    case 2: return IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A);
    default: return IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
    }
}
static int dirPressed(int d) {
    switch (d) {
    case 0: return IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
    case 1: return IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W);
    case 2: return IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A);
    default: return IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D);
    }
}
#define DOWN_PRESSED  dirPressed(0)
#define UP_PRESSED    dirPressed(1)
#define LEFT_PRESSED  dirPressed(2)
#define RIGHT_PRESSED dirPressed(3)

static void drawButton(Rectangle r, const char* label, int fontSize, int selected, int enabled) {
    int hot = selected || mouseOver(r);
    Color edge = !enabled ? (Color) { 70, 80, 100, 255 }
        : hot ? (Color) { 120, 240, 255, 255 } : (Color) { 60, 120, 180, 220 };
    Color fill = hot && enabled ? (Color) { 40, 90, 130, 200 } : (Color) { 15, 25, 45, 220 };
    Color text = enabled ? (hot ? WHITE : (Color) { 190, 220, 240, 255 }) : (Color) { 100, 110, 130, 255 };
    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, hot ? 2.0f : 1.0f, edge);
    int tw = MeasureText(label, fontSize);
    DrawText(label, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - fontSize / 2), fontSize, text);
}

// Which move effect to use in caught mech battles
static int moveEffectForSpecies(int sp) {
    switch (sp) {
    case 1: return FX_BLADE;   // BULWARK - melee
    case 2: return FX_ARC;     // WISP - electric
    case 3: return FX_BEAM;    // RAZOR - beam
    case 4: return FX_MISSILE; // HAVOC
    case 5: return FX_NOVA;    // OBLIVION
    default: return FX_PULSE;  // NOVA
    }
}

// Build a moveset for a mech instance
static void buildMoveSet(Mech* m, MoveSet* ms) {
    int sp = m->speciesIdx;
    // Player-side / friendly attacks
    const char* atkNames[4] = { "STRIKE", "SLASH", "BURST", "OVERDRIVE" };
    int baseDmg[4] = { 6, 9, 12, 20 };
    int costs[4] = { 1, 1, 2, 3 };
    int accs[4] = { 95, 90, 85, 80 };
    int fx[4];
    for (int i = 0; i < 4; i++) fx[i] = moveEffectForSpecies(sp);

    for (int i = 0; i < 4; i++) {
        strncpy(ms->moveNames[i], atkNames[i], 31);
        ms->moveNames[i][31] = 0;
        ms->moveDamage[i] = baseDmg[i];
        ms->moveCost[i] = costs[i];
        ms->moveAccuracy[i] = accs[i];
        ms->moveEffect[i] = fx[i];
        ms->moveColor[i] = species[sp].accent;
        ms->ppMax[i] = (i == 3) ? 5 : (i == 2 ? 10 : 25);
        ms->pp[i] = ms->ppMax[i];
    }
    ms->numMoves = 4;
}

// Build a moveset for an enemy mech (uses hostile-named moves)
static void buildEnemyMoveSet(Mech* m, MoveSet* ms) {
    int sp = m->speciesIdx;
    const char* atkNames[4] = { "ARC ZAP", "BLADE STRIKE", "JAM SIGNAL", "MISSILE VOLLEY" };
    int baseDmg[4] = { 7, 9, 0, 16 };
    int costs[4] = { 1, 1, 0, 2 };
    int accs[4] = { 95, 95, 100, 85 };
    int fx[4] = { FX_ARC, FX_BLADE, FX_JAM, FX_MISSILE };

    for (int i = 0; i < 4; i++) {
        strncpy(ms->moveNames[i], atkNames[i], 31);
        ms->moveNames[i][31] = 0;
        ms->moveDamage[i] = baseDmg[i];
        ms->moveCost[i] = costs[i];
        ms->moveAccuracy[i] = accs[i];
        ms->moveEffect[i] = fx[i];
        ms->moveColor[i] = species[sp].accent;
        ms->ppMax[i] = (i == 3) ? 10 : (i == 2 ? 40 : 30);
        ms->pp[i] = ms->ppMax[i];
    }
    ms->numMoves = 4;
}

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

// ============ MECH FACTORY ============
static int xpNeededForLevel(int level) { return 12 + level * 6; }

static Mech makeMechInstance(int sp, int level) {
    Mech m;
    memset(&m, 0, sizeof(m));
    snprintf(m.name, sizeof(m.name), "%s-%d", species[sp].name, level * 7 + (rand() % 10));
    m.speciesIdx = sp;
    int baseHP = (int)(22 * species[sp].hpMult) + level * 3;
    int baseAtk = (int)(6 * species[sp].atkMult) + level / 2;
    int baseDef = (int)(3 * species[sp].defMult) + level / 3;
    int baseSpd = (int)(5 * species[sp].spdMult) + level / 3;
    m.maxHP = baseHP + rand() % 4;
    m.hp = m.maxHP;
    m.atk = baseAtk;
    m.def = baseDef;
    m.spd = baseSpd;
    m.level = level;
    m.exp = 0;
    m.expToNext = xpNeededForLevel(level);
    return m;
}

// Random wild mech - weighted by rarity
static Mech makeWildMech(void) {
    int weights[NUM_SPECIES];
    int total = 0;
    for (int i = 0; i < NUM_SPECIES; i++) {
        // slot 5 (OBLIVION) only appears at higher levels; skip
        if (i == 5) { weights[i] = 0; continue; }
        int w = (4 - species[i].rarity); // r1->3, r2->2, r3->1
        weights[i] = w; total += w;
    }
    int pick = rand() % total;
    int sp = 0;
    for (int i = 0; i < NUM_SPECIES; i++) {
        if (weights[i] == 0) continue;
        if (pick < weights[i]) { sp = i; break; }
        pick -= weights[i];
    }
    int level = 2 + rand() % 3;
    return makeMechInstance(sp, level);
}

static Mech makeTrainerMechSpecies(int sp, int level) {
    return makeMechInstance(sp, level);
}

static int xpForWild(Mech* enemy) { return 8 + enemy->level * 3 + rand() % 4; }
static int xpForTrainerMech(Mech* enemy, int trainerTier) {
    return 12 + enemy->level * 4 + trainerTier * 8 + rand() % 5;
}

// ============ TRAINER SETUP ============
static void initTrainers(void) {
    trainers[0] = (Trainer){
        "PILOT RHEA", "IRON LEGION", 10, 12, 0,
        (Color) {
255,120,200,255
},
"Hey rookie! Let's see what you've got!",
"You're stronger than you look...",
"Good luck out there, pilot.",
0, 0,
{2}, {3}, 1, 0
    };
    trainers[1] = (Trainer){
        "COMMANDER VOLK", "IRON LEGION", 25, 18, 0,
        (Color) {
255,180,60,255
},
"You dare challenge the Iron Legion?",
"IMPOSSIBLE! My mechs... destroyed!",
"You've earned my respect, pilot.",
1, 0,
{1, 3}, {5, 5}, 2, 0
    };
    trainers[2] = (Trainer){
        "WARDEN KRUX", "IRON LEGION", 14, 22, 0,
        (Color) {
255,60,60,255
},
"Only the strongest reach me. Prepare to be crushed.",
"...You ARE the apex. Well fought.",
"The wasteland is yours. Go.",
2, 0,
{4, 1, 5}, {7, 7, 8}, 3, 0
    };
}

// ============ PLAYER TEAM INIT ============
static void initPlayerTeam(void) {
    team[0] = makeMechInstance(0, 1);   // starter NOVA
    strncpy(team[0].name, "NOVA-7", 31);
    team[0].speciesIdx = 0;
    teamSize = 1;
    activeTeamSlot = 0;
    playerStage = 0;
}

// ============ EFFECTS ============
static void spawnParticle(Vector2 pos, Vector2 vel, float life, float size, Color c) {
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (!particles[i].active) { particles[i] = (Particle){ pos, vel, life, life, size, c, 1 }; return; }
}
static void spawnBurst(Vector2 pos, int count, Color c, float smin, float smax, float life) {
    for (int i = 0; i < count; i++) {
        float ang = (float)GetRandomValue(0, 360) * DEG2RAD;
        float sp = smin + (float)GetRandomValue(0, 100) / 100.0f * (smax - smin);
        Vector2 v = { cosf(ang) * sp, sinf(ang) * sp };
        spawnParticle(pos, v, life * (0.6f + 0.4f * (float)GetRandomValue(0, 100) / 100.0f),
            2.0f + (float)GetRandomValue(0, 4), c);
    }
}
static void addEffect(int kind, int owner, Vector2 from, Vector2 to,
    float duration, Color color, int damage, int hit) {
    for (int i = 0; i < MAX_EFFECTS; i++)
        if (!effects[i].active) { effects[i] = (Effect){ 1, kind, owner, 0, duration, from, to, color, 0, damage, hit }; return; }
}
static void addDamageNum(Vector2 pos, int damage, int miss, int healing) {
    for (int i = 0; i < MAX_DAMAGE_NUMS; i++)
        if (!damageNums[i].active) {
            damageNums[i] = (DamageNum){ 1, pos, 1.2f, damage, healing,
                healing ? (Color) { 120,255,160,255 } : miss ? (Color) { 200,200,200,255 } : (Color) { 255,90,90,255 } };
            return;
        }
}
static void shakeScreen(float amount, float time) {
    if (amount > shakeAmount) { shakeAmount = amount; shakeTimer = time; }
}

static void updateEffects(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        particles[i].life -= dt;
        if (particles[i].life <= 0) { particles[i].active = 0; continue; }
        particles[i].pos.x += particles[i].vel.x * dt;
        particles[i].pos.y += particles[i].vel.y * dt;
        particles[i].vel.x *= 0.96f;
        particles[i].vel.y *= 0.96f;
        particles[i].vel.y += 60 * dt;
    }
    for (int i = 0; i < MAX_EFFECTS; i++) {
        Effect* e = &effects[i];
        if (!e->active) continue;
        e->t += dt;
        float p = e->t / e->duration; if (p > 1) p = 1;
        Vector2 cur = { e->from.x + (e->to.x - e->from.x) * p,
                        e->from.y + (e->to.y - e->from.y) * p };
        switch (e->kind) {
        case FX_PULSE:
            if (p < 0.7f) spawnParticle(cur, (Vector2) { 0, 0 }, 0.25f, 4, e->color);
            if (!e->damageShown && p >= 0.7f) {
                e->damageShown = 1;
                if (e->hit) {
                    spawnBurst(e->to, 12, e->color, 60, 200, 0.5f);
                    addDamageNum((Vector2) { e->to.x, e->to.y - 40 }, e->damage, 0, 0);
                    shakeScreen(6, 0.25f);
                }
                else addDamageNum((Vector2) { e->to.x, e->to.y - 40 }, 0, 1, 0);
            }
            break;
        case FX_BEAM:
            if (p < 0.8f)
                spawnParticle(cur, (Vector2) { (float)GetRandomValue(-40, 40), (float)GetRandomValue(-40, 40) },
                    0.2f, 3, e->color);
            if (!e->damageShown && p >= 0.6f) {
                e->damageShown = 1;
                if (e->hit) {
                    spawnBurst(e->to, 20, e->color, 80, 260, 0.6f);
                    addDamageNum((Vector2) { e->to.x, e->to.y - 40 }, e->damage, 0, 0);
                    shakeScreen(8, 0.3f);
                }
                else addDamageNum((Vector2) { e->to.x + 40, e->to.y - 40 }, 0, 1, 0);
            }
            break;
        case FX_SCAN:
            if (!e->damageShown && p >= 0.9f) {
                e->damageShown = 1;
                spawnBurst(e->to, 24, (Color) { 120, 255, 220, 255 }, 30, 90, 0.8f);
            }
            break;
        case FX_NOVA:
            if (p < 0.5f) {
                if (GetRandomValue(0, 100) < 60)
                    spawnParticle(e->from,
                        (Vector2) {
                    (float)GetRandomValue(-120, 120), (float)GetRandomValue(-120, 120)
                },
                        0.35f, 3, (Color) { 200, 220, 255, 255 });
            }
            else if (!e->damageShown && p >= 0.55f) {
                e->damageShown = 1;
                spawnBurst(e->to, 80, (Color) { 140, 220, 255, 255 }, 100, 500, 1.0f);
                spawnBurst(e->to, 40, WHITE, 50, 400, 0.8f);
                if (e->hit) {
                    addDamageNum((Vector2) { e->to.x, e->to.y - 50 }, e->damage, 0, 0);
                    shakeScreen(22, 0.6f);
                }
                else {
                    addDamageNum((Vector2) { e->to.x + 60, e->to.y - 50 }, 0, 1, 0);
                    shakeScreen(12, 0.4f);
                }
            }
            break;
        case FX_ARC:
            if (p < 0.9f && GetRandomValue(0, 100) < 70) {
                float jx = (float)GetRandomValue(-25, 25), jy = (float)GetRandomValue(-25, 25);
                spawnParticle((Vector2) { cur.x + jx, cur.y + jy },
                    (Vector2) {
                    (float)GetRandomValue(-80, 80), (float)GetRandomValue(-80, 80)
                },
                    0.25f, 3, e->color);
            }
            if (!e->damageShown && p >= 0.7f) {
                e->damageShown = 1;
                if (e->hit) {
                    spawnBurst(e->to, 18, e->color, 100, 300, 0.5f);
                    addDamageNum((Vector2) { e->to.x, e->to.y - 40 }, e->damage, 0, 0);
                    shakeScreen(7, 0.25f);
                }
                else addDamageNum((Vector2) { e->to.x, e->to.y - 40 }, 0, 1, 0);
            }
            break;
        case FX_BLADE:
            if (!e->damageShown && p >= 0.5f) {
                e->damageShown = 1;
                if (e->hit) {
                    spawnBurst(e->to, 22, e->color, 120, 320, 0.5f);
                    addDamageNum((Vector2) { e->to.x, e->to.y - 40 }, e->damage, 0, 0);
                    shakeScreen(9, 0.28f);
                }
                else addDamageNum((Vector2) { e->to.x + 40, e->to.y - 40 }, 0, 1, 0);
            }
            break;
        case FX_JAM:
            if (!e->damageShown && p >= 0.85f) {
                e->damageShown = 1;
                spawnBurst(e->to, 14, e->color, 30, 100, 0.7f);
            }
            break;
        case FX_MISSILE: {
            float arc = sinf(p * PI) * 120;
            Vector2 mpos = { e->from.x + (e->to.x - e->from.x) * p,
                             e->from.y + (e->to.y - e->from.y) * p - arc };
            if (p < 0.9f && GetRandomValue(0, 100) < 80)
                spawnParticle(mpos, (Vector2) { 0, 0 }, 0.3f, 4, (Color) { 255, 180, 120, 255 });
            if (!e->damageShown && p >= 0.9f) {
                e->damageShown = 1;
                spawnBurst(e->to, 40, (Color) { 255, 180, 90, 255 }, 80, 320, 0.8f);
                spawnBurst(e->to, 20, (Color) { 255, 240, 180, 255 }, 40, 200, 0.6f);
                if (e->hit) {
                    addDamageNum((Vector2) { e->to.x, e->to.y - 45 }, e->damage, 0, 0);
                    shakeScreen(14, 0.4f);
                }
                else addDamageNum((Vector2) { e->to.x + 50, e->to.y - 45 }, 0, 1, 0);
            }
            break;
        }
        }
        if (e->t >= e->duration) e->active = 0;
    }
    for (int i = 0; i < MAX_DAMAGE_NUMS; i++) {
        if (!damageNums[i].active) continue;
        damageNums[i].life -= dt;
        damageNums[i].pos.y -= 40 * dt;
        if (damageNums[i].life <= 0) damageNums[i].active = 0;
    }
    if (shakeTimer > 0) { shakeTimer -= dt; if (shakeTimer <= 0) shakeAmount = 0; }
}

// ============ COMBAT ============
static int computeDamage(Battler* attacker, Battler* defender, int moveIdx, int* hitOut) {
    if (moveIdx < 0 || moveIdx >= attacker->moves.numMoves) { *hitOut = 0; return 0; }
    if (rand() % 100 >= attacker->moves.moveAccuracy[moveIdx]) { *hitOut = 0; return 0; }
    *hitOut = 1;
    int dmg = attacker->moves.moveDamage[moveIdx];
    if (dmg <= 0) return 0;
    dmg += attacker->m.atk / 2;
    dmg -= defender->m.def / 2;
    if (dmg < 1) dmg = 1;
    return dmg * (90 + rand() % 20) / 100;
}

static void applyDamage(Battler* defender, int dmg) {
    defender->m.hp -= dmg;
    if (defender->m.hp < 0) defender->m.hp = 0;
}

// ============ BATTLE RESET ============
static void resetBattleCommonState(void) {
    battleMenuSel = 0;
    battleState = 0;
    battleTimer = 0;
    playerEnergy = ENERGY_PER_TURN;
    turnNumber = 1;
    playerEndedTurn = 0;
    pendingPlayerMove = -1;
    pendingEnemyMoveCount = 0;
    pendingEnemyIdx = 0;
    for (int i = 0; i < 4; i++) pendingEnemyActions[i] = 0;
    leveledThisBattle = 0;
    evolvedThisBattle = 0;
    caughtThisBattle = 0;
    lastXpGained = 0;
    for (int i = 0; i < MAX_EFFECTS; i++) effects[i].active = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = 0;
    for (int i = 0; i < MAX_DAMAGE_NUMS; i++) damageNums[i].active = 0;
    shakeAmount = 0;
    shakeTimer = 0;
    battleLog[0] = '\0';
}

static void startWildBattle(void) {
    Mech wild = makeWildMech();
    playerBattler.m = team[activeTeamSlot];
    buildMoveSet(&playerBattler.m, &playerBattler.moves);
    buildEnemyMoveSet(&wild, &enemyBattler.moves);
    enemyBattler.m = wild;
    resetBattleCommonState();
    inTrainerBattle = 0;
    activeTrainerIdx = -1;
    dialoguePhase = DLG_NONE;
    snprintf(battleLog, sizeof(battleLog),
        "HOSTILE %s detected! Reactor online (%d energy).", enemyBattler.m.name, playerEnergy);
}

static void startTrainerBattle(int trainerIdx) {
    if (trainerIdx < 0 || trainerIdx >= NUM_TRAINERS) { startWildBattle(); return; }
    Trainer* t = &trainers[trainerIdx];
    t->numDefeated = 0;

    playerBattler.m = team[activeTeamSlot];
    buildMoveSet(&playerBattler.m, &playerBattler.moves);

    Mech tm = makeTrainerMechSpecies(t->teamSpecies[0], t->teamLevels[0]);
    buildEnemyMoveSet(&tm, &enemyBattler.moves);
    enemyBattler.m = tm;

    resetBattleCommonState();
    inTrainerBattle = 1;
    activeTrainerIdx = trainerIdx;
    snprintf(battleLog, sizeof(battleLog),
        "%s sent out %s! (%d/%d)", t->name, enemyBattler.m.name,
        t->numDefeated + 1, t->numMechs);
}

// ============ ENEMY AI ============
static void queueEnemyActions(void) {
    pendingEnemyMoveCount = 0; pendingEnemyIdx = 0;
    int enemyEnergy = ENERGY_PER_TURN;
    while (enemyEnergy > 0 && pendingEnemyMoveCount < 4) {
        int choices[4], n = 0;
        for (int i = 0; i < enemyBattler.moves.numMoves && i < 4; i++)
            if (enemyBattler.moves.pp[i] > 0 && enemyBattler.moves.moveCost[i] <= enemyEnergy)
                choices[n++] = i;
        if (n == 0) break;
        int mv = choices[rand() % n];
        if (enemyEnergy >= 2) {
            for (int i = 0; i < n; i++)
                if (enemyBattler.moves.moveCost[choices[i]] >= 2 && rand() % 2 == 0) { mv = choices[i]; break; }
        }
        pendingEnemyActions[pendingEnemyMoveCount++] = mv;
        enemyEnergy -= enemyBattler.moves.moveCost[mv];
        enemyBattler.moves.pp[mv]--;
    }
}

static void executeEnemyMove(int mv) {
    if (mv < 0 || mv >= enemyBattler.moves.numMoves) return;
    int hit = 0;
    int dmg = computeDamage(&enemyBattler, &playerBattler, mv, &hit);
    int fx = enemyBattler.moves.moveEffect[mv];
    float dur = fx == FX_NOVA ? 0.9f : (fx == FX_MISSILE ? 0.7f : (fx == FX_BEAM ? 0.5f : 0.55f));
    addEffect(fx, 1, enemyMechPos, playerMechPos, dur, enemyBattler.moves.moveColor[mv], dmg, hit);
    if (hit && dmg > 0) applyDamage(&playerBattler, dmg);
    if (hit && dmg > 0) snprintf(battleLog, sizeof(battleLog), "Enemy %s used %s! %d DMG.",
        enemyBattler.m.name, enemyBattler.moves.moveNames[mv], dmg);
    else if (hit) snprintf(battleLog, sizeof(battleLog), "Enemy %s used %s.",
        enemyBattler.m.name, enemyBattler.moves.moveNames[mv]);
    else snprintf(battleLog, sizeof(battleLog), "Enemy %s used %s... MISSED!",
        enemyBattler.m.name, enemyBattler.moves.moveNames[mv]);
}

static void executePlayerMove(int mv) {
    if (mv < 0 || mv >= playerBattler.moves.numMoves) return;
    int hit = 0;
    int dmg = computeDamage(&playerBattler, &enemyBattler, mv, &hit);
    int fx = playerBattler.moves.moveEffect[mv];
    float dur = fx == FX_NOVA ? 0.9f : (fx == FX_MISSILE ? 0.7f : (fx == FX_BEAM ? 0.5f : 0.55f));
    addEffect(fx, 0, playerMechPos, enemyMechPos, dur, playerBattler.moves.moveColor[mv], dmg, hit);
    if (hit && dmg > 0) applyDamage(&enemyBattler, dmg);
    if (hit && dmg > 0) snprintf(battleLog, sizeof(battleLog), "%s fired %s! %d DMG. [%d EN left]",
        playerBattler.m.name, playerBattler.moves.moveNames[mv], dmg, playerEnergy);
    else if (hit) snprintf(battleLog, sizeof(battleLog), "%s activated %s. [%d EN left]",
        playerBattler.m.name, playerBattler.moves.moveNames[mv], playerEnergy);
    else snprintf(battleLog, sizeof(battleLog), "%s fired %s... MISSED! [%d EN left]",
        playerBattler.m.name, playerBattler.moves.moveNames[mv], playerEnergy);
}

// ============ XP / LEVEL / EVOLUTION ============
static int shouldEvolve(void) {
    // only slot 0 can evolve
    if (activeTeamSlot != 0) return 0;
    if (playerStage >= NUM_STAGES - 1) return 0;
    return team[0].level >= stages[playerStage + 1].requiredLevel;
}

// Award XP to the active mech. Returns levels gained.
static int awardXP(int xp) {
    int gained = 0;
    Mech* m = &team[activeTeamSlot];
    m->exp += xp;
    while (m->exp >= m->expToNext) {
        m->exp -= m->expToNext;
        m->level++;
        m->maxHP += 4;
        m->atk += 2;
        m->def += 1;
        m->spd += 1;
        m->expToNext = xpNeededForLevel(m->level);
        gained++;
    }
    if (gained > 0) m->hp = m->maxHP;
    return gained;
}

static int tryEvolve(void) {
    if (!shouldEvolve()) return 0;
    int next = playerStage + 1;
    EvolutionStage* s = &stages[next];
    Mech* m = &team[0];
    strncpy(m->name, s->name, 31);
    m->speciesIdx = s->toSpeciesIdx;   // swap appearance!
    m->maxHP += s->hpBonus;
    m->atk += s->atkBonus;
    m->def += s->defBonus;
    m->spd += s->spdBonus;
    m->hp = m->maxHP;
    playerStage = next;
    return 1;
}

// ============ CATCHING ============
// Returns 1 if caught, 0 if failed. Only wild battles.
static int tryCatch(void) {
    if (inTrainerBattle) return 0;
    // Catch chance: lower HP = higher chance
    float hpFactor = 1.0f - ((float)enemyBattler.m.hp / enemyBattler.m.maxHP);
    int rarity = species[enemyBattler.m.speciesIdx].rarity;
    float baseChance = 0.25f + hpFactor * 0.55f;
    float rarityPenalty = (rarity - 1) * 0.08f;
    float chance = baseChance - rarityPenalty;
    if (chance < 0.05f) chance = 0.05f;
    if (chance > 0.95f) chance = 0.95f;
    return (rand() % 100) < (int)(chance * 100.0f);
}

static void addToTeam(Mech* m) {
    if (teamSize >= MAX_TEAM) return;
    team[teamSize++] = *m;
}

// ============ MESSAGE ============
static void showMessage(const char* msg, float dur) {
    strncpy(message, msg, sizeof(message) - 1);
    message[sizeof(message) - 1] = 0;
    messageTimer = dur;
}

// ============ SPECIES SPRITE DRAWING ============
// Draws an overworld-sized mech of the given species.
static void drawSpeciesOverworld(int sp, int screenX, int screenY, int facingDir, float t) {
    Color body = species[sp].body;
    Color accent = species[sp].accent;
    Color glow = species[sp].glow;
    float pulse = 0.5f + 0.5f * sinf(t * 4);

    // Legs
    DrawRectangle(screenX + 12, screenY + 28, 6, 10, (Color) { body.r / 2, body.g / 2, body.b / 2, 255 });
    DrawRectangle(screenX + 22, screenY + 28, 6, 10, (Color) { body.r / 2, body.g / 2, body.b / 2, 255 });

    // Body
    DrawRectangle(screenX + 11, screenY + 16, 18, 13, body);
    DrawRectangle(screenX + 11, screenY + 16, 18, 3, accent);

    // Shoulders
    DrawRectangle(screenX + 6, screenY + 18, 6, 6, (Color) { body.r * 3 / 4, body.g * 3 / 4, body.b * 3 / 4, 255 });
    DrawRectangle(screenX + 28, screenY + 18, 6, 6, (Color) { body.r * 3 / 4, body.g * 3 / 4, body.b * 3 / 4, 255 });

    // Head
    DrawRectangle(screenX + 14, screenY + 6, 12, 10, body);
    Color visor = accent;
    if (facingDir == 0)      DrawRectangle(screenX + 16, screenY + 11, 8, 3, visor);
    else if (facingDir == 1) DrawRectangle(screenX + 16, screenY + 7, 8, 2, (Color) { accent.r / 2, accent.g / 2, accent.b / 2, 255 });
    else if (facingDir == 2) DrawRectangle(screenX + 16, screenY + 10, 3, 4, visor);
    else                     DrawRectangle(screenX + 21, screenY + 10, 3, 4, visor);

    // Species-specific decorations
    if (sp == 0) {
        // NOVA: single antenna
        DrawLine(screenX + 20, screenY + 6, screenX + 22, screenY - 2, accent);
        DrawCircle(screenX + 22, screenY - 2, 2, (Color) {
            glow.r, glow.g, glow.b,
                (unsigned char)(180 + pulse * 75)
        });
    }
    else if (sp == 1) {
        // BULWARK: helmet horn + wider chest
        DrawRectangle(screenX + 18, screenY + 1, 4, 4, accent);
        DrawRectangle(screenX + 8, screenY + 20, 24, 3, (Color) { body.r * 3 / 4, body.g * 3 / 4, body.b * 3 / 4, 255 });
    }
    else if (sp == 2) {
        // WISP: side fins
        DrawTriangle((Vector2) { screenX + 14, screenY + 8 },
            (Vector2) {
            screenX + 6, screenY + 2
        },
            (Vector2) {
            screenX + 14, screenY + 14
        }, accent);
        DrawTriangle((Vector2) { screenX + 26, screenY + 8 },
            (Vector2) {
            screenX + 34, screenY + 2
        },
            (Vector2) {
            screenX + 26, screenY + 14
        }, accent);
    }
    else if (sp == 3) {
        // RAZOR: blade arms
        DrawTriangle((Vector2) { screenX + 8, screenY + 22 },
            (Vector2) {
            screenX + 2, screenY + 28
        },
            (Vector2) {
            screenX + 8, screenY + 28
        }, accent);
        DrawTriangle((Vector2) { screenX + 32, screenY + 22 },
            (Vector2) {
            screenX + 38, screenY + 28
        },
            (Vector2) {
            screenX + 32, screenY + 28
        }, accent);
    }
    else if (sp == 4) {
        // HAVOC: shoulder missiles
        DrawRectangle(screenX + 4, screenY + 12, 4, 8, accent);
        DrawRectangle(screenX + 32, screenY + 12, 4, 8, accent);
    }
    else if (sp == 5) {
        // OBLIVION: horns
        DrawTriangle((Vector2) { screenX + 12, screenY + 6 },
            (Vector2) {
            screenX + 8, screenY - 4
        },
            (Vector2) {
            screenX + 16, screenY + 6
        }, (Color) { 220, 30, 60, 255 });
        DrawTriangle((Vector2) { screenX + 28, screenY + 6 },
            (Vector2) {
            screenX + 32, screenY - 4
        },
            (Vector2) {
            screenX + 24, screenY + 6
        }, (Color) { 220, 30, 60, 255 });
    }

    // Chest core
    DrawCircle(screenX + 20, screenY + 22, 3,
        (Color) {
        glow.r, glow.g, glow.b, (unsigned char)(150 + pulse * 100)
    });
}

// Draws a battle-sized mech of the given species.
// Uses the same procedural style but larger with more detail.
static void drawSpeciesBattle(int sp, int cx, int cy, int s, int isEnemy) {
    Color body = species[sp].body;
    Color accent = species[sp].accent;
    Color glow = species[sp].glow;
    if (isEnemy) {
        // tint slightly reddish for hostiles
        body = (Color){ (unsigned char)fminf(255, body.r * 0.9f + 40),
                       (unsigned char)fminf(255, body.g * 0.7f),
                       (unsigned char)fminf(255, body.b * 0.7f), 255 };
        accent = (Color){ (unsigned char)fminf(255, accent.r * 0.9f + 30),
                         (unsigned char)fminf(255, accent.g * 0.7f),
                         (unsigned char)fminf(255, accent.b * 0.7f), 255 };
    }
    float pulse = 0.5f + 0.5f * sinf(glowTimer * 4);
    float breathe = 1.0f + 0.03f * sinf(glowTimer * 3 + sp);
    int sc = (int)(s * breathe);

    // Legs
    DrawRectangle(cx - 22 * sc / 10, cy + 18 * sc / 10, 10 * sc / 10, 24 * sc / 10,
        (Color) {
        body.r / 2, body.g / 2, body.b / 2, 255
    });
    DrawRectangle(cx + 12 * sc / 10, cy + 18 * sc / 10, 10 * sc / 10, 24 * sc / 10,
        (Color) {
        body.r / 2, body.g / 2, body.b / 2, 255
    });
    DrawRectangle(cx - 22 * sc / 10, cy + 34 * sc / 10, 10 * sc / 10, 8 * sc / 10,
        (Color) {
        body.r / 3, body.g / 3, body.b / 3, 255
    });
    DrawRectangle(cx + 12 * sc / 10, cy + 34 * sc / 10, 10 * sc / 10, 8 * sc / 10,
        (Color) {
        body.r / 3, body.g / 3, body.b / 3, 255
    });

    // Torso
    DrawRectangle(cx - 24 * sc / 10, cy - 10 * sc / 10, 48 * sc / 10, 32 * sc / 10, body);
    DrawRectangle(cx - 24 * sc / 10, cy - 10 * sc / 10, 48 * sc / 10, 5 * sc / 10, accent);

    // Shoulders
    DrawRectangle(cx - 38 * sc / 10, cy - 8 * sc / 10, 14 * sc / 10, 16 * sc / 10, accent);
    DrawRectangle(cx + 24 * sc / 10, cy - 8 * sc / 10, 14 * sc / 10, 16 * sc / 10, accent);

    // Head
    DrawRectangle(cx - 18 * sc / 10, cy - 30 * sc / 10, 36 * sc / 10, 22 * sc / 10, body);
    DrawRectangle(cx - 18 * sc / 10, cy - 30 * sc / 10, 36 * sc / 10, 4 * sc / 10, accent);

    // Visor (always glowing)
    DrawRectangle(cx - 14 * sc / 10, cy - 22 * sc / 10, 28 * sc / 10, 6 * sc / 10, accent);
    DrawRectangle(cx - 14 * sc / 10, cy - 22 * sc / 10, 28 * sc / 10, 2 * sc / 10, (Color) { 255, 255, 255, 220 });

    // Arms
    DrawRectangle(cx - 46 * sc / 10, cy - 4 * sc / 10, 12 * sc / 10, 20 * sc / 10,
        (Color) {
        body.r * 3 / 4, body.g * 3 / 4, body.b * 3 / 4, 255
    });
    DrawRectangle(cx + 34 * sc / 10, cy - 4 * sc / 10, 12 * sc / 10, 20 * sc / 10,
        (Color) {
        body.r * 3 / 4, body.g * 3 / 4, body.b * 3 / 4, 255
    });

    // Species-specific battle details
    if (sp == 0) {
        // NOVA: antenna
        DrawLine(cx + 4 * sc / 10, cy - 30 * sc / 10, cx + 12 * sc / 10, cy - 46 * sc / 10, accent);
        DrawCircle(cx + 12 * sc / 10, cy - 46 * sc / 10, 3 * sc / 10,
            (Color) {
            glow.r, glow.g, glow.b, (unsigned char)(180 + pulse * 75)
        });
    }
    else if (sp == 1) {
        // BULWARK: horn + chest plate
        DrawRectangle(cx - 4 * sc / 10, cy - 40 * sc / 10, 8 * sc / 10, 12 * sc / 10, accent);
        DrawRectangle(cx - 24 * sc / 10, cy - 2 * sc / 10, 48 * sc / 10, 4 * sc / 10,
            (Color) {
            body.r * 3 / 4, body.g * 3 / 4, body.b * 3 / 4, 255
        });
        // armoured fists
        DrawRectangle(cx - 50 * sc / 10, cy + 12 * sc / 10, 16 * sc / 10, 8 * sc / 10, accent);
        DrawRectangle(cx + 34 * sc / 10, cy + 12 * sc / 10, 16 * sc / 10, 8 * sc / 10, accent);
    }
    else if (sp == 2) {
        // WISP: side fins & thin frame
        DrawTriangle((Vector2) { cx - 24 * sc / 10, cy - 8 * sc / 10 },
            (Vector2) {
            cx - 40 * sc / 10, cy - 20 * sc / 10
        },
            (Vector2) {
            cx - 24 * sc / 10, cy + 4 * sc / 10
        }, accent);
        DrawTriangle((Vector2) { cx + 24 * sc / 10, cy - 8 * sc / 10 },
            (Vector2) {
            cx + 40 * sc / 10, cy - 20 * sc / 10
        },
            (Vector2) {
            cx + 24 * sc / 10, cy + 4 * sc / 10
        }, accent);
        // antennae
        DrawLine(cx - 8 * sc / 10, cy - 30 * sc / 10, cx - 16 * sc / 10, cy - 46 * sc / 10, accent);
        DrawLine(cx + 8 * sc / 10, cy - 30 * sc / 10, cx + 16 * sc / 10, cy - 46 * sc / 10, accent);
    }
    else if (sp == 3) {
        // RAZOR: blade arms
        DrawTriangle((Vector2) { cx - 46 * sc / 10, cy - 4 * sc / 10 },
            (Vector2) {
            cx - 62 * sc / 10, cy + 14 * sc / 10
        },
            (Vector2) {
            cx - 42 * sc / 10, cy + 6 * sc / 10
        }, accent);
        DrawTriangle((Vector2) { cx + 46 * sc / 10, cy - 4 * sc / 10 },
            (Vector2) {
            cx + 62 * sc / 10, cy + 14 * sc / 10
        },
            (Vector2) {
            cx + 42 * sc / 10, cy + 6 * sc / 10
        }, accent);
        // head crest
        DrawRectangle(cx - 4 * sc / 10, cy - 42 * sc / 10, 8 * sc / 10, 14 * sc / 10, accent);
    }
    else if (sp == 4) {
        // HAVOC: shoulder missile pods
        DrawRectangle(cx - 48 * sc / 10, cy - 18 * sc / 10, 10 * sc / 10, 12 * sc / 10,
            (Color) {
            40, 40, 60, 255
        });
        DrawRectangle(cx + 38 * sc / 10, cy - 18 * sc / 10, 10 * sc / 10, 12 * sc / 10,
            (Color) {
            40, 40, 60, 255
        });
        DrawCircle(cx - 43 * sc / 10, cy - 14 * sc / 10, 2 * sc / 10,
            (Color) {
            255, 120, 60, (unsigned char)(150 + pulse * 100)
        });
        DrawCircle(cx + 43 * sc / 10, cy - 14 * sc / 10, 2 * sc / 10,
            (Color) {
            255, 120, 60, (unsigned char)(150 + pulse * 100)
        });
        // big missile on back
        DrawRectangle(cx - 6 * sc / 10, cy - 46 * sc / 10, 12 * sc / 10, 18 * sc / 10,
            (Color) {
            60, 60, 80, 255
        });
        DrawTriangle((Vector2) { cx - 6 * sc / 10, cy - 46 * sc / 10 },
            (Vector2) {
            cx, cy - 54 * sc / 10
        },
            (Vector2) {
            cx + 6 * sc / 10, cy - 46 * sc / 10
        }, (Color) { 220, 90, 90, 255 });
    }
    else if (sp == 5) {
        // OBLIVION: horns, spikes, glowing red
        DrawTriangle((Vector2) { cx - 18 * sc / 10, cy - 30 * sc / 10 },
            (Vector2) {
            cx - 40 * sc / 10, cy - 54 * sc / 10
        },
            (Vector2) {
            cx - 8 * sc / 10, cy - 30 * sc / 10
        }, (Color) { 220, 30, 60, 255 });
        DrawTriangle((Vector2) { cx + 18 * sc / 10, cy - 30 * sc / 10 },
            (Vector2) {
            cx + 40 * sc / 10, cy - 54 * sc / 10
        },
            (Vector2) {
            cx + 8 * sc / 10, cy - 30 * sc / 10
        }, (Color) { 220, 30, 60, 255 });
        DrawTriangle((Vector2) { cx - 40 * sc / 10, cy - 8 * sc / 10 },
            (Vector2) {
            cx - 58 * sc / 10, cy - 24 * sc / 10
        },
            (Vector2) {
            cx - 30 * sc / 10, cy - 8 * sc / 10
        }, (Color) { 220, 30, 60, 255 });
        DrawTriangle((Vector2) { cx + 40 * sc / 10, cy - 8 * sc / 10 },
            (Vector2) {
            cx + 58 * sc / 10, cy - 24 * sc / 10
        },
            (Vector2) {
            cx + 30 * sc / 10, cy - 8 * sc / 10
        }, (Color) { 220, 30, 60, 255 });
        // big glowing chest core
        DrawCircle(cx, cy + 6 * sc / 10, 12 * sc / 10, (Color) { 80, 10, 20, 255 });
        DrawCircle(cx, cy + 6 * sc / 10, 8 * sc / 10,
            (Color) {
            glow.r, glow.g, glow.b, (unsigned char)(200 + pulse * 55)
        });
        DrawCircle(cx, cy + 6 * sc / 10, 4 * sc / 10, (Color) { 255, 220, 220, 255 });
        DrawCircleLines(cx, cy, 60 * sc / 10,
            (Color) {
            glow.r, glow.g, glow.b, (unsigned char)(40 + pulse * 40)
        });
    }

    // Common chest core for non-OBLIVION
    if (sp != 5) {
        DrawCircle(cx, cy + 6 * sc / 10, 5 * sc / 10,
            (Color) {
            glow.r, glow.g, glow.b, (unsigned char)(180 + pulse * 75)
        });
    }

    DrawCircleLines(cx, cy - 20 * sc / 10, 6 * sc / 10, WHITE);
}

// ============ TRAINER / TILE / BARS ============
static void drawTrainer(Trainer* t, int screenX, int screenY) {
    Color primary = t->color;
    DrawRectangle(screenX + 10, screenY + 26, 8, 12, (Color) { 30, 30, 40, 255 });
    DrawRectangle(screenX + 22, screenY + 26, 8, 12, (Color) { 30, 30, 40, 255 });
    DrawRectangle(screenX + 8, screenY + 12, 24, 16, primary);
    DrawRectangle(screenX + 8, screenY + 12, 24, 4, (Color) { 255, 255, 255, 80 });
    DrawRectangle(screenX + 4, screenY + 12, 6, 8, (Color) { primary.r / 2, primary.g / 2, primary.b / 2, 255 });
    DrawRectangle(screenX + 30, screenY + 12, 6, 8, (Color) { primary.r / 2, primary.g / 2, primary.b / 2, 255 });
    DrawTriangle((Vector2) { screenX + 8, screenY + 14 },
        (Vector2) {
        screenX + 4, screenY + 36
    },
        (Vector2) {
        screenX + 16, screenY + 28
    },
        (Color) {
        primary.r / 3, primary.g / 3, primary.b / 3, 220
    });
    DrawTriangle((Vector2) { screenX + 32, screenY + 14 },
        (Vector2) {
        screenX + 36, screenY + 36
    },
        (Vector2) {
        screenX + 24, screenY + 28
    },
        (Color) {
        primary.r / 3, primary.g / 3, primary.b / 3, 220
    });
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
            (Color) {
            100, 60, 255, (unsigned char)(140 + pulse * 80)
        });
        DrawRectangle(screenX + 10, screenY + 10, TILE_SIZE - 20, TILE_SIZE - 20,
            (Color) {
            180, 100, 255, (unsigned char)(120 + pulse * 80)
        });
        DrawRectangleLines(screenX, screenY, TILE_SIZE, TILE_SIZE, (Color) { 220, 150, 255, 200 });
        break;
    case T_PAD:
        DrawRectangleRec(r, (Color) { 20, 40, 55, 255 });
        DrawRectangle(screenX + 6, screenY + 6, TILE_SIZE - 12, TILE_SIZE - 12,
            (Color) {
            30, (unsigned char)(180 + pulse * 50), 220, 255
        });
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
            (Color) {
            40, (unsigned char)(180 + pulse * 60), 160, 255
        });
        DrawText("T", screenX + 15, screenY + 10, 18, BLACK);
        break;
    }
}

static void drawHPBar(int x, int y, int w, int h, int hp, int maxHP) {
    DrawRectangle(x, y, w, h, (Color) { 20, 20, 30, 255 });
    float pct = maxHP > 0 ? (float)hp / maxHP : 0;
    if (pct < 0) pct = 0;
    Color c = pct > 0.5f ? (Color) { 80, 240, 160, 255 }
    : (pct > 0.2f ? (Color) { 240, 220, 80, 255 }
    : (Color) { 255, 80, 100, 255 });
    DrawRectangle(x + 1, y + 1, (int)((w - 2) * pct), h - 2, c);
    DrawRectangleLines(x, y, w, h, (Color) { 80, 220, 255, 200 });
}

static void drawXPBar(int x, int y, int w, int h, int exp, int needed) {
    DrawRectangle(x, y, w, h, (Color) { 20, 20, 40, 255 });
    float pct = needed > 0 ? (float)exp / needed : 0;
    if (pct < 0) pct = 0; if (pct > 1) pct = 1;
    DrawRectangle(x + 1, y + 1, (int)((w - 2) * pct), h - 2, (Color) { 180, 130, 255, 255 });
    DrawRectangleLines(x, y, w, h, (Color) { 180, 140, 255, 200 });
}

// ============ EFFECT RENDER ============
static void drawEffects(void) {
    for (int i = 0; i < MAX_EFFECTS; i++) {
        Effect* e = &effects[i];
        if (!e->active) continue;
        float p = e->t / e->duration; if (p > 1) p = 1;
        Vector2 cur = { e->from.x + (e->to.x - e->from.x) * p,
                        e->from.y + (e->to.y - e->from.y) * p };
        switch (e->kind) {
        case FX_PULSE: {
            float sz = 6 + 4 * sinf(e->t * 30);
            DrawCircle(cur.x, cur.y, sz + 6, (Color) { e->color.r, e->color.g, e->color.b, 80 });
            DrawCircle(cur.x, cur.y, sz, e->color);
            DrawCircle(cur.x, cur.y, sz * 0.5f, WHITE);
            break;
        }
        case FX_BEAM:
            if (p < 0.85f) {
                float w = 10 * (1 - p); if (w < 2) w = 2;
                DrawLineEx(e->from, e->to, w + 6, (Color) { e->color.r, e->color.g, e->color.b, 90 });
                DrawLineEx(e->from, e->to, w, e->color);
                DrawLineEx(e->from, e->to, w * 0.5f, WHITE);
            }
            else {
                float r = (p - 0.85f) * 6.6f * 30;
                DrawCircle(e->to.x, e->to.y, r, (Color) {
                    e->color.r, e->color.g, e->color.b,
                        (unsigned char)(255 * (1 - p) * 6)
                });
            }
            break;
        case FX_SCAN: {
            float r = p * 80;
            DrawCircleLines((int)e->to.x, (int)e->to.y, r, e->color);
            DrawCircleLines((int)e->to.x, (int)e->to.y, r * 0.7f,
                (Color) {
                e->color.r, e->color.g, e->color.b, 180
            });
            DrawCircleLines((int)e->to.x, (int)e->to.y, r * 0.4f,
                (Color) {
                e->color.r, e->color.g, e->color.b, 120
            });
            const char* txt = "SCANNING...";
            DrawText(txt, (int)(e->to.x - MeasureText(txt, 16) / 2), (int)(e->to.y - 90), 16,
                (Color) {
                200, 255, 240, 255
            });
            break;
        }
        case FX_NOVA:
            if (p < 0.5f) {
                float r = p * 2 * 30;
                DrawCircle(e->from.x, e->from.y, r + 4, (Color) { e->color.r, e->color.g, e->color.b, 80 });
                DrawCircle(e->from.x, e->from.y, r, e->color);
                DrawCircle(e->from.x, e->from.y, r * 0.5f, WHITE);
            }
            else {
                float ep = (p - 0.5f) * 2;
                float r = ep * 160;
                unsigned char a = (unsigned char)(255 * (1 - ep));
                DrawCircle(e->to.x, e->to.y, r, (Color) { e->color.r, e->color.g, e->color.b, a });
                DrawCircle(e->to.x, e->to.y, r * 0.7f, (Color) { 255, 255, 255, a });
                DrawCircle(e->to.x, e->to.y, r * 0.4f, WHITE);
                DrawCircleLines((int)e->to.x, (int)e->to.y, r, WHITE);
                DrawCircleLines((int)e->to.x, (int)e->to.y, r * 1.3f,
                    (Color) {
                    220, 240, 255, (unsigned char)(a * 0.6f)
                });
            }
            break;
        case FX_ARC: {
            int segments = 8;
            Vector2 prev = e->from;
            for (int s = 1; s <= segments; s++) {
                float seg_p = (float)s / segments;
                Vector2 target = { e->from.x + (e->to.x - e->from.x) * seg_p * p,
                                   e->from.y + (e->to.y - e->from.y) * seg_p * p };
                if (s < segments) {
                    target.x += (float)GetRandomValue(-18, 18);
                    target.y += (float)GetRandomValue(-18, 18);
                }
                DrawLineEx(prev, target, 5, (Color) { e->color.r, e->color.g, e->color.b, 100 });
                DrawLineEx(prev, target, 2, e->color);
                DrawLineEx(prev, target, 1, WHITE);
                prev = target;
            }
            if (p >= 0.7f) {
                float ep = (p - 0.7f) / 0.3f;
                float r = ep * 60;
                unsigned char a = (unsigned char)(255 * (1 - ep));
                DrawCircle(e->to.x, e->to.y, r, (Color) { 255, 255, 200, a });
            }
            break;
        }
        case FX_BLADE: {
            float ang = p * PI;
            for (int dir = -1; dir <= 1; dir += 2) {
                Vector2 a1 = { e->to.x + cosf(ang + dir * 0.3f) * 60,
                               e->to.y + sinf(ang + dir * 0.3f) * 60 };
                Vector2 a2 = { e->to.x + cosf(ang + dir * 0.6f) * 70,
                               e->to.y + sinf(ang + dir * 0.6f) * 70 };
                DrawLineEx(a1, a2, 6, (Color) { e->color.r, e->color.g, e->color.b, 180 });
                DrawLineEx(a1, a2, 3, e->color);
                DrawLineEx(a1, a2, 1, WHITE);
            }
            if (p > 0.3f && p < 0.7f) {
                float fp = (p - 0.3f) / 0.4f;
                DrawCircleLines((int)e->to.x, (int)e->to.y, 40 + fp * 30, e->color);
            }
            break;
        }
        case FX_JAM:
            for (int k = 0; k < 3; k++) {
                float rp = p - k * 0.15f;
                if (rp > 0 && rp < 1) {
                    float r = rp * 120;
                    unsigned char a = (unsigned char)(255 * (1 - rp));
                    DrawCircleLines((int)e->to.x, (int)e->to.y, r,
                        (Color) {
                        e->color.r, e->color.g, e->color.b, a
                    });
                }
            }
            for (int k = 0; k < 6; k++) {
                int ny = (int)e->to.y + GetRandomValue(-50, 50);
                int nx1 = (int)e->to.x + GetRandomValue(-60, 0);
                int nx2 = (int)e->to.x + GetRandomValue(0, 60);
                DrawLine(nx1, ny, nx2, ny, (Color) { e->color.r, e->color.g, e->color.b, 120 });
            }
            break;
        case FX_MISSILE: {
            float arc = sinf(p * PI) * 120;
            Vector2 mpos = { e->from.x + (e->to.x - e->from.x) * p,
                             e->from.y + (e->to.y - e->from.y) * p - arc };
            if (p < 0.9f) {
                DrawCircle(mpos.x, mpos.y, 8, e->color);
                DrawCircle(mpos.x, mpos.y, 5, (Color) { 255, 240, 200, 255 });
                DrawCircleLines((int)mpos.x, (int)mpos.y, 10, WHITE);
                for (int k = 0; k < 3; k++) {
                    float ofs = (k + 1) * 6;
                    DrawCircle(mpos.x - ofs, mpos.y + ofs * 0.5f, 6 - k * 2,
                        (Color) {
                        255, 180, 80, (unsigned char)(200 - k * 60)
                    });
                }
            }
            else {
                float ep = (p - 0.9f) / 0.1f;
                float r = 40 + ep * 80;
                unsigned char a = (unsigned char)(255 * (1 - ep));
                DrawCircle(e->to.x, e->to.y, r, (Color) { 255, 160, 60, a });
                DrawCircle(e->to.x, e->to.y, r * 0.6f, (Color) { 255, 240, 180, a });
                DrawCircle(e->to.x, e->to.y, r * 0.3f, (Color) { 255, 255, 255, a });
            }
            break;
        }
        }
    }
}

static void drawParticles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &particles[i];
        if (!p->active) continue;
        float a = p->life / p->maxLife;
        Color c = p->color;
        c.a = (unsigned char)(c.a * a);
        DrawCircle(p->pos.x, p->pos.y, p->size * a + 0.5f, c);
    }
}

static void drawDamageNums(void) {
    for (int i = 0; i < MAX_DAMAGE_NUMS; i++) {
        DamageNum* d = &damageNums[i];
        if (!d->active) continue;
        float a = d->life / 1.2f;
        Color c = d->color; c.a = (unsigned char)(255 * a);
        const char* txt;
        if (d->healing) txt = TextFormat("+%d", d->damage);
        else if (d->damage == 0) txt = "MISS";
        else txt = TextFormat("%d", d->damage);
        int sz = d->healing ? 24 : 28;
        int w = MeasureText(txt, sz);
        DrawText(txt, (int)(d->pos.x - w / 2 + 2), (int)d->pos.y + 2, sz, (Color) { 0, 0, 0, c.a });
        DrawText(txt, (int)(d->pos.x - w / 2), (int)d->pos.y, sz, c);
    }
}

// ============ BATTLE UI LAYOUT ============
static Rectangle moveButtonRect(int i) {
    int col = i % 2, row = i / 2;
    return (Rectangle) { 35.0f + col * 380, SCREEN_H - 135.0f + row * 58, 350, 48 };
}
static Rectangle catchButtonRect(void) { return (Rectangle) { 460, SCREEN_H - 157, 130, 20 }; }
static Rectangle endTurnButtonRect(void) { return (Rectangle) { 600, SCREEN_H - 157, 170, 20 }; }
static int canCatchNow(void) { return !inTrainerBattle && teamSize < MAX_TEAM; }

// ============ BATTLE DRAW ============
static void drawBattle(void) {
    float sx = 0, sy = 0;
    if (shakeTimer > 0) {
        sx = (float)GetRandomValue(-100, 100) / 100.0f * shakeAmount;
        sy = (float)GetRandomValue(-100, 100) / 100.0f * shakeAmount;
    }
    ClearBackground((Color) { 10, 12, 22, 255 });
    int starCount = 60 * screenW / SCREEN_W;
    for (int i = 0; i < starCount; i++) {
        int stx = (i * 137) % screenW;
        int sty = (i * 91) % SCREEN_H;
        float t = glowTimer * 30 + i;
        int bright = 40 + (int)(30 * (sinf(t * 0.1f) * 0.5f + 0.5f));
        DrawPixel(stx, sty, (Color) { bright, bright, (unsigned char)(bright + 40), 255 });
    }
    for (int i = 0; i < 20; i++) {
        int y = 180 + i * 12;
        unsigned char a = (unsigned char)(80 - i * 3);
        DrawLine(0, y, screenW, y, (Color) { 30, 60, 100, a });
    }
    int fanLines = screenW / 80 + 1;
    for (int i = -fanLines; i <= fanLines; i++) {
        int x = screenW / 2 + i * 40;
        DrawLine(x, 180, x + i * 30, SCREEN_H, (Color) { 30, 60, 100, 60 });
    }

    BeginMode2D(layoutCamera());

    // Draw mechs by species
    drawSpeciesBattle(enemyBattler.m.speciesIdx, (int)(enemyMechPos.x + sx), (int)(enemyMechPos.y + sy), 14, 1);
    drawSpeciesBattle(playerBattler.m.speciesIdx, (int)(playerMechPos.x + sx), (int)(playerMechPos.y + sy), 14, 0);

    drawEffects();
    drawParticles();
    drawDamageNums();

    // Enemy HUD
    DrawRectangle(20, 20, 300, 70, (Color) { 20, 25, 40, 230 });
    if (inTrainerBattle && activeTrainerIdx >= 0 && activeTrainerIdx < NUM_TRAINERS) {
        Trainer* t = &trainers[activeTrainerIdx];
        DrawRectangleLines(20, 20, 300, 70, (Color) { 255, 200, 60, 220 });
        DrawText(TextFormat("%s  %s", t->team, t->name), 28, 24, 12, (Color) { 255, 220, 100, 255 });
        DrawText(enemyBattler.m.name, 30, 38, 22, (Color) { 255, 220, 220, 255 });
        DrawText(TextFormat("Lv.%d", enemyBattler.m.level), 255, 38, 20, WHITE);
        drawHPBar(30, 68, 280, 12, enemyBattler.m.hp, enemyBattler.m.maxHP);
        DrawText(TextFormat("%d/%d HP", enemyBattler.m.hp, enemyBattler.m.maxHP), 30, 82, 11, WHITE);
        DrawText(TextFormat("MECH %d/%d", t->numDefeated + 1, t->numMechs),
            255, 82, 11, (Color) { 255, 200, 100, 255 });
    }
    else {
        DrawRectangleLines(20, 20, 300, 70, (Color) { 255, 80, 80, 220 });
        DrawText("HOSTILE", 28, 24, 12, (Color) { 255, 100, 100, 255 });
        DrawText(enemyBattler.m.name, 30, 38, 22, (Color) { 255, 200, 200, 255 });
        DrawText(TextFormat("Lv.%d", enemyBattler.m.level), 255, 38, 20, WHITE);
        drawHPBar(30, 68, 280, 12, enemyBattler.m.hp, enemyBattler.m.maxHP);
        DrawText(TextFormat("%d/%d HP", enemyBattler.m.hp, enemyBattler.m.maxHP), 30, 82, 11, WHITE);
        // Species name for wild
        DrawText(species[enemyBattler.m.speciesIdx].name, 30, 96, 11, (Color) { 200, 200, 240, 255 });
    }

    // Player HUD
    DrawRectangle(400, 220, 300, 100, (Color) { 20, 30, 50, 230 });
    DrawRectangleLines(400, 220, 300, 100, species[playerBattler.m.speciesIdx].accent);
    DrawText("ALLIED", 408, 224, 12, (Color) { 100, 240, 255, 255 });
    DrawText(playerBattler.m.name, 410, 238, 22, species[playerBattler.m.speciesIdx].accent);
    DrawText(TextFormat("Lv.%d", playerBattler.m.level), 655, 238, 20, WHITE);
    drawHPBar(410, 268, 280, 12, playerBattler.m.hp, playerBattler.m.maxHP);
    DrawText(TextFormat("%d/%d HP", playerBattler.m.hp, playerBattler.m.maxHP), 410, 282, 11, WHITE);
    drawXPBar(410, 300, 280, 8, playerBattler.m.exp, playerBattler.m.expToNext);
    DrawText(TextFormat("XP %d/%d", playerBattler.m.exp, playerBattler.m.expToNext),
        410, 310, 11, (Color) { 200, 170, 255, 255 });

    // Reactor
    DrawRectangle(320, 20, 200, 70, (Color) { 15, 25, 45, 230 });
    DrawRectangleLines(320, 20, 200, 70, (Color) { 100, 200, 255, 220 });
    DrawText("REACTOR", 400, 25, 14, (Color) { 150, 220, 255, 255 });
    for (int i = 0; i < MAX_ENERGY; i++) {
        int cx = 350 + i * 50, cy = 65;
        int active = (i < playerEnergy);
        Color fill = active ? (Color) { 60, 200, 255, 255 } : (Color) { 40, 45, 60, 255 };
        Color edge = active ? (Color) { 180, 240, 255, 255 } : (Color) { 70, 80, 100, 255 };
        if (active) {
            float p = 0.5f + 0.5f * sinf(glowTimer * 5 + i);
            DrawCircle(cx, cy, 15 + (int)(p * 3), (Color) { 60, 180, 255, 40 });
        }
        DrawCircle(cx, cy, 15, fill);
        DrawCircleLines(cx, cy, 15, edge);
        DrawCircle(cx - 5, cy - 5, 4, (Color) { 255, 255, 255, (unsigned char)(active ? 220 : 40) });
    }
    DrawText(TextFormat("TURN %d", turnNumber), 330, 96, 14, (Color) { 150, 220, 255, 255 });

    // Bottom panel
    DrawRectangle(20, SCREEN_H - 160, SCREEN_W - 40, 140, (Color) { 15, 20, 35, 240 });
    DrawRectangleLines(20, SCREEN_H - 160, SCREEN_W - 40, 140, (Color) { 80, 220, 255, 200 });

    if (battleState == 0 && dialoguePhase == DLG_NONE) {
        DrawText(">> SELECT WEAPON <<", 30, SCREEN_H - 155, 14, (Color) { 100, 240, 255, 255 });
        for (int i = 0; i < playerBattler.moves.numMoves; i++) {
            Rectangle mr = moveButtonRect(i);
            int bx = (int)mr.x + 5, by = (int)mr.y + 5;
            int cost = playerBattler.moves.moveCost[i];
            int affordable = (cost <= playerEnergy) && (playerBattler.moves.pp[i] > 0);
            int selected = (i == battleMenuSel);
            Color moveCol = playerBattler.moves.moveColor[i];
            Color border = selected
                ? (affordable ? moveCol : (Color) { 120, 120, 120, 255 })
                : (Color) { 60, 120, 180, 200 };
            Color textCol = affordable ? (Color) { 220, 240, 255, 255 } : (Color) { 110, 120, 140, 255 };
            if (selected && affordable)
                DrawRectangle(bx - 5, by - 5, 350, 48, (Color) { moveCol.r, moveCol.g, moveCol.b, 60 });
            DrawRectangleLines(bx - 5, by - 5, 350, 48, border);
            DrawCircle(bx + 8, by + 22, 6, affordable ? moveCol : (Color) { 90, 90, 100, 255 });
            DrawCircleLines(bx + 8, by + 22, 6, WHITE);
            DrawText(playerBattler.moves.moveNames[i], bx + 20, by, 20, textCol);
            DrawText(TextFormat("PP %d/%d", playerBattler.moves.pp[i], playerBattler.moves.ppMax[i]),
                bx + 220, by + 5, 14, (Color) { 150, 200, 220, 255 });
            for (int e = 0; e < cost; e++) {
                int ex = bx + 290 + e * 18, ey = by + 12;
                DrawCircle(ex, ey, 7, affordable ? (Color) { 60, 200, 255, 255 } : (Color) { 70, 80, 100, 255 });
                DrawCircleLines(ex, ey, 7, (Color) { 180, 240, 255, 255 });
            }
            if (cost == 0) DrawText("FREE", bx + 290, by + 5, 12, (Color) { 120, 255, 180, 255 });
        }
        drawButton(catchButtonRect(), "CATCH [C]", 12, 0, canCatchNow());
        drawButton(endTurnButtonRect(), "END TURN [X]", 12, 0, 1);
        DrawText("[Z/ENTER/CLICK] DEPLOY   [WASD/ARROWS] SELECT",
            30, SCREEN_H - 18, 14, (Color) { 100, 240, 255, 255 });
    }
    else {
        DrawText(">> SYS LOG <<", 30, SCREEN_H - 155, 14, (Color) { 100, 240, 255, 255 });
        DrawText(battleLog, 35, SCREEN_H - 130, 20, (Color) { 220, 240, 255, 255 });

        if (battleState == 3 && dialoguePhase == DLG_NONE) {
            if (inTrainerBattle)
                DrawText(">> VICTORY! [Z/CLICK] TO CONTINUE <<", 35, SCREEN_H - 75, 20,
                    (Color) {
                255, 220, 100, 255
            });
            else
                DrawText(">> TARGET DESTROYED! [Z/CLICK] <<", 35, SCREEN_H - 75, 20,
                    (Color) {
                120, 255, 180, 255
            });
            DrawText(TextFormat("+%d XP awarded", lastXpGained), 500, SCREEN_H - 75, 20,
                (Color) {
                200, 170, 255, 255
            });
        }
        if (battleState == 4 && dialoguePhase == DLG_NONE)
            DrawText(">> MECH DISABLED! [Z/CLICK] TO CONTINUE <<", 35, SCREEN_H - 75, 20,
                (Color) {
            255, 100, 100, 255
        });

        if (dialoguePhase != DLG_NONE) {
            DrawRectangle(20, SCREEN_H - 260, SCREEN_W - 40, 100,
                (Color) {
                10, 15, 30, 245
            });
            DrawRectangleLines(20, SCREEN_H - 260, SCREEN_W - 40, 100,
                (Color) {
                255, 220, 100, 240
            });
            if (inTrainerBattle && activeTrainerIdx >= 0 && activeTrainerIdx < NUM_TRAINERS) {
                Trainer* t = &trainers[activeTrainerIdx];
                DrawText(TextFormat("%s  %s", t->team, t->name),
                    35, SCREEN_H - 252, 14, (Color) { 255, 220, 100, 255 });
            }
            DrawText(dialogueText, 40, SCREEN_H - 225, 22, (Color) { 255, 240, 220, 255 });
            DrawText("[Z/CLICK] to continue", SCREEN_W - 220, SCREEN_H - 180, 16,
                (Color) {
                150, 200, 255, 255
            });
        }
    }

    EndMode2D();
}

// ============ LEVELUP SCREEN ============
static void drawLevelUpScreen(void) {
    ClearBackground((Color) { 5, 8, 18, 255 });
    int starCount = 120 * screenW / SCREEN_W;
    for (int i = 0; i < starCount; i++) {
        int stx = (i * 137 + (int)(glowTimer * 40)) % screenW;
        int sty = (i * 91) % SCREEN_H;
        float t = glowTimer * 20 + i;
        int bright = 60 + (int)(80 * (sinf(t * 0.2f) * 0.5f + 0.5f));
        DrawPixel(stx, sty, (Color) { bright, bright, (unsigned char)(bright + 60), 255 });
    }
    BeginMode2D(layoutCamera());
    if (levelUpSubState == 0) {
        const char* title = "LEVEL UP!";
        int tw = MeasureText(title, 48);
        DrawText(title, SCREEN_W / 2 - tw / 2, 60, 48, (Color) { 200, 240, 255, 255 });
        DrawText("=====", SCREEN_W / 2 - 20, 115, 20, (Color) { 100, 240, 255, 255 });
        Mech* m = &team[activeTeamSlot];
        DrawText(TextFormat("MECH: %s", m->name), 200, 180, 28,
            species[m->speciesIdx].accent);
        DrawText(TextFormat("LEVEL %d", m->level), 200, 220, 28, WHITE);
        int y = 290;
        DrawText(TextFormat("HP   %d / %d", m->hp, m->maxHP), 240, y, 24, (Color) { 120, 255, 160, 255 });
        DrawText(TextFormat("ATK  %d", m->atk), 240, y + 35, 24, (Color) { 255, 150, 150, 255 });
        DrawText(TextFormat("DEF  %d", m->def), 240, y + 70, 24, (Color) { 150, 180, 255, 255 });
        DrawText(TextFormat("SPD  %d", m->spd), 240, y + 105, 24, (Color) { 255, 220, 120, 255 });
        DrawText(TextFormat("NEXT LV: %d / %d XP", m->exp, m->expToNext),
            240, 440, 20, (Color) { 200, 170, 255, 255 });
        drawXPBar(240, 465, 320, 12, m->exp, m->expToNext);
        if (activeTeamSlot == 0 && playerStage < NUM_STAGES - 1) {
            int nextLv = stages[playerStage + 1].requiredLevel;
            DrawText(TextFormat("Evolves to %s at LV.%d",
                stages[playerStage + 1].name, nextLv),
                240, 500, 18, (Color) { 140, 200, 255, 255 });
        }
        else if (activeTeamSlot == 0) {
            DrawText("FINAL FORM ACHIEVED", 240, 500, 18, (Color) { 255, 220, 120, 255 });
        }
        if (levelUpTimer < 2.0f) {
            float a = 1.0f - levelUpTimer / 2.0f;
            DrawText("+4 HP", 400, 290, 22, (Color) { 120, 255, 160, (unsigned char)(255 * a) });
            DrawText("+2 ATK", 400, 325, 22, (Color) { 255, 150, 150, (unsigned char)(255 * a) });
            DrawText("+1 DEF", 400, 360, 22, (Color) { 150, 180, 255, (unsigned char)(255 * a) });
            DrawText("+1 SPD", 400, 395, 22, (Color) { 255, 220, 120, (unsigned char)(255 * a) });
        }
        DrawText("[Z/CLICK] to continue", 290, 550, 20, (Color) { 100, 240, 255, 255 });
    }
    else {
        Mech* m = &team[0]; // evolution always for slot 0
        Color accent = species[m->speciesIdx].accent;
        Color body = species[m->speciesIdx].body;
        float p = 0.5f + 0.5f * sinf(glowTimer * 3);
        int ringSize = (playerStage == 2) ? 180 : 120;
        DrawCircle(SCREEN_W / 2, 220, ringSize + (int)(p * 25),
            (Color) {
            body.r, body.g, body.b, 60
        });
        DrawCircle(SCREEN_W / 2, 220, ringSize - 20 + (int)(p * 18),
            (Color) {
            accent.r, accent.g, accent.b, 90
        });
        DrawCircleLines(SCREEN_W / 2, 220, ringSize + 20, accent);
        if (playerStage == 2) {
            DrawCircleLines(SCREEN_W / 2, 220, ringSize + 45, (Color) { 255, 50, 80, 120 });
            DrawCircleLines(SCREEN_W / 2, 220, ringSize + 70, (Color) { 255, 50, 80, 60 });
        }
        int battleScale = (playerStage == 2) ? 15 : 20;
        drawSpeciesBattle(m->speciesIdx, SCREEN_W / 2, 220, battleScale, 0);
        const char* title = (playerStage == 2) ? "APEX EVOLUTION" : "EVOLUTION!";
        int tw = MeasureText(title, 56);
        DrawText(title, SCREEN_W / 2 - tw / 2, 40, 56,
            (playerStage == 2) ? (Color) { 255, 80, 80, 255 } : (Color) { 255, 220, 120, 255 });
        EvolutionStage* s = &stages[playerStage];
        const char* msg = s->evolveMsg;
        int mw = MeasureText(msg, 24);
        DrawText(msg, SCREEN_W / 2 - mw / 2, 395, 24, accent);
        DrawText(TextFormat("+%d MAX HP", s->hpBonus), 280, 440, 20, (Color) { 120, 255, 160, 255 });
        DrawText(TextFormat("+%d ATK", s->atkBonus), 280, 465, 20, (Color) { 255, 150, 150, 255 });
        DrawText(TextFormat("+%d DEF", s->defBonus), 280, 490, 20, (Color) { 150, 180, 255, 255 });
        DrawText(TextFormat("+%d SPD", s->spdBonus), 280, 515, 20, (Color) { 255, 220, 120, 255 });
        if (glowTimer - levelUpTimer > 0.5f)
            DrawText("[Z/CLICK] to continue", 290, 555, 20, (Color) { 100, 240, 255, 255 });
        if (GetRandomValue(0, 100) < 60) {
            Vector2 pos = { (float)(SCREEN_W / 2 + GetRandomValue(-140, 140)),
                            (float)(220 + GetRandomValue(-140, 140)) };
            spawnParticle(pos, (Vector2) { 0, -30 - (float)GetRandomValue(0, 80) },
                0.9f, (playerStage == 2) ? 4 : 3, accent);
        }
    }
    EndMode2D();
}

// ============ TEAM SCREEN ============
static Rectangle teamSlotRect(int i) {
    int col = i % 2, row = i / 2;
    return (Rectangle) { 40.0f + col * 380, 90.0f + row * 160, 340, 140 };
}
static Rectangle teamCloseButtonRect(void) { return (Rectangle) { 30, SCREEN_H - 42, 200, 30 }; }

static void drawTeamScreen(void) {
    ClearBackground((Color) { 8, 12, 22, 255 });
    DrawRectangle(0, 0, screenW, 60, (Color) { 15, 25, 45, 255 });
    DrawRectangleLines(0, 0, screenW, 60, (Color) { 100, 200, 255, 220 });
    BeginMode2D(layoutCamera());
    DrawText(">> MECH TEAM", 30, 18, 26, (Color) { 150, 220, 255, 255 });
    DrawText(TextFormat("%d / %d", teamSize, MAX_TEAM), SCREEN_W - 100, 22, 20, WHITE);

    for (int i = 0; i < teamSize; i++) {
        Rectangle slot = teamSlotRect(i);
        int bx = (int)slot.x;
        int by = (int)slot.y;
        int sp = team[i].speciesIdx;

        // Slot panel
        DrawRectangle(bx, by, 340, 140, (Color) { 20, 30, 50, 230 });
        if (i == teamSel)
            DrawRectangleLinesEx((Rectangle) { bx - 4.0f, by - 4.0f, 348, 148 }, 2, (Color) { 120, 240, 255, 255 });
        DrawRectangleLines(bx, by, 340, 140,
            i == activeTeamSlot ? (Color) { 255, 220, 100, 255 }
        : (Color) { 80, 140, 200, 200 });
        if (i == activeTeamSlot)
            DrawText("ACTIVE", bx + 8, by + 6, 12, (Color) { 255, 220, 100, 255 });

        // Sprite in a box on the left
        DrawRectangle(bx + 10, by + 24, 90, 100, (Color) { 10, 20, 35, 255 });
        DrawRectangleLines(bx + 10, by + 24, 90, 100, species[sp].accent);
        // Draw overworld-sized sprite, centered in box
        drawSpeciesOverworld(sp, bx + 35, by + 60, 0, glowTimer);

        // Stats on the right
        DrawText(team[i].name, bx + 110, by + 28, 22, species[sp].accent);
        DrawText(TextFormat("Lv. %d", team[i].level), bx + 260, by + 28, 18, WHITE);
        DrawText(species[sp].name, bx + 110, by + 52, 14, (Color) { 180, 200, 220, 255 });
        DrawText(species[sp].desc, bx + 110, by + 70, 12, (Color) { 150, 170, 190, 220 });

        // HP bar
        DrawText("HP", bx + 110, by + 92, 12, (Color) { 120, 255, 160, 255 });
        drawHPBar(bx + 130, by + 92, 180, 10, team[i].hp, team[i].maxHP);
        DrawText(TextFormat("%d/%d", team[i].hp, team[i].maxHP), bx + 220, by + 92, 10, WHITE);

        // ATK / DEF / SPD
        DrawText(TextFormat("ATK %d", team[i].atk), bx + 110, by + 112, 14, (Color) { 255, 150, 150, 255 });
        DrawText(TextFormat("DEF %d", team[i].def), bx + 180, by + 112, 14, (Color) { 150, 180, 255, 255 });
        DrawText(TextFormat("SPD %d", team[i].spd), bx + 250, by + 112, 14, (Color) { 255, 220, 120, 255 });
    }

    if (teamSize == 0)
        DrawText("NO MECHS IN TEAM", SCREEN_W / 2 - 120, SCREEN_H / 2, 24, (Color) { 150, 150, 150, 255 });

    drawButton(teamCloseButtonRect(), "CLOSE [TAB/ESC]", 16, 0, 1);
    DrawText("[Z/ENTER/CLICK] Set as active", SCREEN_W - 330, SCREEN_H - 34, 18,
        (Color) {
        255, 220, 100, 255
    });
    EndMode2D();
}

// ============ BATTLE UPDATE ============
static void handlePlayerVictoryOverEnemy(void) {
    if (inTrainerBattle) {
        Trainer* t = &trainers[activeTrainerIdx];
        t->numDefeated++;

        int xp = xpForTrainerMech(&enemyBattler.m, t->tier);
        lastXpGained += xp;
        if (awardXP(xp) > 0) leveledThisBattle = 1;
        if (tryEvolve()) evolvedThisBattle = 1;

        if (t->numDefeated < t->numMechs) {
            Mech tm = makeTrainerMechSpecies(t->teamSpecies[t->numDefeated],
                t->teamLevels[t->numDefeated]);
            buildEnemyMoveSet(&tm, &enemyBattler.moves);
            enemyBattler.m = tm;
            playerEnergy = ENERGY_PER_TURN;
            turnNumber = 1;
            pendingPlayerMove = -1;
            battleState = 0;
            battleMenuSel = 0;
            snprintf(battleLog, sizeof(battleLog),
                "%s sent out %s! (%d/%d)", t->name, enemyBattler.m.name,
                t->numDefeated + 1, t->numMechs);
        }
        else {
            t->defeated = 1;
            battleState = 3;
            snprintf(battleLog, sizeof(battleLog),
                "ALL MECHS DOWN! %s defeated!", t->name);
            dialoguePhase = DLG_DEFEAT;
            snprintf(dialogueText, sizeof(dialogueText), "\"%s\"", t->defeatLine);
        }
    }
    else {
        int xp = xpForWild(&enemyBattler.m);
        lastXpGained = xp;
        if (awardXP(xp) > 0) leveledThisBattle = 1;
        if (tryEvolve()) evolvedThisBattle = 1;
        battleState = 3;
        if (evolvedThisBattle) snprintf(battleLog, sizeof(battleLog),
            "TARGET %s DESTROYED! Evolution triggered!", enemyBattler.m.name);
        else if (leveledThisBattle) snprintf(battleLog, sizeof(battleLog),
            "TARGET %s DESTROYED! Level up!", enemyBattler.m.name);
        else snprintf(battleLog, sizeof(battleLog),
            "TARGET %s DESTROYED! +%d XP.", enemyBattler.m.name, xp);
    }
}

static void finishBattleAndReturn(void) {
    // Sync battle state back to team
    team[activeTeamSlot].hp = playerBattler.m.hp;
    team[activeTeamSlot].exp = playerBattler.m.exp;
    team[activeTeamSlot].expToNext = playerBattler.m.expToNext;
    if (leveledThisBattle) team[activeTeamSlot].hp = team[activeTeamSlot].maxHP;

    if (leveledThisBattle || evolvedThisBattle) {
        levelUpTimer = 0;
        levelUpSubState = 0;
        pendingPlayerMove = -100;
    }
    else {
        pendingPlayerMove = -101;
    }
}

static void updateBattle(float dt) {
    battleTimer += dt;
    updateEffects(dt);

    if (dialoguePhase != DLG_NONE) {
        if (confirmPressed() || clickPressed()) {
            consumeInput();
            int wasPhase = dialoguePhase;
            dialoguePhase = DLG_NONE;
            if (wasPhase == DLG_DEFEAT) finishBattleAndReturn();
        }
        return;
    }

    if (battleState == 0) {
        int busy = 0;
        for (int i = 0; i < MAX_EFFECTS; i++) if (effects[i].active) busy = 1;
        if (busy) return;

        if (battleMenuSel < 0) battleMenuSel = 0;
        if (battleMenuSel >= playerBattler.moves.numMoves) battleMenuSel = playerBattler.moves.numMoves - 1;

        if (RIGHT_PRESSED) battleMenuSel = (battleMenuSel + 1) % playerBattler.moves.numMoves;
        if (LEFT_PRESSED)  battleMenuSel = (battleMenuSel + playerBattler.moves.numMoves - 1) % playerBattler.moves.numMoves;
        if (DOWN_PRESSED)  battleMenuSel = (battleMenuSel + 2) % playerBattler.moves.numMoves;
        if (UP_PRESSED)    battleMenuSel = (battleMenuSel + playerBattler.moves.numMoves - 2) % playerBattler.moves.numMoves;

        // Mouse: hovering selects a weapon, clicking fires it
        int clickedMove = 0;
        for (int i = 0; i < playerBattler.moves.numMoves; i++) {
            if (mouseMoved() && mouseOver(moveButtonRect(i))) battleMenuSel = i;
            if (clickedOn(moveButtonRect(i))) { battleMenuSel = i; clickedMove = 1; }
        }

        // ---- CATCH ----
        if ((IsKeyPressed(KEY_C) || clickedOn(catchButtonRect())) && canCatchNow()) {
            consumeInput();
            // Catching costs your whole turn
            if (tryCatch()) {
                Mech caught = enemyBattler.m;
                // Heal caught mech to full
                caught.hp = caught.maxHP;
                caught.exp = 0;
                caught.expToNext = xpNeededForLevel(caught.level);
                addToTeam(&caught);
                caughtThisBattle = 1;
                snprintf(battleLog, sizeof(battleLog),
                    "CAUGHT %s! Added to team (%d/%d).",
                    caught.name, teamSize, MAX_TEAM);
                // End battle with catch - treat as victory
                battleState = 3;
                pendingPlayerMove = -1;
                snprintf(battleLog, sizeof(battleLog),
                    "CAUGHT %s! [Z] to continue.", caught.name);
                return;
            }
            else {
                snprintf(battleLog, sizeof(battleLog),
                    "CATCH FAILED! %s broke free!", enemyBattler.m.name);
                // Enemy gets a turn
                playerEndedTurn = 1;
                battleState = 2;
                battleTimer = 0;
                queueEnemyActions();
                pendingEnemyIdx = 0;
                return;
            }
        }

        if (IsKeyPressed(KEY_X) || clickedOn(endTurnButtonRect())) {
            consumeInput();
            playerEndedTurn = 1; battleState = 2; battleTimer = 0;
            queueEnemyActions(); pendingEnemyIdx = 0;
            snprintf(battleLog, sizeof(battleLog), "Ending turn. Enemy taking action...");
            return;
        }

        if (confirmPressed() || clickedMove) {
            consumeInput();
            int mv = battleMenuSel;
            int cost = playerBattler.moves.moveCost[mv];
            if (playerBattler.moves.pp[mv] <= 0) {
                snprintf(battleLog, sizeof(battleLog),
                    "WEAPON OFFLINE: %s has no ammo!", playerBattler.moves.moveNames[mv]);
                return;
            }
            if (cost > playerEnergy) {
                snprintf(battleLog, sizeof(battleLog),
                    "INSUFFICIENT ENERGY! %s costs %d EN (you have %d).",
                    playerBattler.moves.moveNames[mv], cost, playerEnergy);
                return;
            }
            playerEnergy -= cost;
            playerBattler.moves.pp[mv]--;
            executePlayerMove(mv);
            pendingPlayerMove = (enemyBattler.m.hp <= 0) ? -2 : 1;
        }
    }

    if (pendingPlayerMove == 1) {
        int busy = 0;
        for (int i = 0; i < MAX_EFFECTS; i++) if (effects[i].active) busy = 1;
        if (!busy) {
            pendingPlayerMove = -1;
            int canAct = 0;
            for (int i = 0; i < playerBattler.moves.numMoves; i++)
                if (playerBattler.moves.pp[i] > 0 && playerBattler.moves.moveCost[i] <= playerEnergy) { canAct = 1; break; }
            if (playerEnergy > 0 && canAct) {
                if (playerBattler.moves.moveCost[battleMenuSel] > playerEnergy ||
                    playerBattler.moves.pp[battleMenuSel] <= 0) {
                    for (int i = 0; i < playerBattler.moves.numMoves; i++) {
                        int idx = (battleMenuSel + i) % playerBattler.moves.numMoves;
                        if (playerBattler.moves.moveCost[idx] <= playerEnergy && playerBattler.moves.pp[idx] > 0) {
                            battleMenuSel = idx; break;
                        }
                    }
                }
            }
            else {
                playerEndedTurn = 1; battleState = 2; battleTimer = 0;
                queueEnemyActions(); pendingEnemyIdx = 0;
                snprintf(battleLog, sizeof(battleLog), "REACTOR DEPLETED. Enemy's turn!");
            }
        }
    }

    if (pendingPlayerMove == -2) {
        int busy = 0;
        for (int i = 0; i < MAX_EFFECTS; i++) if (effects[i].active) busy = 1;
        if (!busy) {
            pendingPlayerMove = -1;
            handlePlayerVictoryOverEnemy();
        }
    }

    if (battleState == 2) {
        int busy = 0;
        for (int i = 0; i < MAX_EFFECTS; i++) if (effects[i].active) busy = 1;
        if (!busy) {
            if (pendingEnemyIdx < pendingEnemyMoveCount) {
                executeEnemyMove(pendingEnemyActions[pendingEnemyIdx]);
                pendingEnemyIdx++;
                if (playerBattler.m.hp <= 0) { pendingPlayerMove = -3; return; }
            }
            else {
                if (playerBattler.m.hp <= 0) {
                    battleState = 4;
                    snprintf(battleLog, sizeof(battleLog), "%s DISABLED!", playerBattler.m.name);
                    return;
                }
                playerEnergy = ENERGY_PER_TURN; turnNumber++; playerEndedTurn = 0;
                battleState = 0; battleMenuSel = 0;
                snprintf(battleLog, sizeof(battleLog),
                    "REACTOR RECHARGED: %d EN. Turn %d.", playerEnergy, turnNumber);
            }
        }
    }

    if (pendingPlayerMove == -3) {
        int busy = 0;
        for (int i = 0; i < MAX_EFFECTS; i++) if (effects[i].active) busy = 1;
        if (!busy) {
            pendingPlayerMove = -1; battleState = 4;
            snprintf(battleLog, sizeof(battleLog), "%s DISABLED!", playerBattler.m.name);
        }
    }
}

// ============ TRAINER ENCOUNTER ============
static void triggerTrainerEncounter(int trainerIdx) {
    if (trainerIdx < 0 || trainerIdx >= NUM_TRAINERS) return;
    Trainer* t = &trainers[trainerIdx];
    if (t->defeated) {
        char buf[128];
        snprintf(buf, sizeof(buf), "\"%s\"", t->postLine);
        showMessage(buf, 3.0f);
        return;
    }
    activeTrainerIdx = trainerIdx;
    startTrainerBattle(trainerIdx);
    dialoguePhase = DLG_INTRO;
    snprintf(dialogueText, sizeof(dialogueText), "\"%s\"", t->introLine);
}

// ============ MAIN MENU / SETTINGS ============
#define NUM_MENU_ITEMS 3
#define NUM_SETTINGS_ITEMS 3   // display mode, aspect ratio, back

static Rectangle menuButtonRect(int i) { return (Rectangle) { SCREEN_W / 2 - 140.0f, 290.0f + i * 70, 280, 52 }; }
static Rectangle settingsRowRect(int i) { return (Rectangle) { SCREEN_W / 2 - 250.0f, 190.0f + i * 90, 500, 56 }; }

static void drawTextCentered(const char* text, int y, int size, Color c) {
    DrawText(text, SCREEN_W / 2 - MeasureText(text, size) / 2, y, size, c);
}
static Rectangle settingsArrowRect(int i, int right) {
    Rectangle r = settingsRowRect(i);
    return (Rectangle) { right ? r.x + r.width - 50 : r.x + 230, r.y + 8, 40, 40 };
}

static void drawMenuBackground(void) {
    ClearBackground((Color) { 6, 8, 18, 255 });
    // Scrolling neon grid
    float scroll = fmodf(glowTimer * 30, TILE_SIZE);
    for (int x = 0; x <= screenW / TILE_SIZE + 1; x++)
        DrawLine(x * TILE_SIZE, 0, x * TILE_SIZE, SCREEN_H, (Color) { 30, 50, 90, 90 });
    for (int y = -1; y <= SCREEN_H / TILE_SIZE + 1; y++)
        DrawLine(0, (int)(y * TILE_SIZE + scroll), screenW, (int)(y * TILE_SIZE + scroll), (Color) { 30, 50, 90, 90 });
    int starCount = 80 * screenW / SCREEN_W;
    for (int i = 0; i < starCount; i++) {
        int stx = (i * 137) % screenW;
        int sty = (i * 91) % SCREEN_H;
        int bright = 60 + (int)(80 * (sinf(glowTimer * 2 + i) * 0.5f + 0.5f));
        DrawPixel(stx, sty, (Color) { bright, bright, (unsigned char)(bright + 60), 255 });
    }
}

static void drawMenuTitle(const char* title, const char* subtitle) {
    int tw = MeasureText(title, 56);
    DrawText(title, SCREEN_W / 2 - tw / 2 + 3, 63, 56, (Color) { 20, 60, 120, 255 });
    DrawText(title, SCREEN_W / 2 - tw / 2, 60, 56, (Color) { 120, 230, 255, 255 });
    int sw = MeasureText(subtitle, 20);
    DrawText(subtitle, SCREEN_W / 2 - sw / 2, 124, 20, (Color) { 255, 120, 200, 255 });
}

static void updateMenu(int* state) {
    if (UP_PRESSED)   menuSel = (menuSel + NUM_MENU_ITEMS - 1) % NUM_MENU_ITEMS;
    if (DOWN_PRESSED) menuSel = (menuSel + 1) % NUM_MENU_ITEMS;
    int activate = confirmPressed();
    for (int i = 0; i < NUM_MENU_ITEMS; i++) {
        if (mouseMoved() && mouseOver(menuButtonRect(i))) menuSel = i;
        if (clickedOn(menuButtonRect(i))) { menuSel = i; activate = 1; }
    }
    // ESC resumes a game in progress
    if (IsKeyPressed(KEY_ESCAPE) && gameStarted) { *state = STATE_OVERWORLD; return; }
    if (!activate) return;
    consumeInput();
    if (menuSel == 0) { gameStarted = 1; *state = STATE_OVERWORLD; }
    else if (menuSel == 1) { settingsSel = 0; *state = STATE_SETTINGS; }
    else quitRequested = 1;
}

static void drawMenu(void) {
    drawMenuBackground();
    BeginMode2D(layoutCamera());
    drawMenuTitle("MECH PILOT", "- NEON WASTELAND -");
    drawSpeciesBattle(0, SCREEN_W / 2, 215, 8, 0);
    const char* labels[NUM_MENU_ITEMS] = { gameStarted ? "CONTINUE" : "START", "SETTINGS", "EXIT" };
    for (int i = 0; i < NUM_MENU_ITEMS; i++)
        drawButton(menuButtonRect(i), labels[i], 24, i == menuSel, 1);
    drawTextCentered("[WASD/ARROWS] Navigate   [Z/ENTER/CLICK] Select", SCREEN_H - 30, 16,
        (Color) { 150, 220, 255, 200 });
    EndMode2D();
}

// Change a setting by +1 / -1 and apply it immediately
static void changeSetting(int row, int delta) {
    if (row == 0) settingFullscreen = !settingFullscreen;
    else if (row == 1) {
        if (settingFullscreen) return;   // aspect ratio only applies in windowed mode
        settingAspect = (settingAspect + delta + NUM_ASPECTS) % NUM_ASPECTS;
    }
    else return;
    applyDisplaySettings();
    saveSettings();
}

static void updateSettings(int* state) {
    if (UP_PRESSED)   settingsSel = (settingsSel + NUM_SETTINGS_ITEMS - 1) % NUM_SETTINGS_ITEMS;
    if (DOWN_PRESSED) settingsSel = (settingsSel + 1) % NUM_SETTINGS_ITEMS;
    if (LEFT_PRESSED)  changeSetting(settingsSel, -1);
    if (RIGHT_PRESSED) changeSetting(settingsSel, +1);

    int activate = confirmPressed();
    for (int i = 0; i < NUM_SETTINGS_ITEMS; i++) {
        if (mouseMoved() && mouseOver(settingsRowRect(i))) settingsSel = i;
        if (i < 2 && clickedOn(settingsArrowRect(i, 0))) { settingsSel = i; changeSetting(i, -1); consumeInput(); }
        else if (clickedOn(settingsRowRect(i))) { settingsSel = i; activate = 1; }
    }
    if (IsKeyPressed(KEY_ESCAPE) || (activate && settingsSel == 2)) {
        consumeInput();
        *state = STATE_MENU;
        return;
    }
    if (activate) { consumeInput(); changeSetting(settingsSel, +1); }
}

static void drawSettings(void) {
    drawMenuBackground();
    BeginMode2D(layoutCamera());
    drawMenuTitle("SETTINGS", "- DISPLAY -");

    const char* names[2] = { "DISPLAY MODE", "ASPECT RATIO" };
    const char* values[2] = { settingFullscreen ? "FULLSCREEN" : "WINDOWED", aspects[settingAspect].label };
    for (int i = 0; i < 2; i++) {
        Rectangle r = settingsRowRect(i);
        int enabled = !(i == 1 && settingFullscreen);
        drawButton(r, "", 20, i == settingsSel, enabled);
        Color text = enabled ? (Color) { 220, 240, 255, 255 } : (Color) { 100, 110, 130, 255 };
        DrawText(names[i], (int)r.x + 20, (int)r.y + 18, 20, text);
        drawButton(settingsArrowRect(i, 0), "<", 24, 0, enabled);
        drawButton(settingsArrowRect(i, 1), ">", 24, 0, enabled);
        Rectangle left = settingsArrowRect(i, 0), right = settingsArrowRect(i, 1);
        const char* val = values[i];
        int vw = MeasureText(val, 20);
        int mid = (int)((left.x + left.width + right.x) / 2);
        DrawText(val, mid - vw / 2, (int)r.y + 18, 20, enabled ? (Color) { 120, 240, 255, 255 } : text);
    }
    if (settingFullscreen)
        drawTextCentered("Aspect ratio follows your monitor in fullscreen.", 342, 16, (Color) { 150, 170, 200, 220 });
    drawButton(settingsRowRect(2), "BACK", 22, settingsSel == 2, 1);

    drawTextCentered("[W/S] Select   [A/D] Change   [Z/ENTER/CLICK] Toggle   [ESC] Back", SCREEN_H - 30, 16,
        (Color) { 150, 220, 255, 200 });
    EndMode2D();
}

// ============ MAIN ============
int main(void) {
    InitWindow(SCREEN_W, SCREEN_H, "MECH PILOT - Neon Wasteland");
    SetExitKey(KEY_NULL);   // ESC opens menus; quit via the EXIT button
    SetTargetFPS(60);
    loadSettings();
    applyDisplaySettings();

    genMap();
    initTrainers();
    initPlayerTeam();
    srand((unsigned)GetTime());

    px = 15; py = 16; facing = 0;
    pxF = px * TILE_SIZE; pyF = py * TILE_SIZE;

    int state = STATE_MENU;

    Camera2D camera = { 0 };
    camera.zoom = 1.0f;

    playerMechPos = (Vector2){ 160, 300 };
    enemyMechPos = (Vector2){ 520, 150 };

    while (!WindowShouldClose() && !quitRequested) {
        float dt = GetFrameTime();
        glowTimer += dt;
        inputConsumed = 0;
        updateCanvasTransform();

        // Only the screen that was active at the start of the frame gets updated,
        // so a key press that changes screens isn't handled twice.
        int frameState = state;

        if (frameState == STATE_MENU) {
            updateMenu(&state);
        }
        else if (frameState == STATE_SETTINGS) {
            updateSettings(&state);
        }
        else if (frameState == STATE_OVERWORLD) {
            if (IsKeyPressed(KEY_TAB)) {
                teamSel = activeTeamSlot;
                state = STATE_TEAM;
            }
            else if (IsKeyPressed(KEY_ESCAPE)) {
                menuSel = 0;
                state = STATE_MENU;
            }

            // Advance the current step between tiles
            int arrived = 0;
            float carry = 0;   // leftover step progress, so continuous walking doesn't stutter
            if (moving && state == STATE_OVERWORLD) {
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

            if (state != STATE_OVERWORLD) {
                // switched to the team screen / menu this frame
            }
            else if (arrived && map[py][px] == T_RUINS && rand() % 100 < encounterChance) {
                startWildBattle();
                state = STATE_BATTLE;
            }
            else if (messageTimer > 0) {
                messageTimer -= dt;
                if (confirmPressed() || clickPressed()) { messageTimer = 0; consumeInput(); }
            }
            else if (!moving) {
                if (confirmPressed()) {
                    consumeInput();
                    int fx = px, fy = py;
                    if (facing == 0) fy++;
                    else if (facing == 1) fy--;
                    else if (facing == 2) fx--;
                    else if (facing == 3) fx++;
                    for (int i = 0; i < NUM_TRAINERS; i++) {
                        if (trainers[i].x == fx && trainers[i].y == fy) {
                            triggerTrainerEncounter(i);
                            if (!trainers[i].defeated || dialoguePhase == DLG_INTRO)
                                state = STATE_BATTLE;
                            break;
                        }
                    }
                    if (state == STATE_OVERWORLD && fx >= 0 && fy >= 0 && fx < MAP_W && fy < MAP_H &&
                        map[fy][fx] == T_TERMINAL)
                        showMessage("[TERMINAL] LOG: Catch mechs to grow your team!", 3.5f);
                }

                // Pick a direction: the most recently pressed one wins while it is held
                for (int d = 0; d < 4; d++) if (dirPressed(d)) lastDir = d;
                int dir = -1;
                if (lastDir >= 0 && dirDown(lastDir)) dir = lastDir;
                else for (int d = 0; d < 4; d++) if (dirDown(d)) { dir = d; break; }

                if (dir >= 0 && state == STATE_OVERWORLD && messageTimer <= 0) {
                    // Bumping into things only reacts to a fresh press or walking into them,
                    // not to a key that is still held from before (e.g. after a battle).
                    int fresh = dirPressed(dir) || arrived;
                    int dx = (dir == 3) - (dir == 2);
                    int dy = (dir == 0) - (dir == 1);
                    int nx = px + dx, ny = py + dy;
                    facing = dir;

                    int blocked = 0;
                    for (int i = 0; i < NUM_TRAINERS; i++) {
                        if (trainers[i].x == nx && trainers[i].y == ny) {
                            blocked = 1;
                            if (fresh) {
                                triggerTrainerEncounter(i);
                                if (!trainers[i].defeated || dialoguePhase == DLG_INTRO)
                                    state = STATE_BATTLE;
                            }
                            break;
                        }
                    }
                    if (!blocked) {
                        if (isSolid(nx, ny)) {
                            if (fresh && map[ny][nx] == T_TERMINAL)
                                showMessage("[TERMINAL] LOG: Catch mechs to grow your team!", 3.5f);
                        }
                        else {
                            moveFromX = (float)(px * TILE_SIZE);
                            moveFromY = (float)(py * TILE_SIZE);
                            px = nx; py = ny;
                            moving = 1;
                            moveT = carry;
                            pxF = moveFromX + (px * TILE_SIZE - moveFromX) * moveT;
                            pyF = moveFromY + (py * TILE_SIZE - moveFromY) * moveT;
                        }
                    }
                }
            }
            camera.offset = (Vector2){ screenW / 2.0f, SCREEN_H / 2.0f };
            camera.target = (Vector2){ pxF + TILE_SIZE / 2, pyF + TILE_SIZE / 2 };
        }
        else if (frameState == STATE_BATTLE) {
            updateBattle(dt);

            int cont = confirmPressed() || clickPressed();
            if (battleState == 3 && dialoguePhase == DLG_NONE && cont) {
                consumeInput();
                finishBattleAndReturn();
            }
            if (battleState == 4 && dialoguePhase == DLG_NONE && cont) {
                consumeInput();
                // Heal active mech on defeat
                team[activeTeamSlot].hp = team[activeTeamSlot].maxHP;
                inTrainerBattle = 0;
                activeTrainerIdx = -1;
                state = STATE_OVERWORLD;
            }

            if (pendingPlayerMove == -100) {
                pendingPlayerMove = -1;
                state = STATE_LEVELUP;
            }
            if (pendingPlayerMove == -101) {
                pendingPlayerMove = -1;
                team[activeTeamSlot].hp = playerBattler.m.hp;
                inTrainerBattle = 0;
                activeTrainerIdx = -1;
                state = STATE_OVERWORLD;
            }
        }
        else if (frameState == STATE_LEVELUP) {
            levelUpTimer += dt;
            updateEffects(dt);
            if (confirmPressed() || clickPressed()) {
                consumeInput();
                if (levelUpSubState == 0 && evolvedThisBattle) {
                    levelUpSubState = 1;
                    levelUpTimer = 0;
                }
                else {
                    team[activeTeamSlot].hp = playerBattler.m.hp;
                    inTrainerBattle = 0;
                    activeTrainerIdx = -1;
                    state = STATE_OVERWORLD;
                }
            }
        }
        else if (frameState == STATE_TEAM) {
            if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE) || clickedOn(teamCloseButtonRect())) {
                consumeInput();
                state = STATE_OVERWORLD;
            }
            // Navigate & set active
            else if (teamSize > 0) {
                if (teamSel >= teamSize) teamSel = 0;
                if (RIGHT_PRESSED) teamSel = (teamSel + 1) % teamSize;
                if (LEFT_PRESSED)  teamSel = (teamSel + teamSize - 1) % teamSize;
                if (DOWN_PRESSED)  teamSel = (teamSel + 2) % teamSize;
                if (UP_PRESSED)    teamSel = (teamSel + teamSize - 2) % teamSize;
                int activate = confirmPressed();
                for (int i = 0; i < teamSize; i++) {
                    if (mouseMoved() && mouseOver(teamSlotRect(i))) teamSel = i;
                    if (clickedOn(teamSlotRect(i))) { teamSel = i; activate = 1; }
                }
                if (activate) {
                    consumeInput();
                    activeTeamSlot = teamSel;
                }
            }
        }

        // ===== DRAW =====
        BeginTextureMode(canvas);
        ClearBackground((Color) { 8, 10, 20, 255 });

        if (state == STATE_OVERWORLD) {
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

            // Draw active mech overworld using its species
            drawSpeciesOverworld(team[activeTeamSlot].speciesIdx,
                (int)pxF, (int)pyF, facing, glowTimer);
            EndMode2D();

            // HUD
            int sp = team[activeTeamSlot].speciesIdx;
            DrawRectangle(10, 10, 300, 110, (Color) { 15, 25, 45, 220 });
            DrawRectangleLines(10, 10, 300, 110, species[sp].accent);
            DrawText("PILOT STATUS", 20, 15, 12, species[sp].accent);
            DrawText(TextFormat("%s  LV.%d", team[activeTeamSlot].name, team[activeTeamSlot].level),
                20, 30, 20, WHITE);
            DrawText(species[sp].name, 20, 52, 12, (Color) { 180, 200, 220, 255 });
            drawHPBar(20, 70, 280, 14, team[activeTeamSlot].hp, team[activeTeamSlot].maxHP);
            DrawText(TextFormat("%d/%d HP", team[activeTeamSlot].hp, team[activeTeamSlot].maxHP),
                20, 86, 11, WHITE);
            drawXPBar(20, 102, 280, 8, team[activeTeamSlot].exp, team[activeTeamSlot].expToNext);
            DrawText(TextFormat("XP %d/%d", team[activeTeamSlot].exp, team[activeTeamSlot].expToNext),
                20, 112, 10, (Color) { 200, 170, 255, 255 });

            // Trainer tracker
            int defeated = 0;
            for (int i = 0; i < NUM_TRAINERS; i++) if (trainers[i].defeated) defeated++;
            DrawRectangle(screenW - 200, 10, 190, 60, (Color) { 15, 25, 45, 200 });
            DrawRectangleLines(screenW - 200, 10, 190, 60, (Color) { 255, 200, 100, 180 });
            DrawText("IRON LEGION", screenW - 190, 16, 14, (Color) { 255, 220, 100, 255 });
            DrawText(TextFormat("Defeated: %d / %d", defeated, NUM_TRAINERS),
                screenW - 190, 36, 14, WHITE);
            DrawText(TextFormat("SECTOR %02d-%02d", px, py), screenW - 190, 54, 12,
                (Color) {
                150, 200, 255, 200
            });

            DrawText("[WASD/ARROWS] Move   [Z/ENTER] Talk   [TAB] Team   [ESC] Menu",
                10, SCREEN_H - 28, 16, (Color) { 150, 220, 255, 220 });

            if (messageTimer > 0) {
                DrawRectangle(40, SCREEN_H - 130, screenW - 80, 90, (Color) { 15, 25, 45, 240 });
                DrawRectangleLines(40, SCREEN_H - 130, screenW - 80, 90, (Color) { 80, 220, 255, 220 });
                DrawText(">> COMMS", 55, SCREEN_H - 122, 13, (Color) { 100, 240, 255, 255 });
                DrawText(message, 55, SCREEN_H - 100, 20, (Color) { 200, 240, 255, 255 });
            }
        }
        else if (state == STATE_BATTLE) {
            drawBattle();
        }
        else if (state == STATE_LEVELUP) {
            drawLevelUpScreen();
        }
        else if (state == STATE_TEAM) {
            drawTeamScreen();
        }
        else if (state == STATE_MENU) {
            drawMenu();
        }
        else if (state == STATE_SETTINGS) {
            drawSettings();
        }
        EndTextureMode();

        // Scale the canvas to the window, letterboxed if the aspect ratios differ
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(canvas.texture,
            (Rectangle) { 0, 0, (float)canvas.texture.width, -(float)canvas.texture.height },
            (Rectangle) { canvasOffset.x, canvasOffset.y, screenW * canvasScale, SCREEN_H * canvasScale },
            (Vector2) { 0, 0 }, 0, WHITE);
        EndDrawing();
    }

    UnloadRenderTexture(canvas);
    CloseWindow();
    return 0;
}
