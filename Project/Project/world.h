#ifndef WORLD_H
#define WORLD_H

#include "raylib.h"
#include "display.h"
#include "mech.h"

#define MAP_W 40
#define MAP_H 30
#define MOVE_TIME 0.14f   // seconds to walk one tile

enum { T_GRID, T_RUINS, T_BLOCK, T_PLASMA, T_PAD, T_BUNKER, T_TERMINAL };

// ============ TRAINERS ============
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
    int teamArchetypes[MAX_TEAM];  // index into archetypes[]
    int teamRevisions[MAX_TEAM];   // firmware revision step
    int numMechs;
    int numDefeated;
} Trainer;

#define NUM_TRAINERS 4           // the last one is the boss
extern Trainer trainers[NUM_TRAINERS];

void worldInit(void);
void worldUpdate(float dt, GameState* state);
void worldDraw(void);
void showMessage(const char* msg, float dur);

#endif
