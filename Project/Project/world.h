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
    Color tint;                    // ambient tint for tiles
    int baseEncounter;             // base encounter chance per ruins tile
    int ruinsCount;                // how many ruins clusters
    int gateWest, gateEast;        // -1 = no gate; else zone index
} Zone;

extern const Zone zones[NUM_ZONES];

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
    int faction;
    int x, y;
    int zone;                      // which zone they live in
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