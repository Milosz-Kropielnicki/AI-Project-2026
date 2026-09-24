#ifndef UI_H
#define UI_H

#include "raylib.h"
#include "display.h"

// ============ ui_battle.c ============
void uiBattleOpen(void);
void uiBattleUpdate(float dt, GameState* state);
void uiBattleDraw(void);
void uiRevisionOpen(void);
void uiRevisionUpdate(float dt, GameState* state);
void uiRevisionDraw(void);

// ============ ui_team.c ============
void uiTeamOpen(void);
void uiTeamUpdate(GameState* state);
void uiTeamDraw(void);

// ============ ui_menu.c ============
extern int gameStarted;
extern int quitRequested;
void uiMenuOpen(void);
void uiMenuUpdate(GameState* state);
void uiMenuDraw(void);
void uiSettingsOpen(void);
void uiSettingsUpdate(GameState* state);
void uiSettingsDraw(void);
void drawMenuBackground(void);

// ============ ui_starter.c ============
void uiStarterOpen(void);
void uiStarterUpdate(float dt, GameState* state);
void uiStarterDraw(void);

// ============ ui_debug.c ============
void uiDebugOpen(GameState returnTo);
void uiDebugUpdate(GameState* state);
void uiDebugDraw(void);

// ============ ui_sprites.c ============
void drawMechOverworld(int model, int x, int y, int facing, float t);
void drawMechBattle(int model, int cx, int cy, int scale, int isEnemy);

#endif