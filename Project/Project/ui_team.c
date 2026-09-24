#include "ui.h"
#include "mech.h"
#include "game.h"
#include <math.h>
#include <stdio.h>

// Two views: the team grid, and a loadout screen for one mech where refit
// modules, weapons and firmware chips can be swapped (outside combat only),
// and chip loadouts saved to / loaded from Firmware Profiles.

static int teamSel = 0;
static int loadoutOpen = 0;
static int loadoutRow = 0;
static char profileMsg[64] = "";

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

static void changeRow(Mech* m, int row, int dir) {
    if (row < ROW_WEAPON) cycleRefit(m, row - ROW_REFIT, dir);
    else if (row < ROW_SOCKET) cycleWeapon(m, row - ROW_WEAPON, dir);
    else cycleChip(m, row - ROW_SOCKET, dir);
}

// ============ UPDATE ============
static void updateLoadout(void) {
    Mech* m = &team[teamSel];
    int rows = loadoutRows(m);
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
        activeTeamSlot = teamSel;
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
    EndMode2D();
}

static const char* rowLabel(int row) {
    if (row < ROW_WEAPON) return refitSlotNames[row - ROW_REFIT];
    if (row < ROW_SOCKET) return TextFormat("WEAPON %d", row - ROW_WEAPON + 1);
    return TextFormat("SOCKET %d", row - ROW_SOCKET + 1);
}

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
    DrawText(TextFormat("Processing Capacity: %d/%d", firmwareCapacityUsed(&m->fw), firmwareCapacity(&m->fw)),
        px + 14, fy + 20, 12, WHITE);
    DrawText(TextFormat("Instruction Sockets: %d", firmwareSockets(&m->fw)), px + 14, fy + 36, 12, WHITE);
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
    }
    else {
        int c = m->fw.chips[loadoutRow - ROW_SOCKET];
        if (c >= 0) {
            const ChipDef* d = &chipDefs[c];
            DrawText(d->name, px + 10, dy, 14, WHITE);
            DrawText(TextFormat("%s  %s  COST %d", chipCategoryName(d->category), chipRarityName(d->rarity), d->cost),
                px + 10, dy + 20, 10, (Color) { 200, 170, 255, 255 });
            DrawText(d->desc, px + 10, dy + 36, 10, (Color) { 180, 200, 220, 255 });
        }
        DrawText("Chips owned / free:", px + 10, dy + 58, 10, (Color) { 150, 170, 190, 255 });
        int line = 0;
        for (int k = 0; k < NUM_CHIPS && line < 5; k++) {
            if (chipOwned[k] == 0) continue;
            DrawText(TextFormat("%s  %d/%d", chipDefs[k].name, chipOwned[k], chipAvailable(k)),
                px + 14, dy + 72 + line * 13, 10, (Color) { 170, 190, 210, 255 });
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
    DrawText("[W/S] Select   [A/D/CLICK] Change", 250, SCREEN_H - 34, 16, (Color) { 150, 220, 255, 220 });
    EndMode2D();
}

void uiTeamDraw(void) {
    if (loadoutOpen) drawLoadout();
    else drawTeamGrid();
}
