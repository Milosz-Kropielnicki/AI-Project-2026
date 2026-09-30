#ifndef DISPLAY_H
#define DISPLAY_H

#include "raylib.h"

// ============ CONFIG ============
// SCREEN_W is the width of the fixed-layout screens (battle, team, menus).
// The actual canvas is SCREEN_H tall and screenW wide, depending on aspect ratio.
#define SCREEN_W 800
#define SCREEN_H 600
#define TILE_SIZE 40

// ============ SCREENS ============
typedef enum {
    STATE_OVERWORLD,
    STATE_BATTLE,
    STATE_REVISION,   // post-battle firmware revision screen
    STATE_TEAM,
    STATE_MENU,
    STATE_SETTINGS,
    STATE_DEBUG,
    STATE_TERMINAL,   // mission board, showroom, parts market, services
    STATE_STARTER     // new game: pick a starter mech
} GameState;

extern int screenW;       // current canvas width
extern float glowTimer;   // global animation clock

// ============ CANVAS / SETTINGS ============
#define NUM_ASPECTS 4
extern int settingFullscreen;
extern int settingAspect;

void displayInit(void);        // load settings.cfg and create the canvas
void displayShutdown(void);
void updateCanvasTransform(void);
void beginCanvas(void);        // start drawing to the offscreen canvas
void presentCanvas(void);      // scale the canvas to the window
const char* aspectLabel(int i);
void changeDisplaySetting(int row, int delta);   // row 0 = mode, 1 = aspect

// Horizontal offset / camera that centers a SCREEN_W-wide layout on the canvas
float layoutX(void);
Camera2D layoutCamera(void);

// ============ INPUT ============
// Once an input has triggered something this frame, it is consumed so the same
// key press / click can't also trigger whatever screen comes next.
void inputBeginFrame(void);
void consumeInput(void);
int confirmPressed(void);
int clickPressed(void);
Vector2 layoutMouse(void);   // mouse in fixed-layout (SCREEN_W x SCREEN_H) coordinates
int mouseOver(Rectangle r);
int mouseMoved(void);
int clickedOn(Rectangle r);

// Directions use the facing convention: 0 = down, 1 = up, 2 = left, 3 = right
int dirDown(int d);
int dirPressed(int d);
#define DOWN_PRESSED  dirPressed(0)
#define UP_PRESSED    dirPressed(1)
#define LEFT_PRESSED  dirPressed(2)
#define RIGHT_PRESSED dirPressed(3)

// ============ WIDGETS ============
void drawButton(Rectangle r, const char* label, int fontSize, int selected, int enabled);
void drawTextCentered(const char* text, int y, int size, Color c);
void drawIntegrityBar(int x, int y, int w, int h, int value, int max);
void drawArmorBar(int x, int y, int w, int h, int value, int max);
void drawHeatBar(int x, int y, int w, int h, int value, int max);
void drawDataBar(int x, int y, int w, int h, int value, int max);

#endif
