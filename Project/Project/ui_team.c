#include "ui.h"
#include "mech.h"
#include "game.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// Two views: the team grid, and a loadout screen for one mech where refit
// modules, weapons and firmware chips can be swapped (outside combat only),
// and chip loadouts saved to / loaded from Firmware Profiles.

static int teamSel = 0;
static int loadoutOpen = 0;
static int loadoutRow = 0;
static char profileMsg[64] = "";
static int compareOpen = 0, compareSel = 0, compareScroll = 0;
static float switchFlash = 0;      // seconds remaining on the "DEPLOYED" flash
static int switchFlashSlot = -1;

enum { ROW_REFIT = 0, ROW_WEAPON = NUM_REFIT_SLOTS, ROW_SOCKET = NUM_REFIT_SLOTS + MAX_WEAPONS };

static Rectangle teamSlotRect(int i) {
    int col = i % 2, row = i / 2;
    return (Rectangle) { 40.0f + col * 380, 80.0f + row * 160, 340, 146 };
}
static Rectangle closeButtonRect(void) { return (Rectangle) { 30, SCREEN_H - 42, 200, 30 }; }
static Rectangle loadoutButtonRect(void) { return (Rectangle) { 245, SCREEN_H - 42, 200, 30 }; }
// Rows are grouped REFIT / WEAPON SYSTEM / FIRMWARE with a label gap between groups
static Rectangle loadoutRowRect(int i) {
    int group = i >= ROW_SOCKET ? 2 : (i >= ROW_WEAPON ? 1 : 0);
    return (Rectangle) { 30, 80.0f + i * 25 + group * 14, 440, 22 };
}
// Profile strip under the loadout rows: click the name to load, SAVE to overwrite
static Rectangle profileRect(int i) { return (Rectangle) { 30.0f + i * 148, 518, 140, 30 }; }
static Rectangle profileSaveRect(int i) {
    Rectangle r = profileRect(i);
    return (Rectangle) { r.x + r.width - 42, r.y + 4, 38, r.height - 8 };
}
static Rectangle arrowRect(int i, int right) {
    Rectangle r = loadoutRowRect(i);
    return (Rectangle) { right ? r.x + r.width - 24 : r.x + 90, r.y + 1, 20, 20 };
}

void uiTeamOpen(void) {
    teamSel = activeTeamSlot;
    loadoutOpen = 0;
    loadoutRow = 0;
    profileMsg[0] = 0;
}

static int loadoutRows(const Mech* m) { return ROW_SOCKET + firmwareSockets(&m->fw); }

// ============ LOADOUT CHANGES ============
// Only parts in the inventory (owned and not fitted to another mech) can be installed
static void cycleRefit(Mech* m, int slot, int dir) {
    int cur = m->refit[slot];
    for (int k = 1; k <= NUM_REFIT_MODULES; k++) {
        int idx = ((cur + dir * k) % NUM_REFIT_MODULES + NUM_REFIT_MODULES) % NUM_REFIT_MODULES;
        if (refitModules[idx].slot == (RefitSlot)slot && moduleAvailable(idx) > 0) {
            mechSetRefit(m, (RefitSlot)slot, idx);
            mechReplate(m);
            return;
        }
    }
}

// Options run -1 (empty) .. count-1
static int stepOption(int cur, int dir, int count) {
    int n = count + 1;
    return ((cur + 1 + dir) % n + n) % n - 1;
}

static void cycleWeapon(Mech* m, int mount, int dir) {
    int w = m->weapons[mount].weapon;
    for (int k = 0; k <= NUM_WEAPONS; k++) {
        w = stepOption(w, dir, NUM_WEAPONS);
        if (w < 0 || weaponAvailable(w) > 0) { mechSetWeapon(m, mount, w); return; }
    }
}

static void cycleChip(Mech* m, int socket, int dir) {
    int cur = m->fw.chips[socket], c = cur;
    for (int k = 0; k <= NUM_CHIPS; k++) {
        c = stepOption(c, dir, NUM_CHIPS);
        if (c < 0 || ((chipAvailable(c) > 0 || c == cur) && firmwareCanInstall(&m->fw, socket, c))) {
            firmwareInstall(&m->fw, socket, c);
            mechRefreshStats(m);
            return;
        }
    }
}

static void loadProfile(Mech* m, int slot) {
    int skipped = mechLoadProfile(m, slot);
    const char* name = m->fw.profiles[slot].name;
    if (skipped < 0) snprintf(profileMsg, sizeof(profileMsg), "%s is empty - [SHIFT+%d] to save", name, slot + 1);
    else if (skipped > 0) snprintf(profileMsg, sizeof(profileMsg), "Loaded %s (%d chip%s unavailable)", name, skipped, skipped > 1 ? "s" : "");
    else snprintf(profileMsg, sizeof(profileMsg), "Loaded %s", name);
    if (loadoutRow >= loadoutRows(m)) loadoutRow = 0;
}

static void saveProfile(Mech* m, int slot) {
    firmwareSaveProfile(&m->fw, slot, NULL);
    snprintf(profileMsg, sizeof(profileMsg), "Saved current chips to %s", m->fw.profiles[slot].name);
}

// ============ COMPARE ============
// Every option you could put in the selected slot, measured by building the
// mech with it and diffing the final attributes against the current build.
#define MAX_CANDIDATES 32
static int candidates[MAX_CANDIDATES];
static int numCandidates = 0;

static int rowSlotKind(int row) { return row < ROW_WEAPON ? 0 : row < ROW_SOCKET ? 1 : 2; }

static void buildCandidates(const Mech* m, int row) {
    numCandidates = 0;
    int kind = rowSlotKind(row);
    if (kind == 0) {
        int slot = row - ROW_REFIT;
        for (int i = 0; i < NUM_REFIT_MODULES && numCandidates < MAX_CANDIDATES; i++)
            if (refitModules[i].slot == (RefitSlot)slot && (i == m->refit[slot] || moduleAvailable(i) > 0)) candidates[numCandidates++] = i;
    }
    else if (kind == 1) {
        int cur = m->weapons[row - ROW_WEAPON].weapon;
        candidates[numCandidates++] = -1;
        for (int w = 0; w < NUM_WEAPONS && numCandidates < MAX_CANDIDATES; w++)
            if (w == cur || weaponAvailable(w) > 0) candidates[numCandidates++] = w;
    }
    else {
        int socket = row - ROW_SOCKET, cur = m->fw.chips[socket];
        candidates[numCandidates++] = -1;
        for (int c = 0; c < NUM_CHIPS && numCandidates < MAX_CANDIDATES; c++)
            if (c == cur || (chipAvailable(c) > 0 && firmwareCanInstall(&m->fw, socket, c))) candidates[numCandidates++] = c;
    }
}

static int currentOption(const Mech* m, int row) {
    int kind = rowSlotKind(row);
    return kind == 0 ? m->refit[row - ROW_REFIT] : kind == 1 ? m->weapons[row - ROW_WEAPON].weapon : m->fw.chips[row - ROW_SOCKET];
}

static Mech trialBuild(const Mech* m, int row, int option) {
    Mech t = *m;
    int kind = rowSlotKind(row);
    if (kind == 0) mechSetRefit(&t, (RefitSlot)(row - ROW_REFIT), option);
    else if (kind == 1) mechSetWeapon(&t, row - ROW_WEAPON, option);
    else { firmwareInstall(&t.fw, row - ROW_SOCKET, option); mechRefreshStats(&t); }
    return t;
}

static void applyOption(Mech* m, int row, int option) {
    int kind = rowSlotKind(row);
    if (kind == 0) { mechSetRefit(m, (RefitSlot)(row - ROW_REFIT), option); mechReplate(m); }
    else if (kind == 1) mechSetWeapon(m, row - ROW_WEAPON, option);
    else { firmwareInstall(&m->fw, row - ROW_SOCKET, option); mechRefreshStats(m); }
}

static void openCompare(const Mech* m) {
    buildCandidates(m, loadoutRow);
    compareSel = compareScroll = 0;
    for (int i = 0; i < numCandidates; i++) if (candidates[i] == currentOption(m, loadoutRow)) compareSel = i;
    compareOpen = 1;
}

static void updateCompare(Mech* m) {
    if (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_ESCAPE)) { consumeInput(); compareOpen = 0; return; }
    if (numCandidates == 0) return;
    if (UP_PRESSED)   compareSel = (compareSel + numCandidates - 1) % numCandidates;
    if (DOWN_PRESSED) compareSel = (compareSel + 1) % numCandidates;
    if (compareSel < compareScroll) compareScroll = compareSel;
    if (compareSel >= compareScroll + 13) compareScroll = compareSel - 12;
    if (confirmPressed()) {
        consumeInput();
        applyOption(m, loadoutRow, candidates[compareSel]);
        compareOpen = 0;
    }
}

static void changeRow(Mech* m, int row, int dir) {
    if (row < ROW_WEAPON) cycleRefit(m, row - ROW_REFIT, dir);
    else if (row < ROW_SOCKET) cycleWeapon(m, row - ROW_WEAPON, dir);
    else cycleChip(m, row - ROW_SOCKET, dir);
}

// ============ UPDATE ============
static void updateLoadout(void) {
    Mech* m = &team[teamSel];
    int rows = loadoutRows(m);
    if (compareOpen) { updateCompare(m); return; }
    if (IsKeyPressed(KEY_C)) { consumeInput(); openCompare(m); return; }
    if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_E) || clickedOn(closeButtonRect())) {
        consumeInput();
        loadoutOpen = 0;
        return;
    }
    if (UP_PRESSED)   loadoutRow = (loadoutRow + rows - 1) % rows;
    if (DOWN_PRESSED) loadoutRow = (loadoutRow + 1) % rows;
    if (LEFT_PRESSED)  changeRow(m, loadoutRow, -1);
    if (RIGHT_PRESSED || confirmPressed()) changeRow(m, loadoutRow, +1);
    int shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    for (int p = 0; p < MAX_PROFILES; p++) {
        if (IsKeyPressed(KEY_ONE + p)) { if (shift) saveProfile(m, p); else loadProfile(m, p); }
        if (clickedOn(profileSaveRect(p))) { saveProfile(m, p); consumeInput(); }
        else if (clickedOn(profileRect(p))) { loadProfile(m, p); consumeInput(); }
    }
    for (int i = 0; i < rows; i++) {
        if (mouseMoved() && mouseOver(loadoutRowRect(i))) loadoutRow = i;
        if (clickedOn(arrowRect(i, 0))) { loadoutRow = i; changeRow(m, i, -1); consumeInput(); }
        else if (clickedOn(loadoutRowRect(i))) { loadoutRow = i; changeRow(m, i, +1); consumeInput(); }
    }
}

void uiTeamUpdate(GameState* state) {
    if (switchFlash > 0) switchFlash -= GetFrameTime();

    if (loadoutOpen) { updateLoadout(); return; }

    if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE) || clickedOn(closeButtonRect())) {
        consumeInput();
        *state = STATE_OVERWORLD;
        return;
    }
    if (teamSize == 0) return;
    if (teamSel >= teamSize) teamSel = 0;
    if (RIGHT_PRESSED) teamSel = (teamSel + 1) % teamSize;
    if (LEFT_PRESSED)  teamSel = (teamSel + teamSize - 1) % teamSize;
    if (DOWN_PRESSED)  teamSel = (teamSel + 2) % teamSize;
    if (UP_PRESSED)    teamSel = (teamSel + teamSize - 2) % teamSize;
    int activate = confirmPressed();
    for (int i = 0; i < teamSize; i++) {
        if (mouseMoved() && mouseOver(teamSlotRect(i))) teamSel = i;
        if (clickedOn(teamSlotRect(i))) { teamSel = i; activate = 1; }
    }
    if (IsKeyPressed(KEY_E) || clickedOn(loadoutButtonRect())) {
        consumeInput();
        loadoutOpen = 1;
        loadoutRow = 0;
        return;
    }
    if (activate) {
        consumeInput();
        if (activeTeamSlot != teamSel) {
            activeTeamSlot = teamSel;
            switchFlash = 1.2f;
            switchFlashSlot = teamSel;
        }
    }
}

// ============ DRAW ============
static void drawHeader(const char* title) {
    ClearBackground((Color) { 8, 12, 22, 255 });
    DrawRectangle(0, 0, screenW, 60, (Color) { 15, 25, 45, 255 });
    DrawRectangleLines(0, 0, screenW, 60, (Color) { 100, 200, 255, 220 });
    BeginMode2D(layoutCamera());
    DrawText(title, 30, 18, 26, (Color) { 150, 220, 255, 255 });
}

static void drawRating(int x, int y, int rating) {
    for (int i = 0; i < 5; i++) {
        Color c = i < rating ? (rating >= 4 ? (Color) { 120, 255, 160, 255 } : rating >= 3 ? (Color) { 255, 220, 100, 255 }
            : (Color) { 255, 110, 90, 255 }) : (Color) { 50, 60, 80, 255 };
        DrawRectangle(x + i * 9, y, 7, 7, c);
    }
}

static void drawTeamGrid(void) {
    drawHeader(">> MECH TEAM");
    DrawText(TextFormat("%d / %d", teamSize, MAX_TEAM), SCREEN_W - 100, 22, 20, WHITE);
    DrawText("[Z/ENTER/CLICK] Deploy selected mech", 30, 66, 12, (Color) { 150, 190, 220, 220 });

    for (int i = 0; i < teamSize; i++) {
        Rectangle slot = teamSlotRect(i);
        int bx = (int)slot.x, by = (int)slot.y;
        Mech* m = &team[i];
        const MechModel* model = mechModel(m);
        const MechStats* s = &m->stats;

        DrawRectangleRec(slot, (Color) { 20, 30, 50, 230 });
        if (i == teamSel)
            DrawRectangleLinesEx((Rectangle) { slot.x - 4, slot.y - 4, slot.width + 8, slot.height + 8 }, 2, (Color) { 120, 240, 255, 255 });
        DrawRectangleLinesEx(slot, 1, i == activeTeamSlot ? (Color) { 255, 220, 100, 255 } : (Color) { 80, 140, 200, 200 });
        if (i == activeTeamSlot) DrawText("ACTIVE", bx + 8, by + 6, 12, (Color) { 255, 220, 100, 255 });

        DrawRectangle(bx + 10, by + 24, 90, 100, (Color) { 10, 20, 35, 255 });
        DrawRectangleLines(bx + 10, by + 24, 90, 100, model->accent);
        drawMechOverworld(m->model, bx + 35, by + 60, 0, glowTimer);

        DrawText(m->name, bx + 110, by + 22, 20, model->accent);
        DrawText(TextFormat("FW %s", firmwareLabel(m->fw.revision)), bx + 270, by + 24, 16, WHITE);
        DrawText(TextFormat("%s %s", model->designation, model->name), bx + 110, by + 44, 12, (Color) { 180, 200, 220, 255 });
        DrawText(TextFormat("%s / %s", classNames[mechClass(m)], roleName(mechRole(m))), bx + 110, by + 58, 10,
            (Color) { 150, 170, 190, 220 });

        drawIntegrityBar(bx + 110, by + 74, 180, 9, s->integrity, s->maxIntegrity);
        DrawText(TextFormat("%d/%d", s->integrity, s->maxIntegrity), bx + 294, by + 74, 10, WHITE);
        drawArmorBar(bx + 110, by + 87, 180, 6, s->armor, s->maxArmor);
        DrawText(TextFormat("%d/%d", s->armor, s->maxArmor), bx + 294, by + 85, 10, (Color) { 150, 190, 240, 255 });

        DrawText(TextFormat("PWR %.2fx  MOB %d%%  EN %d  HEAT %d", s->power, s->mobility, s->maxEnergy, s->maxHeat),
            bx + 110, by + 102, 12, (Color) { 255, 190, 150, 255 });
        DrawText(TextFormat("ACC %d%%  STB %d%%", s->accuracy, s->stability), bx + 110, by + 118, 12, (Color) { 150, 190, 255, 255 });
        int installed = 0;
        for (int k = 0; k < firmwareSockets(&m->fw); k++) if (m->fw.chips[k] >= 0) installed++;
        DrawText(TextFormat("CHIPS %d/%d", installed, firmwareSockets(&m->fw)), bx + 110, by + 132, 10, (Color) { 200, 170, 255, 255 });
    }

    if (teamSize == 0)
        DrawText("NO MECHS IN TEAM", SCREEN_W / 2 - 120, SCREEN_H / 2, 24, (Color) { 150, 150, 150, 255 });

    drawButton(closeButtonRect(), "CLOSE [TAB/ESC]", 16, 0, 1);
    drawButton(loadoutButtonRect(), "LOADOUT [E]", 16, 0, teamSize > 0);
    DrawText("[Z/ENTER/CLICK] Set active", SCREEN_W - 290, SCREEN_H - 34, 18, (Color) { 255, 220, 100, 255 });

    if (switchFlash > 0 && switchFlashSlot >= 0 && switchFlashSlot < teamSize) {
        float a = switchFlash / 1.2f;
        if (a > 1.0f) a = 1.0f;
        Color c = mechModel(&team[switchFlashSlot])->accent;
        c.a = (unsigned char)(255 * a);
        const char* msg = TextFormat(">> %s DEPLOYED <<", team[switchFlashSlot].name);
        int mw = MeasureText(msg, 22);
        DrawRectangle(SCREEN_W / 2 - mw / 2 - 20, SCREEN_H / 2 - 20, mw + 40, 40, (Color) { 15, 25, 45, (unsigned char)(220 * a) });
        DrawRectangleLines(SCREEN_W / 2 - mw / 2 - 20, SCREEN_H / 2 - 20, mw + 40, 40, c);
        DrawText(msg, SCREEN_W / 2 - mw / 2, SCREEN_H / 2 - 12, 22, c);
    }
    EndMode2D();
}

static const char* rowLabel(int row);

static int wrapLines(const char* text, int x, int y, int width, int size, Color c, int maxLines) {
    char line[200] = "";
    int lines = 0;
    const char* p = text;
    while (*p && lines < maxLines) {
        const char* sp = strchr(p, ' ');
        int len = sp ? (int)(sp - p) : (int)strlen(p);
        char trial[200];
        snprintf(trial, sizeof(trial), "%s%s%.*s", line, line[0] ? " " : "", len, p);
        if (MeasureText(trial, size) > width && line[0]) {
            DrawText(line, x, y + lines * (size + 3), size, c);
            lines++;
            snprintf(line, sizeof(line), "%.*s", len, p);
        }
        else snprintf(line, sizeof(line), "%s", trial);
        p += len;
        while (*p == ' ') p++;
    }
    if (line[0] && lines < maxLines) { DrawText(line, x, y + lines * (size + 3), size, c); lines++; }
    return lines;
}

static Color categoryColor(ChipCategory c) {
    static const Color cols[NUM_CHIP_CATEGORIES] = {
        { 255, 120, 100, 255 }, { 120, 190, 255, 255 }, { 120, 255, 180, 255 }, { 200, 150, 255, 255 },
        { 255, 220, 110, 255 }, { 180, 200, 220, 255 }, { 255, 160, 220, 255 },
    };
    return cols[c];
}

// "PRECISION STRIKE" -> "PS", "COUNTER-INTRUSION" -> "CI"
static const char* chipInitials(const char* name) {
    static char out[4];
    int n = 0, start = 1;
    for (const char* p = name; *p && n < 3; p++) {
        if (start && *p != ' ' && *p != '-') out[n++] = *p;
        start = (*p == ' ' || *p == '-');
    }
    out[n] = 0;
    return out;
}

// Capacity bar (one segment per point, colored by the chip using it) and the
// socket board: installed / empty / locked with the revision that unlocks it.
static void drawSocketBoard(const Mech* m, int x, int y, int w) {
    const Firmware* fw = &m->fw;
    int cap = firmwareCapacity(fw), used = firmwareCapacityUsed(fw), open = firmwareSockets(fw);
    DrawText(TextFormat("PROCESSING %d/%d", used, cap), x, y, 10, (Color) { 200, 170, 255, 255 });
    int seg = cap > 0 ? (w - 110) / cap : 0, sx = x + 108, k = 0;
    for (int s = 0; s < MAX_SOCKETS; s++) {
        if (fw->chips[s] < 0) continue;
        for (int c = 0; c < chipDefs[fw->chips[s]].cost && k < cap; c++, k++)
            DrawRectangle(sx + k * seg, y + 1, seg - 1, 8, categoryColor(chipDefs[fw->chips[s]].category));
    }
    for (; k < cap; k++) DrawRectangleLines(sx + k * seg, y + 1, seg - 1, 8, (Color) { 70, 80, 110, 255 });

    int bw = (w - 7 * 4) / MAX_SOCKETS;
    for (int s = 0; s < MAX_SOCKETS; s++) {
        Rectangle r = { (float)(x + s * (bw + 4)), (float)(y + 14), (float)bw, 18 };
        if (s >= open) {
            Firmware t;
            int unlock = fw->revision;
            do { firmwareInit(&t, ++unlock); } while (unlock < MAX_REVISION && firmwareSockets(&t) <= s);
            DrawRectangleRec(r, (Color) { 20, 22, 30, 255 });
            DrawText(firmwareLabel(unlock), (int)r.x + 6, (int)r.y + 5, 10, (Color) { 90, 100, 120, 255 });
            continue;
        }
        int c = fw->chips[s];
        int corrupted = c >= 0 && fw->corrupt[s] > 0;
        if (c >= 0) {
            Color cc = categoryColor(chipDefs[c].category);
            DrawRectangleRec(r, (Color) { cc.r / 3, cc.g / 3, cc.b / 3, 255 });
            DrawRectangleLinesEx(r, 1, corrupted ? RED : cc);
            DrawText(chipInitials(chipDefs[c].name), (int)r.x + 4, (int)r.y + 5, 10, cc);
        }
        else {
            DrawRectangleLinesEx(r, 1, (Color) { 90, 110, 150, 255 });
            DrawText("+", (int)r.x + bw / 2 - 3, (int)r.y + 4, 10, (Color) { 90, 110, 150, 255 });
        }
        if (loadoutRow == ROW_SOCKET + s) DrawRectangleLinesEx((Rectangle) { r.x - 2, r.y - 2, r.width + 4, r.height + 4 }, 1, WHITE);
    }
}

static const char* optionName(int row, int option) {
    if (option < 0) return "-- EMPTY --";
    int kind = rowSlotKind(row);
    return kind == 0 ? refitModules[option].name : kind == 1 ? weaponTable[option].name : chipDefs[option].name;
}

// Full-screen comparison of every option for the selected slot
static void drawCompare(const Mech* m) {
    int x = 20, y = 66, w = SCREEN_W - 40, h = 470;
    int kind = rowSlotKind(loadoutRow), cur = currentOption(m, loadoutRow);
    DrawRectangle(x, y, w, h, (Color) { 8, 12, 24, 255 });
    DrawRectangleLines(x, y, w, h, (Color) { 120, 220, 255, 230 });
    DrawText(TextFormat("COMPARE  %s", rowLabel(loadoutRow)), x + 10, y + 8, 18, (Color) { 150, 230, 255, 255 });
    DrawText("[W/S] select   [Z] install   [C/ESC] close      green = better than now, red = worse", x + 230, y + 12, 10,
        (Color) { 120, 160, 200, 255 });

    StatBreakdown now;
    mechStatBreakdown(m, &now);
    static const StatId shown[] = { STAT_INTEGRITY, STAT_POWER, STAT_ARMOR, STAT_MOBILITY, STAT_ENERGY, STAT_HEAT, STAT_COOLING, STAT_ACCURACY, STAT_STABILITY };
    int nShown = (int)(sizeof(shown) / sizeof(shown[0]));
    int hy = y + 36;
    DrawText("OPTION", x + 10, hy, 10, (Color) { 120, 160, 200, 255 });
    if (kind == 1) {
        const char* cols[] = { "DMG", "ACC", "PEN", "HEAT", "EN", "SCR", "RATING" };
        for (int c = 0; c < 7; c++) DrawText(cols[c], x + 220 + c * 62, hy, 10, (Color) { 120, 160, 200, 255 });
    }
    else for (int k = 0; k < nShown; k++) DrawText(statShort(shown[k]), x + 220 + k * 50, hy, 10, (Color) { 120, 160, 200, 255 });

    for (int r = 0; r < 13 && compareScroll + r < numCandidates; r++) {
        int idx = compareScroll + r, opt = candidates[idx];
        int ry = hy + 16 + r * 20;
        if (idx == compareSel) DrawRectangle(x + 4, ry - 3, w - 8, 18, (Color) { 40, 80, 120, 220 });
        DrawText(TextFormat("%s%s", optionName(loadoutRow, opt), opt == cur ? "  (now)" : ""), x + 10, ry, 11,
            opt == cur ? (Color) { 255, 220, 120, 255 } : WHITE);
        Mech t = trialBuild(m, loadoutRow, opt);
        if (kind == 1) {
            const Weapon* tw = opt >= 0 ? &t.weapons[loadoutRow - ROW_WEAPON].fitted : NULL;
            const Weapon* cw = mechWeapon(m, loadoutRow - ROW_WEAPON);
            if (!tw) continue;
            int vals[6] = { tw->baseDamage, tw->accuracy, tw->armorPen, tw->heat, tw->energyCost, tw->scramble };
            int base[6] = { cw ? cw->baseDamage : 0, cw ? cw->accuracy : 0, cw ? cw->armorPen : 0, cw ? cw->heat : 0,
                            cw ? cw->energyCost : 0, cw ? cw->scramble : 0 };
            int lowerIsBetter[6] = { 0, 0, 0, 1, 1, 0 };
            for (int c = 0; c < 6; c++) {
                int d = vals[c] - base[c];
                int better = lowerIsBetter[c] ? d < 0 : d > 0;
                DrawText(TextFormat("%d", vals[c]), x + 220 + c * 62, ry, 11,
                    d == 0 ? (Color) { 190, 200, 215, 255 } : better ? (Color) { 120, 255, 160, 255 } : (Color) { 255, 130, 120, 255 });
            }
            DrawText(TextFormat("%d/5", t.weapons[loadoutRow - ROW_WEAPON].rating), x + 220 + 6 * 62, ry, 11,
                (Color) { 255, 220, 120, 255 });
        }
        else {
            StatBreakdown tb;
            mechStatBreakdown(&t, &tb);
            for (int k = 0; k < nShown; k++) {
                float d = tb.final.v[shown[k]] - now.final.v[shown[k]];
                if (fabsf(d) < 0.001f) { DrawText(".", x + 232 + k * 50, ry, 11, (Color) { 90, 100, 120, 255 }); continue; }
                DrawText(shown[k] == STAT_POWER ? TextFormat("%+.2f", d) : TextFormat("%+d", (int)roundf(d)), x + 220 + k * 50, ry, 11,
                    d > 0 ? (Color) { 120, 255, 160, 255 } : (Color) { 255, 130, 120, 255 });
            }
        }
    }

    // What the highlighted option actually does
    int dy = y + h - 96;
    DrawLine(x + 8, dy - 6, x + w - 8, dy - 6, (Color) { 60, 100, 150, 200 });
    int opt = numCandidates > 0 ? candidates[compareSel] : -1;
    if (opt < 0) { DrawText("Leave the slot empty.", x + 10, dy, 12, (Color) { 180, 200, 220, 255 }); return; }
    if (kind == 0) {
        int rating = refitModules[opt].rating[mechClass(m)];
        DrawText(refitModules[opt].desc, x + 10, dy, 12, WHITE);
        DrawText(TextFormat("Compatibility with %s: %d/5 - keeps %d%% of the upside, all of the downside.", classNames[mechClass(m)],
            rating, (int)roundf(refitRatingFactor(rating) * 100)), x + 10, dy + 18, 11, (Color) { 255, 220, 120, 255 });
        DrawText(TextFormat("Free in inventory: %s", moduleAvailable(opt) > 50 ? "unlimited" : TextFormat("%d", moduleAvailable(opt))),
            x + 10, dy + 36, 11, (Color) { 170, 190, 210, 255 });
    }
    else if (kind == 1) {
        const Weapon* wt = &weaponTable[opt];
        DrawText(TextFormat("%s / %s / %s   base DMG %d  PEN %d%%  AMMO %s", platformNames[wt->platform], munitionNames[wt->munition],
            targetingNames[wt->targeting], wt->baseDamage, wt->armorPen, wt->ammo ? TextFormat("%d", wt->ammo) : "INF"),
            x + 10, dy, 12, munitionColor(wt->munition));
        DrawText("Numbers above are after this mech's mount rating. PEN = share of each hit that ignores Armor.",
            x + 10, dy + 18, 11, (Color) { 170, 190, 210, 255 });
    }
    else {
        const ChipDef* d = &chipDefs[opt];
        DrawText(TextFormat("%s  -  %s %s, capacity %d", d->desc, chipRarityName(d->rarity), chipCategoryName(d->category), d->cost),
            x + 10, dy, 12, categoryColor(d->category));
        wrapLines(d->plain, x + 10, dy + 18, w - 20, 11, (Color) { 210, 225, 240, 255 }, 4);
    }
}

static const char* rowLabel(int row) {
    if (row < ROW_WEAPON) return refitSlotNames[row - ROW_REFIT];
    if (row < ROW_SOCKET) return TextFormat("WEAPON %d", row - ROW_WEAPON + 1);
    return TextFormat("SOCKET %d", row - ROW_SOCKET + 1);
}

static const char* rowLabel(int row);

static void drawLoadout(void) {
    Mech* m = &team[teamSel];
    const MechModel* model = mechModel(m);
    drawHeader(TextFormat(">> LOADOUT  %s", m->name));
    DrawText(TextFormat("%s %s - %s", model->designation, model->name, roleName(mechRole(m))),
        SCREEN_W - 330, 24, 16, model->accent);

    int rows = loadoutRows(m);
    for (int i = 0; i < rows; i++) {
        Rectangle r = loadoutRowRect(i);
        int sel = i == loadoutRow;
        const char* group = i == ROW_REFIT ? "CHASSIS REFIT" : i == ROW_WEAPON ? "WEAPON SYSTEM" : i == ROW_SOCKET ? "FIRMWARE" : NULL;
        if (group) DrawText(group, 30, (int)r.y - 12, 10, (Color) { 120, 160, 200, 255 });
        DrawRectangleRec(r, sel ? (Color) { 40, 90, 130, 200 } : (Color) { 15, 25, 45, 220 });
        DrawRectangleLinesEx(r, 1, sel ? (Color) { 120, 240, 255, 255 } : (Color) { 60, 120, 180, 160 });
        DrawText(rowLabel(i), (int)r.x + 8, (int)r.y + 6, 12, (Color) { 150, 190, 220, 255 });
        drawButton(arrowRect(i, 0), "<", 14, 0, 1);
        drawButton(arrowRect(i, 1), ">", 14, 0, 1);

        const char* value;
        Color valueCol = (Color) { 220, 240, 255, 255 };
        if (i < ROW_WEAPON) {
            int mod = m->refit[i - ROW_REFIT];
            value = refitModules[mod].name;
            drawRating((int)(r.x + r.width) - 80, (int)r.y + 8, refitRating(m, mod));
        }
        else if (i < ROW_SOCKET) {
            const Weapon* w = mechWeapon(m, i - ROW_WEAPON);
            value = w ? TextFormat("%s  (%d EN)", w->name, w->energyCost) : "-- EMPTY --";
            if (w) {
                valueCol = munitionColor(w->munition);
                drawRating((int)(r.x + r.width) - 80, (int)r.y + 8, m->weapons[i - ROW_WEAPON].rating);
            }
        }
        else {
            int c = m->fw.chips[i - ROW_SOCKET];
            value = c >= 0 ? TextFormat("%s  [%d]", chipDefs[c].name, chipDefs[c].cost) : "-- EMPTY --";
            if (c >= 0) valueCol = (Color) { 200, 170, 255, 255 };
        }
        DrawText(value, (int)r.x + 120, (int)r.y + 6, 12, valueCol);
    }

    // Right panel: stats + firmware + selected item description
    int px = 490, py = 70;
    StatBreakdown b;
    mechStatBreakdown(m, &b);
    Stats s = b.final;
    DrawRectangle(px, py, 290, 470, (Color) { 15, 25, 45, 220 });
    DrawRectangleLines(px, py, 290, 470, (Color) { 80, 160, 220, 200 });
    DrawText("ATTRIBUTES", px + 10, py + 8, 14, (Color) { 150, 220, 255, 255 });
    for (int i = 0; i < NUM_STATS; i++) {
        DrawText(statName((StatId)i), px + 14, py + 30 + i * 20, 14, (Color) { 170, 190, 210, 255 });
        DrawText(statFormat((StatId)i, s.v[i]), px + 160, py + 30 + i * 20, 14, WHITE);
    }
    int fy = py + 30 + NUM_STATS * 20 + 10;
    DrawText(TextFormat("FIRMWARE REVISION %s", firmwareLabel(m->fw.revision)), px + 10, fy, 14, (Color) { 200, 170, 255, 255 });
    drawSocketBoard(m, px + 10, fy + 18, 270);
    DrawText(TextFormat("Revision Data: %d/%d", m->fw.data, firmwareDataToNext(m->fw.revision)), px + 14, fy + 52, 12, WHITE);
    DrawText(TextFormat("TRAIT  %s", traitDefs[m->fw.trait].name), px + 14, fy + 68, 10, (Color) { 255, 220, 120, 255 });
    const char* branches = m->fw.numBranches == 0 ? "none (first at 2.0)" : "";
    for (int i = 0; i < m->fw.numBranches; i++)
        branches = TextFormat("%s%s%s", branches, i ? ", " : "", branchDefs[m->fw.branches[i]].name);
    DrawText(TextFormat("BRANCH %s", branches), px + 14, fy + 81, 10, (Color) { 255, 220, 120, 255 });

    int dy = fy + 100;
    DrawLine(px + 10, dy - 8, px + 280, dy - 8, (Color) { 60, 100, 150, 200 });
    if (loadoutRow < ROW_WEAPON) {
        int mod = m->refit[loadoutRow - ROW_REFIT];
        int rating = refitRating(m, mod);
        DrawText(refitModules[mod].name, px + 10, dy, 14, WHITE);
        DrawText(refitModules[mod].desc, px + 10, dy + 20, 10, (Color) { 180, 200, 220, 255 });
        DrawText(TextFormat("Compatibility %d/5 (%s): %d%% of upside", rating, classNames[mechClass(m)],
            (int)roundf(refitRatingFactor(rating) * 100)), px + 10, dy + 36, 10, (Color) { 180, 200, 220, 255 });
        DrawText("[C] compare every module for this slot", px + 10, dy + 56, 10, (Color) { 100, 200, 240, 255 });
    }
    else if (loadoutRow < ROW_SOCKET) {
        int mount = loadoutRow - ROW_WEAPON;
        const Weapon* w = mechWeapon(m, mount);
        if (w) {
            int rating = m->weapons[mount].rating;
            DrawText(TextFormat("Compatibility %d/5 (%s): %d%% of upside", rating, classNames[mechClass(m)],
                (int)roundf(refitRatingFactor(rating) * 100)), px + 10, dy + 50, 10, (Color) { 180, 200, 220, 255 });
            DrawText(TextFormat("%s  -  %s / %s", w->name, platformNames[w->platform], munitionNames[w->munition]),
                px + 10, dy, 10, WHITE);
            DrawText(TextFormat("DMG %d  ACC %d%%  PEN %d%%  COST %d EN", w->baseDamage, w->accuracy,
                w->armorPen, w->energyCost), px + 10, dy + 18, 10, (Color) { 180, 200, 220, 255 });
            DrawText(TextFormat("HEAT +%d  SCRAMBLE %d  AMMO %s  RANGE %d", w->heat, w->scramble,
                w->ammo > 0 ? TextFormat("%d", w->ammo) : "INF", w->range), px + 10, dy + 34, 10, (Color) { 180, 200, 220, 255 });
        }
        DrawText("[C] compare every weapon you own", px + 10, dy + 70, 10, (Color) { 100, 200, 240, 255 });
    }
    else {
        int c = m->fw.chips[loadoutRow - ROW_SOCKET];
        if (c >= 0) {
            const ChipDef* d = &chipDefs[c];
            DrawText(d->name, px + 10, dy, 14, WHITE);
            DrawText(TextFormat("%s  %s  COST %d", chipCategoryName(d->category), chipRarityName(d->rarity), d->cost),
                px + 10, dy + 20, 10, (Color) { 200, 170, 255, 255 });
            DrawText(d->desc, px + 10, dy + 36, 10, (Color) { 180, 200, 220, 255 });
            wrapLines(d->plain, px + 10, dy + 52, 270, 10, (Color) { 210, 225, 240, 255 }, 4);
        }
        else DrawText("Empty socket - [C] to compare chips that fit.", px + 10, dy, 10, (Color) { 150, 170, 190, 255 });
        DrawText("Chips owned / free:  [C] compare", px + 10, dy + 106, 10, (Color) { 150, 170, 190, 255 });
        int line = 0;
        for (int k = 0; k < NUM_CHIPS && line < 2; k++) {
            if (chipOwned[k] == 0) continue;
            DrawText(TextFormat("%s  %d/%d", chipDefs[k].name, chipOwned[k], chipAvailable(k)),
                px + 14, dy + 120 + line * 13, 10, (Color) { 170, 190, 210, 255 });
            line++;
        }
    }

    DrawText("FIRMWARE PROFILES  [1-3] load  [SHIFT+1-3] save", 30, 506, 10, (Color) { 120, 160, 200, 255 });
    for (int p = 0; p < MAX_PROFILES; p++) {
        const FirmwareProfile* prof = &m->fw.profiles[p];
        Rectangle r = profileRect(p);
        int n = 0;
        for (int k = 0; k < MAX_SOCKETS; k++) if (prof->chips[k] >= 0) n++;
        DrawRectangleRec(r, mouseOver(r) ? (Color) { 40, 70, 110, 230 } : (Color) { 15, 25, 45, 220 });
        DrawRectangleLinesEx(r, 1, prof->saved ? (Color) { 200, 170, 255, 255 } : (Color) { 60, 80, 110, 255 });
        DrawText(TextFormat("%d %s", p + 1, prof->name), (int)r.x + 6, (int)r.y + 4, 10, prof->saved ? WHITE : (Color) { 110, 120, 140, 255 });
        DrawText(prof->saved ? TextFormat("%d chips", n) : "empty", (int)r.x + 6, (int)r.y + 17, 10, (Color) { 150, 170, 190, 255 });
        drawButton(profileSaveRect(p), "SAVE", 10, 0, 1);
    }
    if (profileMsg[0]) DrawText(profileMsg, 490, 548, 10, (Color) { 120, 255, 180, 255 });

    drawButton(closeButtonRect(), "BACK [E/ESC]", 16, 0, 1);
    DrawText("[W/S] Select   [A/D/CLICK] Change   [C] Compare", 250, SCREEN_H - 34, 16, (Color) { 150, 220, 255, 220 });
    if (compareOpen) drawCompare(m);
    EndMode2D();
}

void uiTeamDraw(void) {
    if (loadoutOpen) drawLoadout();
    else drawTeamGrid();
}
