#include "raylib.h"
#include "display.h"
#include "world.h"
#include "battle.h"
#include "mech.h"
#include "firmware.h"
#include "ui.h"
#include "game.h"
#include <stdlib.h>

// Called once when the game switches screens. Returning from the debug
// screen resumes the previous screen as it was.
static void enterState(GameState to, GameState from) {
    if (from == STATE_DEBUG) return;
    switch (to) {
    case STATE_MENU:     if (from == STATE_OVERWORLD) uiMenuOpen(); break;
    case STATE_SETTINGS: uiSettingsOpen(); break;
    case STATE_TEAM:     uiTeamOpen(); break;
    case STATE_BATTLE:   uiBattleOpen(); break;
    case STATE_REVISION: uiRevisionOpen(); break;
    case STATE_STARTER:  uiStarterOpen(); break;
    case STATE_DEBUG:    uiDebugOpen(from); break;
    case STATE_TERMINAL: uiTerminalOpen(); break;
    case STATE_OVERWORLD:   // back from a fight: starter evolution and bonus starters
        if (from == STATE_BATTLE || from == STATE_REVISION) {
            worldTryStarterEvolution();
            worldOfferAlternateStarters();
        }
        break;
    default: break;
    }
}

static int debugAllowedFrom(GameState s) {
    return s == STATE_OVERWORLD || s == STATE_BATTLE || s == STATE_TEAM;
}

int main(void) {
    InitWindow(SCREEN_W, SCREEN_H, "MECH PILOT - Neon Wasteland");
    SetExitKey(KEY_NULL);   // ESC opens menus; quit via the EXIT button
    SetTargetFPS(60);
    displayInit();

    gameNew();              // a fresh campaign; the menu can load a save over it

    GameState state = STATE_MENU;

    while (!WindowShouldClose() && !quitRequested) {
        float dt = GetFrameTime();
        glowTimer += dt;
        inputBeginFrame();
        updateCanvasTransform();

        // Only the screen that was active at the start of the frame gets updated,
        // so a key press that changes screens isn't handled twice.
        GameState frameState = state;
        if (IsKeyPressed(KEY_F1) && debugAllowedFrom(frameState)) {
            consumeInput();
            state = STATE_DEBUG;
        }
        else {
            switch (frameState) {
            case STATE_MENU:      uiMenuUpdate(&state); break;
            case STATE_SETTINGS:  uiSettingsUpdate(&state); break;
            case STATE_OVERWORLD: worldUpdate(dt, &state); break;
            case STATE_BATTLE:    uiBattleUpdate(dt, &state); break;
            case STATE_REVISION:  uiRevisionUpdate(dt, &state); break;
            case STATE_TEAM:      uiTeamUpdate(&state); break;
            case STATE_DEBUG:     uiDebugUpdate(&state); break;
            case STATE_TERMINAL:  uiTerminalUpdate(&state); break;
            case STATE_STARTER:   uiStarterUpdate(dt, &state); break;
            }
        }
        if (state != frameState) enterState(state, frameState);

        beginCanvas();
        switch (state) {
        case STATE_MENU:      uiMenuDraw(); break;
        case STATE_SETTINGS:  uiSettingsDraw(); break;
        case STATE_OVERWORLD: worldDraw(); break;
        case STATE_BATTLE:    uiBattleDraw(); break;
        case STATE_REVISION:  uiRevisionDraw(); break;
        case STATE_TEAM:      uiTeamDraw(); break;
        case STATE_DEBUG:     uiDebugDraw(); break;
        case STATE_TERMINAL:  uiTerminalDraw(); break;
        case STATE_STARTER:   uiStarterDraw(); break;
        }
        presentCanvas();
    }

    displayShutdown();
    CloseWindow();
    return 0;
}
