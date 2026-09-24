#ifndef WORLD_H
#define WORLD_H

#include "raylib.h"
#include "display.h"
#include "mech.h"

#define MAP_W 40
#define MAP_H 30
#define MOVE_TIME 0.14f   // seconds to walk one tile

enum { T_GRID, T_RUINS, T_BLOCK, T_PLASMA, T_PAD, T_BUNKER, T_TERMINAL, T_GATE };

// ============ ZONES ============
#define NUM_ZONES 3
enum { ZONE_ALPHA, ZONE_BETA, ZONE_GAMMA };

typedef struct {
    const char* name;
    const char* subtitle;
    Color tint;
    int baseEncounter;
    int ruinsCount;
    int gateWest, gateEast;
} Zone;

extern const Zone zones[NUM_ZONES];

// ============ STARTERS ============
#define NUM_STARTERS 3
#define NUM_STARTER_STAGES 3

typedef struct {
    const char* name;
    const char* tagline;
    int stages[NUM_STARTER_STAGES];
    int evolveLevel[NUM_STARTER_STAGES];
    Color accent;
    const char* desc;
} StarterLine;

extern const StarterLine starters[NUM_STARTERS];
extern int playerStarter;
extern int starterStage;
extern int starterSlot;
extern int obtainedStarters;

// ============ TRAINERS ============
typedef struct {
    char name[32];
    char team[32];
    int x, y;
    int zone;
    int facing;
    Color color;
    const char* introLine;
    const char* defeatLine;
    const char* postLine;
    int tier;
    int defeated;
    int teamArchetypes[MAX_TEAM];
    int teamRevisions[MAX_TEAM];
    int numMechs;
    int numDefeated;
} Trainer;

#define NUM_TRAINERS 7                 // 2 per zone, plus the Gamma boss
extern Trainer trainers[NUM_TRAINERS];    // <-- THIS is what battle.c needs

void worldInit(void);
void worldInitNewGame(int starterIdx);
void worldUpdate(float dt, GameState* state);
void worldDraw(void);
void showMessage(const char* msg, float dur);
int worldCurrentZone(void);
const char* worldCurrentZoneName(void);
const char* worldCurrentZoneSubtitle(void);
int worldTryStarterEvolution(void);
void worldOfferAlternateStarters(void);
int worldStarterSlot(void);

#endif