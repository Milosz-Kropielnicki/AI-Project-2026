#ifndef TRANSITION_H
#define TRANSITION_H

#include "raylib.h"

// A single wipe used to bridge the overworld and the battle screen. The
// world side closes it (phases 1-2); the battle side opens it (phase 3) so
// the reveal is drawn over the fight, not over the world.
//
// Phases:
//   0  idle
//   1  bubble (world side): trainer line is up, world frozen
//   2  close  (world side): panels slide in from the edges and hold closed
//   3  open   (battle side): panels slide back out, revealing the battle
typedef struct {
    int active;         // 1 while any phase is running
    int phase;          // 0 idle, 2 close, 3 open (1 is unused, kept for clarity)
    float t;            // seconds in the current phase
    float cover;        // 0 = fully open, 1 = fully closed
    Color accent;       // trainer's colour, for the panel edges and title
    char title[32];     // trainer's name, shown while the panels are closed
} Transition;

#define TRANSITION_CLOSE_TIME 0.7f    // panels slide in
#define TRANSITION_HOLD_TIME  0.25f   // panels sit closed
#define TRANSITION_OPEN_TIME  0.45f   // panels slide back out over the battle

extern Transition transition;

void transitionReset(void);
void transitionStartClose(const char* title, Color accent);   // world -> closed
int  transitionUpdate(float dt);                              // returns 1 the frame a phase ends
void transitionDraw(void);                                    // draws panels, full canvas

#endif