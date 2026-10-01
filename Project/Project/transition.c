#include "transition.h"
#include "display.h"
#include <string.h>
#include <stdio.h>

Transition transition;

void transitionReset(void) {
    memset(&transition, 0, sizeof(transition));
}

void transitionStartClose(const char* title, Color accent) {
    transition.active = 1;
    transition.phase = 2;           // straight into the close
    transition.t = 0;
    transition.cover = 0;
    transition.accent = accent;
    snprintf(transition.title, sizeof(transition.title), "%s", title ? title : "");
}

// Returns 1 on the frame a phase transitions or finishes. Callers switch
// states on that frame.
int transitionUpdate(float dt) {
    if (!transition.active) return 0;
    transition.t += dt;
    int done = 0;

    switch (transition.phase) {
    case 2: {   // close, then hold closed
        float p = transition.t / TRANSITION_CLOSE_TIME;
        if (p < 1.0f) transition.cover = p;
        else {
            transition.cover = 1.0f;
            if (transition.t >= TRANSITION_CLOSE_TIME + TRANSITION_HOLD_TIME) done = 1;
        }
        break;
    }
    case 3: {   // open, drawn over the battle
        float p = transition.t / TRANSITION_OPEN_TIME;
        if (p < 1.0f) transition.cover = 1.0f - p;
        else {
            transition.cover = 0.0f;
            transition.active = 0;
            transition.phase = 0;
            done = 1;
        }
        break;
    }
    default: break;
    }
    return done;
}

// Full-canvas wipe: the panels slide in from the true left and right edges of
// the canvas (not the 800px fixed-layout bounds) so the whole screen is
// covered, letterbox margins included. The title stays centred on the canvas,
// which is also the fixed-layout centre. Drawn OUTSIDE BeginMode2D so it
// uses raw canvas coordinates.
void transitionDraw(void) {
    if (!transition.active || transition.cover <= 0) return;

    int halfW = screenW / 2;
    int closed = (int)(halfW * transition.cover);
    if (closed <= 0) return;

    unsigned char alpha = transition.cover < 0.999f ? 235 : 255;
    Color bar = (Color){ 8, 14, 28, alpha };
    DrawRectangle(0, 0, closed, SCREEN_H, bar);
    DrawRectangle(screenW - closed, 0, closed, SCREEN_H, bar);

    Color accent = transition.accent;
    DrawRectangle(closed - 3, 0, 3, SCREEN_H, accent);
    DrawRectangle(screenW - closed, 0, 3, SCREEN_H, accent);

    if (transition.cover >= 0.9f && transition.title[0]) {
        int nw = MeasureText(transition.title, 40);
        DrawText(transition.title, screenW / 2 - nw / 2, SCREEN_H / 2 - 40, 40, accent);
        const char* tag = ">> ENGAGING <<";
        int tw = MeasureText(tag, 20);
        DrawText(tag, screenW / 2 - tw / 2, SCREEN_H / 2 + 10, 20, (Color) { 255, 220, 100, 255 });
        DrawRectangle(screenW / 2 - 200, SCREEN_H / 2 + 40, 400, 2, accent);
    }
}