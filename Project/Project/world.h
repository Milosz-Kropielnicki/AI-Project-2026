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
    int baseEncounter;           // % chance per step on an encounter tile
    int minX, minY, maxX, maxY;  // bounding box used for lookups
} Region;

extern const Region regions[NUM_REGIONS];
int worldRegionAt(int tileX, int tileY);   // -1 if outside all regions

// ============ STARTERS ============
// New-game starter lines. The starter evolves into the next chassis when its
// firmware reaches evolveLevel (a revision step).
#define NUM_STARTERS 3
#define NUM_STARTER_STAGES 3

typedef struct {
    const char* name;
    const char* tagline;
    int stages[NUM_STARTER_STAGES];         // chassis per stage
    int evolveLevel[NUM_STARTER_STAGES];    // firmware revision step to reach each stage
    Color accent;
    const char* desc;
} StarterLine;

extern const StarterLine starters[NUM_STARTERS];
extern int playerStarter;       // which starter the player picked (-1 = none)
extern int starterStage;        // current evolution stage of the starter
extern int starterSlot;         // which team slot the starter lives in
extern int obtainedStarters;    // bitmask of starters acquired

// ============ ENCOUNTERS ============
// Faction encounters (a squad from a criminal org or rogue AI). Still called
// Trainer in code; faction indexes factions[] in game.h.
typedef struct {
    char name[32];
    int faction;                   // FAC_* from game.h
    int x, y;                      // world tile coords
    int facing;
    Color color;
    const char* introLine;
    const char* defeatLine;
    const char* postLine;
    int tier;
    int defeated;
    int teamArchetypes[MAX_TEAM];  // index into archetypes[]
    int teamRevisions[MAX_TEAM];   // firmware revision step
    int numMechs;
    int numDefeated;
} Trainer;

#define NUM_TRAINERS 7                 // 2 per zone, plus the Gamma boss
extern Trainer trainers[NUM_TRAINERS];

void worldInit(void);
void worldInitNewGame(int starterIdx);         // team = the chosen starter
void worldUpdate(float dt, GameState* state);
void worldDraw(void);
void showMessage(const char* msg, float dur);
int worldCurrentZone(void);
const char* worldCurrentZoneName(void);
const char* worldCurrentZoneSubtitle(void);
void worldGetPlayer(int* zone, int* x, int* y);
void worldSetPlayer(int zone, int x, int y);   // used by save/load
int worldFindTrainer(const char* name);        // -1 if none
int worldTryStarterEvolution(void);            // 1 if the starter evolved
void worldOfferAlternateStarters(void);        // grants unpicked starters after enough encounters
int worldStarterSlot(void);

#endif