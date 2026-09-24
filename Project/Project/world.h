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

// ============ TRAINERS ============
typedef struct {
    char name[32];
    char team[32];
    int x, y;
    int zone;                      // which zone they live in
    int facing;
    Color color;
    const char* introLine;
    const char* defeatLine;
    const char* postLine;
    int tier;
    int defeated;
    int teamModels[MAX_TEAM];
    int teamRevisions[MAX_TEAM];
    int numMechs;
    int numDefeated;
} Trainer;

#define NUM_TRAINERS 6                 // 2 per zone
extern Trainer trainers[NUM_TRAINERS];

void worldInit(void);
void worldUpdate(float dt, GameState* state);
void worldDraw(void);
void showMessage(const char* msg, float dur);
int worldCurrentZone(void);
const char* worldCurrentZoneName(void);
const char* worldCurrentZoneSubtitle(void);

#endif