#include "display.h"
#include <stdio.h>
#include <math.h>

#define SETTINGS_FILE "settings.cfg"

int screenW = SCREEN_W;
float glowTimer = 0;

// ============ CANVAS / SETTINGS ============
// Everything is drawn to an offscreen canvas SCREEN_H pixels tall whose width follows
// the aspect ratio, then scaled to the window. Fixed-layout screens are centered in it.
typedef struct { const char* label; int aw, ah, winW, winH; } AspectPreset;
static AspectPreset aspects[NUM_ASPECTS] = {
    { "4:3",    4,  3,  800, 600 },
    { "16:9",  16,  9, 1280, 720 },
    { "16:10", 16, 10, 1280, 800 },
    { "21:9",  21,  9, 1680, 720 },
};
int settingFullscreen = 0;
int settingAspect = 0;
static RenderTexture2D canvas;
static float canvasScale = 1.0f;
static Vector2 canvasOffset;

float layoutX(void) { return (float)((screenW - SCREEN_W) / 2); }

Camera2D layoutCamera(void) {
    Camera2D c = { 0 };
    c.offset = (Vector2){ layoutX(), 0 };
    c.zoom = 1.0f;
    return c;
}

const char* aspectLabel(int i) { return aspects[i].label; }

void updateCanvasTransform(void) {
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    canvasScale = fminf(sw / screenW, sh / SCREEN_H);
    canvasOffset = (Vector2){ (sw - screenW * canvasScale) / 2, (sh - SCREEN_H * canvasScale) / 2 };
}

static void applyDisplaySettings(void) {
    int mon = GetCurrentMonitor();
    int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    Vector2 mpos = GetMonitorPosition(mon);
    int borderless = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    int newW;
    if (settingFullscreen) {
        if (!borderless) ToggleBorderlessWindowed();
        newW = SCREEN_H * mw / mh;
    }
    else {
        if (borderless) ToggleBorderlessWindowed();
        AspectPreset* a = &aspects[settingAspect];
        int w = a->winW, h = a->winH;
        // Shrink the window if it would not fit on this monitor
        if (w > mw * 9 / 10 || h > mh * 9 / 10) {
            float s = fminf(mw * 0.9f / w, mh * 0.9f / h);
            w = (int)(w * s); h = (int)(h * s);
        }
        SetWindowSize(w, h);
        SetWindowPosition((int)mpos.x + (mw - w) / 2, (int)mpos.y + (mh - h) / 2);
        newW = SCREEN_H * a->aw / a->ah;
    }
    if (canvas.id == 0 || newW != screenW) {
        if (canvas.id != 0) UnloadRenderTexture(canvas);
        screenW = newW;
        canvas = LoadRenderTexture(screenW, SCREEN_H);
        SetTextureFilter(canvas.texture, TEXTURE_FILTER_BILINEAR);
    }
    updateCanvasTransform();
}

static void loadSettings(void) {
    FILE* f = fopen(SETTINGS_FILE, "r");
    if (!f) return;
    int fs = 0, asp = 0;
    if (fscanf(f, "fullscreen=%d aspect=%d", &fs, &asp) == 2) {
        settingFullscreen = fs ? 1 : 0;
        if (asp >= 0 && asp < NUM_ASPECTS) settingAspect = asp;
    }
    fclose(f);
}

static void saveSettings(void) {
    FILE* f = fopen(SETTINGS_FILE, "w");
    if (!f) return;
    fprintf(f, "fullscreen=%d\naspect=%d\n", settingFullscreen, settingAspect);
    fclose(f);
}

void displayInit(void) {
    loadSettings();
    applyDisplaySettings();
}

void displayShutdown(void) {
    if (canvas.id != 0) UnloadRenderTexture(canvas);
}

void beginCanvas(void) {
    BeginTextureMode(canvas);
    ClearBackground((Color) { 8, 10, 20, 255 });
}

void presentCanvas(void) {
    EndTextureMode();
    // Scale the canvas to the window, letterboxed if the aspect ratios differ
    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(canvas.texture,
        (Rectangle) { 0, 0, (float)canvas.texture.width, -(float)canvas.texture.height },
        (Rectangle) { canvasOffset.x, canvasOffset.y, screenW * canvasScale, SCREEN_H * canvasScale },
        (Vector2) { 0, 0 }, 0, WHITE);
    EndDrawing();
}

// Change a setting by +1 / -1 and apply it immediately
void changeDisplaySetting(int row, int delta) {
    if (row == 0) settingFullscreen = !settingFullscreen;
    else if (row == 1) {
        if (settingFullscreen) return;   // aspect ratio only applies in windowed mode
        settingAspect = (settingAspect + delta + NUM_ASPECTS) % NUM_ASPECTS;
    }
    else return;
    applyDisplaySettings();
    saveSettings();
}

// ============ INPUT ============
static int inputConsumed = 0;

void inputBeginFrame(void) { inputConsumed = 0; }
void consumeInput(void) { inputConsumed = 1; }

int confirmPressed(void) {
    return !inputConsumed && (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER));
}
int clickPressed(void) { return !inputConsumed && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

Vector2 layoutMouse(void) {
    Vector2 m = GetMousePosition();
    return (Vector2) { (m.x - canvasOffset.x) / canvasScale - layoutX(), (m.y - canvasOffset.y) / canvasScale };
}
int mouseOver(Rectangle r) { return CheckCollisionPointRec(layoutMouse(), r); }
int mouseMoved(void) { Vector2 d = GetMouseDelta(); return d.x != 0 || d.y != 0; }
int clickedOn(Rectangle r) { return clickPressed() && mouseOver(r); }

int dirDown(int d) {
    switch (d) {
    case 0: return IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S);
    case 1: return IsKeyDown(KEY_UP) || IsKeyDown(KEY_W);
    case 2: return IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A);
    default: return IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
    }
}
int dirPressed(int d) {
    switch (d) {
    case 0: return IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
    case 1: return IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W);
    case 2: return IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A);
    default: return IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D);
    }
}

// ============ WIDGETS ============
void drawButton(Rectangle r, const char* label, int fontSize, int selected, int enabled) {
    int hot = selected || mouseOver(r);
    Color edge = !enabled ? (Color) { 70, 80, 100, 255 }
        : hot ? (Color) { 120, 240, 255, 255 } : (Color) { 60, 120, 180, 220 };
    Color fill = hot && enabled ? (Color) { 40, 90, 130, 200 } : (Color) { 15, 25, 45, 220 };
    Color text = enabled ? (hot ? WHITE : (Color) { 190, 220, 240, 255 }) : (Color) { 100, 110, 130, 255 };
    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, hot ? 2.0f : 1.0f, edge);
    int tw = MeasureText(label, fontSize);
    DrawText(label, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - fontSize / 2), fontSize, text);
}

void drawTextCentered(const char* text, int y, int size, Color c) {
    DrawText(text, SCREEN_W / 2 - MeasureText(text, size) / 2, y, size, c);
}

static void drawBar(int x, int y, int w, int h, int value, int max, Color fill, Color edge, Color back) {
    DrawRectangle(x, y, w, h, back);
    float pct = max > 0 ? (float)value / max : 0;
    if (pct < 0) pct = 0;
    if (pct > 1) pct = 1;
    DrawRectangle(x + 1, y + 1, (int)((w - 2) * pct), h - 2, fill);
    DrawRectangleLines(x, y, w, h, edge);
}

void drawIntegrityBar(int x, int y, int w, int h, int value, int max) {
    float pct = max > 0 ? (float)value / max : 0;
    Color c = pct > 0.5f ? (Color) { 80, 240, 160, 255 }
        : (pct > 0.2f ? (Color) { 240, 220, 80, 255 } : (Color) { 255, 80, 100, 255 });
    drawBar(x, y, w, h, value, max, c, (Color) { 80, 220, 255, 200 }, (Color) { 20, 20, 30, 255 });
}

void drawArmorBar(int x, int y, int w, int h, int value, int max) {
    drawBar(x, y, w, h, value, max, (Color) { 120, 170, 230, 255 }, (Color) { 150, 190, 240, 200 }, (Color) { 20, 22, 35, 255 });
}

void drawHeatBar(int x, int y, int w, int h, int value, int max) {
    float pct = max > 0 ? (float)value / max : 0;
    Color c = pct > 0.8f ? (Color) { 255, 70, 50, 255 } : (Color) { 255, 150, 60, 255 };
    drawBar(x, y, w, h, value, max, c, (Color) { 255, 170, 90, 200 }, (Color) { 30, 18, 15, 255 });
}

void drawDataBar(int x, int y, int w, int h, int value, int max) {
    drawBar(x, y, w, h, value, max, (Color) { 180, 130, 255, 255 }, (Color) { 180, 140, 255, 200 }, (Color) { 20, 20, 40, 255 });
}
