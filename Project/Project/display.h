#ifndef DISPLAY_H
#define DISPLAY_H

#include "raylib.h"

#define SCREEN_W 800
#define SCREEN_H 600
#define TILE_SIZE 40

typedef enum {
    STATE_OVERWORLD,
    STATE_BATTLE,
    STATE_REVISION,
    STATE_TEAM,
    STATE_MENU,
    STATE_SETTINGS,
    STATE_DEBUG,
    STATE_STARTER
} GameState;

extern int screenW;
extern float glowTimer;

#define NUM_ASPECTS 4
extern int settingFullscreen;
extern int settingAspect;

void displayInit(void);
void displayShutdown(void);
void updateCanvasTransform(void);
void beginCanvas(void);
void presentCanvas(void);
const char* aspectLabel(int i);
void changeDisplaySetting(int row, int delta);

float layoutX(void);
Camera2D layoutCamera(void);

void inputBeginFrame(void);
void consumeInput(void);
int confirmPressed(void);
int clickPressed(void);
Vector2 layoutMouse(void);
int mouseOver(Rectangle r);
int mouseMoved(void);
int clickedOn(Rectangle r);

int dirDown(int d);
int dirPressed(int d);
#define DOWN_PRESSED  dirPressed(0)
#define UP_PRESSED    dirPressed(1)
#define LEFT_PRESSED  dirPressed(2)
#define RIGHT_PRESSED dirPressed(3)

void drawButton(Rectangle r, const char* label, int fontSize, int selected, int enabled);
void drawTextCentered(const char* text, int y, int size, Color c);
void drawIntegrityBar(int x, int y, int w, int h, int value, int max);
void drawArmorBar(int x, int y, int w, int h, int value, int max);
void drawHeatBar(int x, int y, int w, int h, int value, int max);
void drawDataBar(int x, int y, int w, int h, int value, int max);

#endif