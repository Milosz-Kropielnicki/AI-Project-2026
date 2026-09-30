#ifndef WORLD_H
#define WORLD_H

#include "raylib.h"
#include "display.h"
#include "mech.h"

// One big seamless world. Regions are a *classification of tiles*, not
// separate maps. The player can walk anywhere the tiles allow.
#define MAP_W 120
#define MAP_H 60
#define MOVE_TIME 0.14f   // seconds to walk one tile

enum { T_GRID, T_RUINS, T_BLOCK, T_PLASMA, T_PAD, T_BUNKER, T_TERMINAL, T_GRASS };

// ============ REGIONS ============
// Regions are contiguous areas of the world with their own tint, encounter
// rate, and difficulty. They are laid out by the map generator using simple
// rectangles; there is no concept of a "zone transition" anymore.
#define NUM_REGIONS 3
enum { REGION_ALPHA, REGION_BETA, REGION_GAMMA };

typedef struct {
    const char* name;
    const char* subtitle;
    Color tint;
    int baseEncounter;   // % chance per step on an encounter tile
    int minX, minY, maxX, maxY;  // bounding box used for lookups
} Region;

extern const Region regions[NUM_REGIONS];
int worldRegionAt(int tileX, int tileY);   // -1 if outside all regions

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
// Position is now a world tile coordinate in the big map.
typedef struct {
    char name[32];
    char team[32];
    int x, y;                // world tile coords
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

#define NUM_TRAINERS 7
extern Trainer trainers[NUM_TRAINERS];

void worldInit(void);
void worldInitNewGame(int starterIdx);
void worldUpdate(float dt, GameState* state);
void worldDraw(void);
void showMessage(const char* msg, float dur);
int worldTryStarterEvolution(void);
void worldOfferAlternateStarters(void);
int worldStarterSlot(void);

#endif