#ifndef WORLD_H
#define WORLD_H

#include "raylib.h"
#include "display.h"
#include "mech.h"

// One big seamless world. Regions are a *classification of tiles*, not
// separate maps. The player can walk anywhere the tiles allow.
#define MAP_W 120
#define MAP_H 60
#define MOVE_TIME 0.14f

enum { T_GRID, T_RUINS, T_BLOCK, T_PLASMA, T_PAD, T_BUNKER, T_TERMINAL, T_GRASS };

// ============ REGIONS ============
// Five hubs of varying size, connected by narrow route corridors. The chain
// goes Alpha -> Beta -> Gamma -> Delta -> Omega, with a long loop route from
// Gamma back to Omega for players who want to skip Delta.
#define NUM_REGIONS 5
enum { REGION_ALPHA, REGION_BETA, REGION_GAMMA, REGION_DELTA, REGION_OMEGA };

typedef struct {
    const char* name;
    const char* subtitle;
    Color tint;
    int baseEncounter;
    int minX, minY, maxX, maxY;
} Region;

extern const Region regions[NUM_REGIONS];
int worldRegionAt(int tileX, int tileY);

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

// ============ ENCOUNTERS ============
typedef struct {
    char name[32];
    int faction;
    int x, y;
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

#define NUM_TRAINERS 16
extern Trainer trainers[NUM_TRAINERS];

void worldInit(void);
void worldInitNewGame(int starterIdx);
void worldUpdate(float dt, GameState* state);
void worldDraw(void);
void showMessage(const char* msg, float dur);
int worldCurrentZone(void);
const char* worldCurrentZoneName(void);
const char* worldCurrentZoneSubtitle(void);
void worldGetPlayer(int* zone, int* x, int* y);
void worldSetPlayer(int zone, int x, int y);
int worldFindTrainer(const char* name);
int worldTryStarterEvolution(void);
void worldOfferAlternateStarters(void);
int worldStarterSlot(void);

// ============ MAP SCREEN ============
void worldDrawMinimap(int x, int y, int w, int h);

#endif