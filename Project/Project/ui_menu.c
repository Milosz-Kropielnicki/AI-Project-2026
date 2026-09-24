#include "ui.h"
#include "mech.h"
#include "battle.h"
#include "world.h"
#include "game.h"
#include <math.h>

#define NUM_MENU_ITEMS 5   // new game / resume, load, test range, settings, exit
#define NUM_SETTINGS_ITEMS 3   // display mode, aspect ratio, back

int gameStarted = 0;
int quitRequested = 0;
static int menuSel = 0, settingsSel = 0;
static int saveExists = 0;
static float menuNoticeTimer = 0;
static const char* menuNotice = "";

static Rectangle menuButtonRect(int i) { return (Rectangle) { SCREEN_W / 2 - 140.0f, 280.0f + i * 54, 280, 44 }; }
static Rectangle settingsRowRect(int i) { return (Rectangle) { SCREEN_W / 2 - 250.0f, 190.0f + i * 90, 500, 56 }; }
static Rectangle settingsArrowRect(int i, int right) {
    Rectangle r = settingsRowRect(i);
    return (Rectangle) { right ? r.x + r.width - 50 : r.x + 230, r.y + 8, 40, 40 };
}

void drawMenuBackground(void) {
    ClearBackground((Color) { 6, 8, 18, 255 });
    // Scrolling neon grid
    float scroll = fmodf(glowTimer * 30, TILE_SIZE);
    for (int x = 0; x <= screenW / TILE_SIZE + 1; x++)
        DrawLine(x * TILE_SIZE, 0, x * TILE_SIZE, SCREEN_H, (Color) { 30, 50, 90, 90 });
    for (int y = -1; y <= SCREEN_H / TILE_SIZE + 1; y++)
        DrawLine(0, (int)(y * TILE_SIZE + scroll), screenW, (int)(y * TILE_SIZE + scroll), (Color) { 30, 50, 90, 90 });
    int starCount = 80 * screenW / SCREEN_W;
    for (int i = 0; i < starCount; i++) {
        int stx = (i * 137) % screenW;
        int sty = (i * 91) % SCREEN_H;
        int bright = 60 + (int)(80 * (sinf(glowTimer * 2 + i) * 0.5f + 0.5f));
        DrawPixel(stx, sty, (Color) { (unsigned char)bright, (unsigned char)bright, (unsigned char)(bright + 60), 255 });
    }
}

static void drawMenuTitle(const char* title, const char* subtitle) {
    int tw = MeasureText(title, 56);
    DrawText(title, SCREEN_W / 2 - tw / 2 + 3, 63, 56, (Color) { 20, 60, 120, 255 });
    DrawText(title, SCREEN_W / 2 - tw / 2, 60, 56, (Color) { 120, 230, 255, 255 });
    int sw = MeasureText(subtitle, 20);
    DrawText(subtitle, SCREEN_W / 2 - sw / 2, 124, 20, (Color) { 255, 120, 200, 255 });
}

// ============ MAIN MENU ============
void uiMenuOpen(void) {
    menuSel = 0;
    saveExists = gameSaveExists();
}

void uiMenuUpdate(GameState* state) {
    if (UP_PRESSED)   menuSel = (menuSel + NUM_MENU_ITEMS - 1) % NUM_MENU_ITEMS;
    if (DOWN_PRESSED) menuSel = (menuSel + 1) % NUM_MENU_ITEMS;
    int activate = confirmPressed();
    for (int i = 0; i < NUM_MENU_ITEMS; i++) {
        if (mouseMoved() && mouseOver(menuButtonRect(i))) menuSel = i;
        if (clickedOn(menuButtonRect(i))) { menuSel = i; activate = 1; }
    }
    // ESC resumes a game in progress
    if (IsKeyPressed(KEY_ESCAPE) && gameStarted) { *state = STATE_OVERWORLD; return; }
    if (!activate) return;
    consumeInput();
    if (menuSel == 0) {
        if (!gameStarted) gameNew();
        gameStarted = 1;
        *state = STATE_OVERWORLD;
    }
    else if (menuSel == 1) {
        if (saveExists && gameLoad()) { gameStarted = 1; *state = STATE_OVERWORLD; showMessage("[SYSTEM] Save loaded.", 2.0f); }
        else { menuNotice = "No compatible save found."; menuNoticeTimer = 2.5f; }
    }
    else if (menuSel == 2) { battleStartTestRange(); *state = STATE_BATTLE; }   // active mech vs a passive dummy
    else if (menuSel == 3) *state = STATE_SETTINGS;
    else quitRequested = 1;
}

void uiMenuDraw(void) {
    drawMenuBackground();
    BeginMode2D(layoutCamera());
    drawMenuTitle("MECH PILOT", "- NEON WASTELAND -");
    drawMechBattle(MODEL_NOVA, SCREEN_W / 2, 215, 8, 0);
    const char* labels[NUM_MENU_ITEMS] = { gameStarted ? "RESUME" : "NEW GAME", "LOAD GAME", "TEST RANGE", "SETTINGS", "EXIT" };
    for (int i = 0; i < NUM_MENU_ITEMS; i++)
        drawButton(menuButtonRect(i), labels[i], 22, i == menuSel, i != 1 || saveExists);
    if (menuNoticeTimer > 0) {
        menuNoticeTimer -= GetFrameTime();
        drawTextCentered(menuNotice, SCREEN_H - 56, 16, (Color) { 255, 120, 110, 255 });
    }
    drawTextCentered("[WASD/ARROWS] Navigate   [Z/ENTER/CLICK] Select", SCREEN_H - 30, 16, (Color) { 150, 220, 255, 200 });
    EndMode2D();
}

// ============ SETTINGS ============
void uiSettingsOpen(void) { settingsSel = 0; }

void uiSettingsUpdate(GameState* state) {
    if (UP_PRESSED)   settingsSel = (settingsSel + NUM_SETTINGS_ITEMS - 1) % NUM_SETTINGS_ITEMS;
    if (DOWN_PRESSED) settingsSel = (settingsSel + 1) % NUM_SETTINGS_ITEMS;
    if (LEFT_PRESSED)  changeDisplaySetting(settingsSel, -1);
    if (RIGHT_PRESSED) changeDisplaySetting(settingsSel, +1);

    int activate = confirmPressed();
    for (int i = 0; i < NUM_SETTINGS_ITEMS; i++) {
        if (mouseMoved() && mouseOver(settingsRowRect(i))) settingsSel = i;
        if (i < 2 && clickedOn(settingsArrowRect(i, 0))) { settingsSel = i; changeDisplaySetting(i, -1); consumeInput(); }
        else if (clickedOn(settingsRowRect(i))) { settingsSel = i; activate = 1; }
    }
    if (IsKeyPressed(KEY_ESCAPE) || (activate && settingsSel == 2)) {
        consumeInput();
        *state = STATE_MENU;
        return;
    }
    if (activate) { consumeInput(); changeDisplaySetting(settingsSel, +1); }
}

void uiSettingsDraw(void) {
    drawMenuBackground();
    BeginMode2D(layoutCamera());
    drawMenuTitle("SETTINGS", "- DISPLAY -");

    const char* names[2] = { "DISPLAY MODE", "ASPECT RATIO" };
    const char* values[2] = { settingFullscreen ? "FULLSCREEN" : "WINDOWED", aspectLabel(settingAspect) };
    for (int i = 0; i < 2; i++) {
        Rectangle r = settingsRowRect(i);
        int enabled = !(i == 1 && settingFullscreen);
        drawButton(r, "", 20, i == settingsSel, enabled);
        Color text = enabled ? (Color) { 220, 240, 255, 255 } : (Color) { 100, 110, 130, 255 };
        DrawText(names[i], (int)r.x + 20, (int)r.y + 18, 20, text);
        drawButton(settingsArrowRect(i, 0), "<", 24, 0, enabled);
        drawButton(settingsArrowRect(i, 1), ">", 24, 0, enabled);
        Rectangle left = settingsArrowRect(i, 0), right = settingsArrowRect(i, 1);
        int vw = MeasureText(values[i], 20);
        int mid = (int)((left.x + left.width + right.x) / 2);
        DrawText(values[i], mid - vw / 2, (int)r.y + 18, 20, enabled ? (Color) { 120, 240, 255, 255 } : text);
    }
    if (settingFullscreen)
        drawTextCentered("Aspect ratio follows your monitor in fullscreen.", 342, 16, (Color) { 150, 170, 200, 220 });
    drawButton(settingsRowRect(2), "BACK", 22, settingsSel == 2, 1);

    drawTextCentered("[W/S] Select   [A/D] Change   [Z/ENTER/CLICK] Toggle   [ESC] Back", SCREEN_H - 30, 16,
        (Color) { 150, 220, 255, 200 });
    EndMode2D();
}
