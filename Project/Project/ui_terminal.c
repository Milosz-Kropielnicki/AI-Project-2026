#include "ui.h"
#include "game.h"
#include "world.h"
#include <stdio.h>
#include <string.h>

// Terminal: the hub of the loop. Four tabs - the job board, the zone's
// manufacturer showroom, the parts market and services (repair, save).
// Leaving the terminal autosaves.

enum { TAB_JOBS, TAB_SHOWROOM, TAB_PARTS, TAB_SERVICES, NUM_TABS };
static const char* tabNames[NUM_TABS] = { "JOB BOARD", "SHOWROOM", "PARTS MARKET", "SERVICES" };

#define LIST_Y 110
#define ROW_H 26
#define VISIBLE_ROWS 15

// A row of the current tab: what it is and which index it refers to
enum { ROW_JOB, ROW_MECH, ROW_WEAPON, ROW_MODULE, ROW_CHIP, ROW_REPAIR, ROW_SAVE, ROW_EXIT };
typedef struct { int kind, index; } Row;

static int tab = TAB_JOBS, sel = 0, scroll = 0;
static Row rows[64];
static int numRows = 0;
static char status[128] = "";
static int statusGood = 1;
static Mech preview;            // showroom preview, rebuilt when the selection changes
static int previewEntry = -1;

static const Color colHead = { 150, 220, 255, 255 };
static const Color colText = { 210, 225, 240, 255 };
static const Color colDim = { 130, 150, 175, 255 };
static const Color colGood = { 120, 255, 160, 255 };
static const Color colBad = { 255, 120, 110, 255 };
static const Color colCredit = { 255, 220, 100, 255 };

static Rectangle tabRect(int i) { return (Rectangle) { 30.0f + i * 150, 70, 142, 28 }; }
static Rectangle rowRect(int i) { return (Rectangle) { 30, (float)(LIST_Y + i * ROW_H), 440, ROW_H - 3 }; }

static void setStatus(const char* err, const char* ok) {
    snprintf(status, sizeof(status), "%s", err ? err : ok);
    statusGood = err == NULL;
}

// ============ ROWS ============
static void buildRows(void) {
    int zone = worldCurrentZone();
    numRows = 0;
    switch (tab) {
    case TAB_JOBS:   // this zone's board, plus jobs taken elsewhere
        for (int j = 0; j < NUM_JOBS; j++)
            if (jobState[j] != JS_DONE && (jobDefs[j].zone == zone || jobState[j] != JS_OPEN))
                rows[numRows++] = (Row){ ROW_JOB, j };
        break;
    case TAB_SHOWROOM:
        for (int i = 0; i < NUM_CATALOG; i++)
            if (catalog[i].company == zone) rows[numRows++] = (Row){ ROW_MECH, i };
        break;
    case TAB_PARTS:
        for (int w = 0; w < NUM_WEAPONS; w++) rows[numRows++] = (Row){ ROW_WEAPON, w };
        for (int m = 0; m < NUM_REFIT_MODULES; m++)
            if (!moduleIsStandard(m)) rows[numRows++] = (Row){ ROW_MODULE, m };
        for (int c = 0; c < NUM_CHIPS; c++)
            if (chipForSale(c)) rows[numRows++] = (Row){ ROW_CHIP, c };
        break;
    default:
        rows[numRows++] = (Row){ ROW_REPAIR, 0 };
        rows[numRows++] = (Row){ ROW_SAVE, 0 };
        rows[numRows++] = (Row){ ROW_EXIT, 0 };
        break;
    }
    if (sel >= numRows) sel = numRows > 0 ? numRows - 1 : 0;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + VISIBLE_ROWS) scroll = sel - VISIBLE_ROWS + 1;
}

void uiTerminalOpen(void) {
    tab = TAB_JOBS;
    sel = scroll = 0;
    previewEntry = -1;
    int ready = 0;
    for (int j = 0; j < NUM_JOBS; j++) if (jobState[j] == JS_READY) ready++;
    if (ready) setStatus(NULL, TextFormat("%d job%s ready to claim.", ready, ready > 1 ? "s" : ""));
    else setStatus(NULL, TextFormat("Connected to %s terminal.", worldCurrentZoneName()));
    buildRows();
}

// ============ ACTIONS ============
static void leave(GameState* state) {
    showMessage(gameSave() ? "[TERMINAL] Progress saved." : "[TERMINAL] Save failed!", 2.0f);
    *state = STATE_OVERWORLD;
}

static void activate(GameState* state) {
    if (numRows == 0) return;
    Row r = rows[sel];
    switch (r.kind) {
    case ROW_JOB:
        if (jobState[r.index] == JS_OPEN)
            setStatus(jobAccept(r.index), TextFormat("Accepted: %s", jobDefs[r.index].title));
        else if (jobState[r.index] == JS_READY) {
            const JobDef* d = &jobDefs[r.index];
            setStatus(jobClaim(r.index), TextFormat("Paid %d CR%s%s", d->credits, d->reward ? " + " : "",
                d->reward ? rewardName(d->reward, d->rewardItem) : ""));
        }
        else setStatus("Job in progress", NULL);
        break;
    case ROW_MECH:
        setStatus(buyMech(r.index), TextFormat("Delivered: %s. Check your team [TAB].", mechModels[catalog[r.index].chassis].name));
        break;
    case ROW_WEAPON: setStatus(buyWeapon(r.index), TextFormat("Bought %s", weaponTable[r.index].name)); break;
    case ROW_MODULE: setStatus(buyModule(r.index), TextFormat("Bought %s", refitModules[r.index].name)); break;
    case ROW_CHIP:   setStatus(buyChip(r.index), TextFormat("Bought %s", chipDefs[r.index].name)); break;
    case ROW_REPAIR: {
        int cost = teamRepairCost();
        if (cost == 0) { setStatus(NULL, "Team is already at full Integrity."); break; }
        int spent = teamRepair();
        if (spent == 0) setStatus("No credits for repairs", NULL);
        else setStatus(NULL, spent < cost ? TextFormat("Partial repair: %d CR", spent) : TextFormat("Team repaired: %d CR", spent));
        break;
    }
    case ROW_SAVE: setStatus(gameSave() ? NULL : "Save failed", "Game saved."); break;
    default: leave(state); return;
    }
    buildRows();
}

void uiTerminalUpdate(GameState* state) {
    if (IsKeyPressed(KEY_ESCAPE)) { consumeInput(); leave(state); return; }
    int oldTab = tab;
    if (LEFT_PRESSED)  tab = (tab + NUM_TABS - 1) % NUM_TABS;
    if (RIGHT_PRESSED) tab = (tab + 1) % NUM_TABS;
    for (int i = 0; i < NUM_TABS; i++) if (clickedOn(tabRect(i))) { tab = i; consumeInput(); }
    if (tab != oldTab) { sel = scroll = 0; buildRows(); }

    if (numRows > 0) {
        if (UP_PRESSED)   sel = (sel + numRows - 1) % numRows;
        if (DOWN_PRESSED) sel = (sel + 1) % numRows;
        float wheel = GetMouseWheelMove();
        if (wheel != 0) { scroll -= (int)wheel; if (scroll < 0) scroll = 0; if (scroll > numRows - 1) scroll = numRows - 1; }
    }
    int act = confirmPressed();
    for (int i = 0; i < VISIBLE_ROWS && scroll + i < numRows; i++) {
        if (!clickedOn(rowRect(i))) continue;
        consumeInput();
        if (sel == scroll + i) act = 1;   // click once to select, again to confirm
        sel = scroll + i;
    }
    buildRows();
    if (act) { consumeInput(); activate(state); }
}

// ============ DRAW ============
// Word-wrapped text; returns the y below the last line
static int drawWrapped(const char* text, int x, int y, int width, int size, Color c) {
    char line[160] = "";
    const char* p = text;
    while (*p) {
        const char* end = strchr(p, ' ');
        int len = end ? (int)(end - p) : (int)strlen(p);
        char trial[160];
        snprintf(trial, sizeof(trial), "%s%s%.*s", line, line[0] ? " " : "", len, p);
        if (MeasureText(trial, size) > width && line[0]) {
            DrawText(line, x, y, size, c);
            y += size + 4;
            snprintf(line, sizeof(line), "%.*s", len, p);
        }
        else snprintf(line, sizeof(line), "%s", trial);
        p += len;
        while (*p == ' ') p++;
    }
    if (line[0]) { DrawText(line, x, y, size, c); y += size + 4; }
    return y;
}

static const char* jobTag(int j) {
    switch (jobState[j]) {
    case JS_OPEN:   return "OPEN";
    case JS_ACTIVE: return "ACTIVE";
    default:        return "READY";
    }
}

static void drawRow(int i, Row r, int selected) {
    Rectangle rr = rowRect(i);
    DrawRectangleRec(rr, selected ? (Color) { 40, 90, 130, 220 } : (Color) { 15, 25, 45, 220 });
    DrawRectangleLinesEx(rr, 1, selected ? (Color) { 120, 230, 255, 255 } : (Color) { 50, 80, 120, 200 });
    int x = (int)rr.x + 8, y = (int)rr.y + 5;
    switch (r.kind) {
    case ROW_JOB: {
        Color tc = jobState[r.index] == JS_READY ? colGood : jobState[r.index] == JS_ACTIVE ? colCredit : colDim;
        DrawText(jobTag(r.index), x, y, 12, tc);
        DrawText(jobDefs[r.index].title, x + 64, y, 14, WHITE);
        DrawText(TextFormat("%d CR", jobDefs[r.index].credits), x + 360, y, 12, colCredit);
        break;
    }
    case ROW_MECH: {
        const CatalogEntry* e = &catalog[r.index];
        const MechModel* mm = &mechModels[e->chassis];
        DrawText(TextFormat("%s %s", mm->designation, mm->name), x, y, 14, mm->accent);
        DrawText(TextFormat("FW %s", firmwareLabel(e->level)), x + 200, y, 12, colText);
        DrawText(TextFormat("%d CR", e->price), x + 340, y, 12, credits >= e->price ? colCredit : colBad);
        break;
    }
    case ROW_WEAPON: case ROW_MODULE: case ROW_CHIP: {
        const char* tag = r.kind == ROW_WEAPON ? "WPN" : r.kind == ROW_MODULE ? "REFIT" : "CHIP";
        const char* name = r.kind == ROW_WEAPON ? weaponTable[r.index].name
            : r.kind == ROW_MODULE ? refitModules[r.index].name : chipDefs[r.index].name;
        int price = r.kind == ROW_WEAPON ? weaponPrice(r.index) : r.kind == ROW_MODULE ? modulePrice(r.index) : chipPrice(r.index);
        int owned = r.kind == ROW_WEAPON ? weaponOwned[r.index] : r.kind == ROW_MODULE ? moduleOwned[r.index] : chipOwned[r.index];
        DrawText(tag, x, y + 1, 10, colDim);
        DrawText(name, x + 44, y, 12, r.kind == ROW_WEAPON ? munitionColor(weaponTable[r.index].munition) : WHITE);
        DrawText(TextFormat("x%d", owned), x + 290, y, 12, owned ? colText : colDim);
        DrawText(TextFormat("%d CR", price), x + 340, y, 12, credits >= price ? colCredit : colBad);
        break;
    }
    case ROW_REPAIR:
        DrawText("REPAIR TEAM", x, y, 14, WHITE);
        DrawText(TextFormat("%d CR", teamRepairCost()), x + 340, y, 12, colCredit);
        break;
    case ROW_SAVE: DrawText("SAVE GAME", x, y, 14, WHITE); break;
    default:       DrawText("EXIT TERMINAL (autosaves)", x, y, 14, WHITE); break;
    }
}

static void drawMechStats(const Mech* m, int x, int y) {
    const MechStats* s = &m->stats;
    DrawText(TextFormat("%s / %s", classNames[mechClass(m)], roleName(mechRole(m))), x, y, 12, colText);
    DrawText(TextFormat("INT %d  ARM %d  PWR %.2fx", s->maxIntegrity, s->maxArmor, s->power), x, y + 18, 12, colText);
    DrawText(TextFormat("MOB %d%%  EN %d  ACC %d%%  STB %d%%", s->mobility, s->maxEnergy, s->accuracy, s->stability), x, y + 34, 12, colText);
    DrawText(TextFormat("HEAT %d  COOL %d", s->maxHeat, s->cooling), x, y + 50, 12, colText);
    for (int i = 0; i < MAX_WEAPONS; i++) {
        const Weapon* w = mechWeapon(m, i);
        if (w) DrawText(TextFormat("- %s (%d/5)", w->name, m->weapons[i].rating), x, y + 72 + i * 14, 12, munitionColor(w->munition));
    }
}

static void drawDetail(int x, int y, int w) {
    if (numRows == 0) { DrawText("Nothing here.", x, y, 14, colDim); return; }
    Row r = rows[sel];
    switch (r.kind) {
    case ROW_JOB: {
        const JobDef* d = &jobDefs[r.index];
        DrawText(d->title, x, y, 20, WHITE);
        DrawText(TextFormat("CLIENT  %s", d->client), x, y + 26, 12, colHead);
        int ny = drawWrapped(d->brief, x, y + 48, w, 12, colText);
        drawWrapped(TextFormat("OBJECTIVE: %s", jobObjective(r.index)), x, ny + 8, w, 12, colCredit);
        DrawText(TextFormat("REWARD  %d CR", d->credits), x, ny + 56, 14, colCredit);
        if (d->reward) drawWrapped(TextFormat("+ %s", rewardName(d->reward, d->rewardItem)), x, ny + 76, w, 12, colGood);
        const char* hint = jobState[r.index] == JS_OPEN ? TextFormat("[Z] Accept  (%d/%d active)", jobsActive(), MAX_ACTIVE_JOBS)
            : jobState[r.index] == JS_READY ? "[Z] Claim reward" : "In progress";
        DrawText(hint, x, y + 380, 14, colHead);
        break;
    }
    case ROW_MECH: {
        const CatalogEntry* e = &catalog[r.index];
        if (previewEntry != r.index) { preview = mechCreateStock(e->chassis, e->level); previewEntry = r.index; }
        const MechModel* mm = &mechModels[e->chassis];
        DrawText(TextFormat("%s %s", mm->designation, mm->name), x, y, 20, mm->accent);
        drawWrapped(mm->desc, x, y + 26, w, 12, colText);
        drawMechBattle(e->chassis, x + w - 50, y + 90, 6, 0);
        drawMechStats(&preview, x, y + 60);
        DrawText(TextFormat("PRICE %d CR   FW %s", e->price, firmwareLabel(e->level)), x, y + 220, 14, colCredit);
        DrawText(TextFormat("Team %d/%d", teamSize, MAX_TEAM), x, y + 242, 12, teamSize < MAX_TEAM ? colText : colBad);
        DrawText("Its parts join your inventory.", x, y + 260, 12, colDim);
        DrawText("[Z] Buy", x, y + 380, 14, colHead);
        break;
    }
    case ROW_WEAPON: {
        const Weapon* wp = &weaponTable[r.index];
        DrawText(wp->name, x, y, 20, munitionColor(wp->munition));
        DrawText(TextFormat("%s / %s / %s", platformNames[wp->platform], munitionNames[wp->munition], targetingNames[wp->targeting]),
            x, y + 26, 12, colText);
        DrawText(TextFormat("DMG %d  ACC %d%%  PEN %d%%  COST %d EN", wp->baseDamage, wp->accuracy, wp->armorPen, wp->energyCost),
            x, y + 46, 12, colText);
        DrawText(TextFormat("HEAT +%d  SCRAMBLE %d  AMMO %s", wp->heat, wp->scramble, wp->ammo ? TextFormat("%d", wp->ammo) : "INF"),
            x, y + 62, 12, colText);
        DrawText("Mount rating per class (HA / A / R / EW):", x, y + 88, 12, colDim);
        DrawText(TextFormat("%d / %d / %d / %d", weaponRating(CLASS_HEAVY_ASSAULT, r.index), weaponRating(CLASS_ARTILLERY, r.index),
            weaponRating(CLASS_RECON, r.index), weaponRating(CLASS_EW, r.index)), x, y + 104, 14, WHITE);
        DrawText(TextFormat("Owned %d, free %d", weaponOwned[r.index], weaponAvailable(r.index)), x, y + 132, 12, colText);
        DrawText(TextFormat("[Z] Buy for %d CR", weaponPrice(r.index)), x, y + 380, 14, colHead);
        break;
    }
    case ROW_MODULE: {
        const RefitModule* m = &refitModules[r.index];
        DrawText(m->name, x, y, 20, WHITE);
        DrawText(TextFormat("%s REFIT", refitSlotNames[m->slot]), x, y + 26, 12, colHead);
        drawWrapped(m->desc, x, y + 46, w, 12, colText);
        DrawText("Rating per class (HA / A / R / EW):", x, y + 88, 12, colDim);
        DrawText(TextFormat("%d / %d / %d / %d", m->rating[0], m->rating[1], m->rating[2], m->rating[3]), x, y + 104, 14, WHITE);
        DrawText(TextFormat("Owned %d, free %d", moduleOwned[r.index], moduleAvailable(r.index)), x, y + 132, 12, colText);
        DrawText(TextFormat("[Z] Buy for %d CR", modulePrice(r.index)), x, y + 380, 14, colHead);
        break;
    }
    case ROW_CHIP: {
        const ChipDef* c = &chipDefs[r.index];
        DrawText(c->name, x, y, 18, (Color) { 200, 170, 255, 255 });
        DrawText(TextFormat("%s  %s  CAPACITY %d", chipCategoryName(c->category), chipRarityName(c->rarity), c->cost),
            x, y + 26, 12, colHead);
        drawWrapped(c->desc, x, y + 46, w, 12, colText);
        drawWrapped(c->plain, x, y + 66, w, 12, colDim);
        DrawText(TextFormat("Owned %d, free %d", chipOwned[r.index], chipAvailable(r.index)), x, y + 160, 12, colText);
        DrawText(TextFormat("[Z] Buy for %d CR", chipPrice(r.index)), x, y + 380, 14, colHead);
        break;
    }
    case ROW_REPAIR:
        DrawText("REPAIR BAY", x, y, 20, WHITE);
        drawWrapped(TextFormat("Armor is replated free after every battle. Integrity costs 1 CR per %d points; "
            "with too few credits the active mech is repaired first.", REPAIR_INT_PER_CREDIT), x, y + 26, w, 12, colText);
        for (int i = 0; i < teamSize; i++) {
            const MechStats* s = &team[i].stats;
            DrawText(TextFormat("%-12s INT %d/%d", team[i].name, s->integrity, s->maxIntegrity), x, y + 90 + i * 18, 12,
                s->integrity < s->maxIntegrity ? colCredit : colText);
        }
        break;
    case ROW_SAVE:
        DrawText("SAVE GAME", x, y, 20, WHITE);
        drawWrapped("Saves team, inventory, credits, jobs, defeated encounters and your position. "
            "Load it from the main menu.", x, y + 26, w, 12, colText);
        break;
    default:
        DrawText("EXIT", x, y, 20, WHITE);
        drawWrapped("Back to the sector. Leaving the terminal saves automatically.", x, y + 26, w, 12, colText);
        break;
    }
}

void uiTerminalDraw(void) {
    ClearBackground((Color) { 8, 12, 22, 255 });
    DrawRectangle(0, 0, screenW, 60, (Color) { 15, 25, 45, 255 });
    DrawRectangleLines(0, 0, screenW, 60, (Color) { 100, 200, 255, 220 });
    BeginMode2D(layoutCamera());
    DrawText(TextFormat(">> TERMINAL  %s", worldCurrentZoneName()), 30, 18, 26, colHead);
    DrawText(TextFormat("%d CR", credits), SCREEN_W - 150, 20, 24, colCredit);

    for (int i = 0; i < NUM_TABS; i++) drawButton(tabRect(i), tabNames[i], 14, i == tab, 1);
    if (tab == TAB_SHOWROOM)
        DrawText(TextFormat("%s - %s", companies[worldCurrentZone()].name, companies[worldCurrentZone()].tagline),
            30, 100, 10, companies[worldCurrentZone()].color);

    for (int i = 0; i < VISIBLE_ROWS && scroll + i < numRows; i++) drawRow(i, rows[scroll + i], scroll + i == sel);
    if (numRows > VISIBLE_ROWS)
        DrawText(TextFormat("%d-%d of %d", scroll + 1, scroll + VISIBLE_ROWS < numRows ? scroll + VISIBLE_ROWS : numRows, numRows),
            400, LIST_Y + VISIBLE_ROWS * ROW_H, 10, colDim);

    DrawRectangle(490, LIST_Y, 290, 420, (Color) { 15, 25, 45, 220 });
    DrawRectangleLines(490, LIST_Y, 290, 420, (Color) { 80, 160, 220, 200 });
    drawDetail(502, LIST_Y + 10, 266);

    DrawText(status, 30, SCREEN_H - 50, 14, statusGood ? colGood : colBad);
    DrawText("[A/D] Tab   [W/S] Select   [Z/ENTER] Confirm   [ESC] Leave", 30, SCREEN_H - 26, 16, (Color) { 150, 220, 255, 220 });
    EndMode2D();
}
