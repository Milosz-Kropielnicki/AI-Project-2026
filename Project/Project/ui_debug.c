#include "ui.h"
#include "mech.h"
#include "battle.h"
#include <stdio.h>
#include <math.h>

// Debug inspector: every stat layer and every formula step, computed by the
// same functions combat uses (mechStatBreakdown, attackPreview, hackChance...).
//
// Subjects/targets: the team, the current battle enemy (if opened from a
// battle) and one baseline dummy per model.

#define MAX_POOL (MAX_TEAM + 1 + NUM_MODELS)
static Mech* pool[MAX_POOL];
static char poolTag[MAX_POOL][16];
static int poolSize = 0;
static Mech dummies[NUM_MODELS];
static int subject = 0, target = 0;
static GameState returnState = STATE_OVERWORLD;

static const Color colHead = { 120, 230, 255, 255 };
static const Color colText = { 210, 225, 240, 255 };
static const Color colDim = { 130, 150, 175, 255 };
static const Color colGood = { 120, 255, 160, 255 };
static const Color colBad = { 255, 120, 110, 255 };
static const Color colFw = { 200, 170, 255, 255 };

void uiDebugOpen(GameState returnTo) {
    returnState = returnTo;
    poolSize = 0;
    for (int i = 0; i < teamSize; i++) {
        pool[poolSize] = &team[i];
        snprintf(poolTag[poolSize++], sizeof(poolTag[0]), "TEAM %d", i + 1);
    }
    if (returnTo == STATE_BATTLE) {
        pool[poolSize] = &battle.enemyMech;
        snprintf(poolTag[poolSize++], sizeof(poolTag[0]), "ENEMY");
    }
    for (int i = 0; i < NUM_MODELS; i++) {
        dummies[i] = mechCreate(i, 0);
        snprintf(dummies[i].name, sizeof(dummies[i].name), "%s", mechModels[i].name);
        pool[poolSize] = &dummies[i];
        snprintf(poolTag[poolSize++], sizeof(poolTag[0]), "DUMMY");
    }
    if (subject >= poolSize) subject = 0;
    target = (returnTo == STATE_BATTLE) ? teamSize : (subject + 1) % poolSize;
    if (target >= poolSize) target = 0;
}

static int isBattleEnemy(const Mech* m) { return returnState == STATE_BATTLE && m == &battle.enemyMech; }

void uiDebugUpdate(GameState* state) {
    if (IsKeyPressed(KEY_F1) || IsKeyPressed(KEY_ESCAPE)) {
        consumeInput();
        // Refits / revisions made here must reach the live battle numbers
        if (returnState == STATE_BATTLE) {
            mechStats(battle.player.mech, &battle.player.stats);
            mechStats(battle.enemy.mech, &battle.enemy.stats);
        }
        *state = returnState;
        return;
    }
    if (LEFT_PRESSED)  subject = (subject + poolSize - 1) % poolSize;
    if (RIGHT_PRESSED) subject = (subject + 1) % poolSize;
    if (UP_PRESSED)    target = (target + poolSize - 1) % poolSize;
    if (DOWN_PRESSED)  target = (target + 1) % poolSize;

    Mech* m = pool[subject];
    if (isBattleEnemy(m)) return;   // the live enemy is read-only
    for (int slot = 0; slot < NUM_REFIT_SLOTS; slot++) {
        if (!IsKeyPressed(KEY_ONE + slot)) continue;
        int cur = m->refit[slot];
        for (int k = 1; k <= NUM_REFIT_MODULES; k++) {
            int idx = (cur + k) % NUM_REFIT_MODULES;
            if (refitModules[idx].slot == (RefitSlot)slot) { mechSetRefit(m, (RefitSlot)slot, idx); break; }
        }
    }
    if (IsKeyPressed(KEY_R) && m->fw.revision < MAX_REVISION) {
        firmwareAddData(&m->fw, firmwareDataToNext(m->fw.revision) - m->fw.data);
        firmwareAutoSpend(&m->fw);
    }
    if (IsKeyPressed(KEY_T)) mechRepair(m);
}

// ============ DRAW HELPERS ============
static void text(const char* s, int x, int y, int size, Color c) { DrawText(s, x, y, size, c); }

static const char* signedStat(StatId s, float v) {
    if (v == 0) return ".";
    if (s == STAT_POWER) return TextFormat("%+.2f", v);
    return TextFormat("%+d", (int)roundf(v));
}

static void drawStatTable(const Mech* m, int x, int y) {
    StatBreakdown b;
    mechStatBreakdown(m, &b);
    MechClass cls = mechClass(m);
    const int col[] = { 0, 90, 150, 210, 270, 345, 405, 475, 545 };
    text("STAT", x + col[0], y, 10, colHead);
    for (int l = 0; l < NUM_STAT_LAYERS; l++) text(statLayerNames[l], x + col[1 + l], y, 10, colHead);
    text("SUM", x + col[6], y, 10, colHead);
    text("FINAL", x + col[7], y, 10, colHead);
    text(TextFormat("CLASS (%s)", classInitials[cls]), x + col[8], y, 10, colDim);
    for (int s = 0; s < NUM_STATS; s++) {
        int ry = y + 14 + s * 13;
        text(statName((StatId)s), x + col[0], ry, 10, colText);
        text(statFormat((StatId)s, b.layer[LAYER_ROLE].v[s]), x + col[1], ry, 10, colText);
        for (int l = 1; l < NUM_STAT_LAYERS; l++) {
            float v = b.layer[l].v[s];
            text(signedStat((StatId)s, v), x + col[1 + l], ry, 10, v > 0 ? colGood : v < 0 ? colBad : colDim);
        }
        text(s == STAT_POWER ? TextFormat("%.3f", b.sum.v[s]) : TextFormat("%.1f", b.sum.v[s]), x + col[6], ry, 10, colDim);
        int clamped = fabsf(b.sum.v[s] - b.final.v[s]) > 0.001f;
        text(statFormat((StatId)s, b.final.v[s]), x + col[7], ry, 10, WHITE);
        if (clamped) text("*", x + col[7] + 44, ry, 10, colBad);
        text(statFormat((StatId)s, classBaseline[cls].v[s]), x + col[8], ry, 10, colDim);
    }
    text("FINAL = clamp(ROLE + MODEL + REFIT + FIRMWARE + CHIPS)   * = clamped / rounded to attribute range",
        x, y + 14 + NUM_STATS * 13 + 2, 10, colDim);
}

static void drawRefit(const Mech* m, int x, int y) {
    text("CHASSIS REFIT  [1-4] cycle", x, y, 10, colHead);
    for (int slot = 0; slot < NUM_REFIT_SLOTS; slot++) {
        int mod = m->refit[slot];
        int rating = refitRating(m, mod);
        text(TextFormat("%-5s %s", refitSlotNames[slot], refitModules[mod].name), x, y + 14 + slot * 13, 10, colText);
        text(TextFormat("%d/5 -> upside x%.2f", rating, refitRatingFactor(rating)), x + 190, y + 14 + slot * 13, 10,
            rating >= 4 ? colGood : rating >= 3 ? colText : colBad);
    }
}

static void drawFirmware(const Mech* m, int x, int y) {
    const Firmware* fw = &m->fw;
    text(TextFormat("FIRMWARE %s  [R] +1 revision", firmwareLabel(fw->revision)), x, y, 10, colHead);
    text(TextFormat("DATA %d/%d   SOCKETS %d   CAPACITY %d/%d   OPT PTS %d", fw->data, firmwareDataToNext(fw->revision),
        firmwareSockets(fw), firmwareCapacityUsed(fw), firmwareCapacity(fw), fw->optPoints), x, y + 14, 10, colFw);
    if (fw->revision < MAX_REVISION)
        text(TextFormat("NEXT %s: %s", firmwareLabel(fw->revision + 1), firmwareUnlockDesc(firmwareUnlockAt(fw->revision + 1))),
            x, y + 27, 10, colDim);
    int line = 0;
    for (int s = 0; s < firmwareSockets(fw) && line < 3; s++) {
        int c = fw->chips[s];
        if (c < 0) continue;
        text(TextFormat("[%d] %s (%d) = %g", s + 1, chipDefs[c].name, chipDefs[c].cost, chipDefs[c].value),
            x, y + 40 + line * 13, 10, colText);
        line++;
    }
    if (line == 0) text("no chips installed", x, y + 40, 10, colDim);
}

static void drawAttack(const Mech* a, const Stats* as, int mount, const Mech* d, const Stats* ds, int x, int y) {
    const WeaponDef* w = mechWeapon(a, mount);
    if (!w) {
        text(TextFormat("[%d] -- empty mount --", mount + 1), x, y, 10, colDim);
        return;
    }
    AttackContext ctx = attackContextBaseline(as, &a->fw, ds, &d->fw, d->armor);
    AttackPreview p;
    attackPreview(as, &a->fw, w, ds, &d->fw, &ctx, &p);

    text(TextFormat("[%d] %s  %s / %s   COST %d   HEAT +%d   AMMO %s", mount + 1, w->name, platformNames[w->platform],
        munitionNames[w->munition], w->energyCost, p.heat, w->ammo > 0 ? TextFormat("%d", w->ammo) : "INF"),
        x, y, 10, munitionColor(w->munition));
    text(TextFormat("HIT  %.0f%% x ACC %.0f/100=%.2f x (1 - MOB %.0f/200)=%.2f = %.1f%%  x spoof %.2f  -> clamp %.1f%%",
        p.weaponAcc, ctx.attackerAccuracy, p.accMod, ctx.targetMobility, p.evasionMod, p.hitUnclamped * 100, p.spoofMod,
        p.hitChance * 100), x + 12, y + 12, 10, colText);
    text(TextFormat("DMG  %.0f x PWR %.2f = %.1f  x mod %.2f = %.1f   pen %.0f%% -> ARM %.1f (x%.2f breach)  INT %.1f",
        p.baseDamage, p.power, p.raw, p.damageMod, p.modified, p.pen * 100, p.toArmor, p.breachMod, p.toIntegrity),
        x + 12, y + 24, 10, colText);
    float expected = (p.armorDamage + p.integrityDamage) * p.hitChance;
    text(TextFormat("vs ARMOR %d: absorbed %d, spill %d -> ARM -%d  INT -%d   expected %.1f/shot%s",
        p.armorBefore, p.armorDamage, p.spill, p.armorDamage, p.integrityDamage, expected,
        p.scramble > 0 ? TextFormat("   SCRAMBLE %d resist %.1f%%", p.scramble, p.resist * 100) : ""),
        x + 12, y + 36, 10, colGood);
}

void uiDebugDraw(void) {
    ClearBackground((Color) { 6, 10, 18, 255 });
    BeginMode2D(layoutCamera());
    Mech* a = pool[subject];
    Mech* d = pool[target];
    Stats as, ds;
    mechStats(a, &as);
    mechStats(d, &ds);

    text("DEBUG // STATS & FORMULAS", 20, 8, 18, colHead);
    text(TextFormat("[A/D] SUBJECT  %s  %s  (%s %s)  INT %d/%d  ARM %d/%d", poolTag[subject], a->name,
        mechModel(a)->designation, roleNames[mechModel(a)->role], a->integrity, (int)as.v[STAT_INTEGRITY], a->armor,
        (int)as.v[STAT_ARMOR]), 20, 32, 10, WHITE);
    text(TextFormat("[W/S] TARGET   %s  %s  (%s %s)  INT %d/%d  ARM %d/%d", poolTag[target], d->name,
        mechModel(d)->designation, roleNames[mechModel(d)->role], d->integrity, (int)ds.v[STAT_INTEGRITY], d->armor,
        (int)ds.v[STAT_ARMOR]), 20, 45, 10, colDim);

    drawStatTable(a, 20, 64);
    drawRefit(a, 20, 212);
    drawFirmware(a, 400, 212);

    int wy = 290;
    text(TextFormat("ATTACK RESOLUTION: %s -> %s  (first attack of a round; chips included)", a->name, d->name), 20, wy, 10, colHead);
    for (int i = 0; i < MAX_WEAPONS; i++) drawAttack(a, &as, i, d, &ds, 20, wy + 14 + i * 50);

    int my = wy + 14 + MAX_WEAPONS * 50 + 2;
    float maxI = ds.v[STAT_INTEGRITY];
    int rarity = mechModel(d)->rarity < 1 ? 3 : mechModel(d)->rarity;
    text(TextFormat("HACK %s: 0.25 + (1 - %d/%.0f) x 0.55 - (rarity %d - 1) x 0.08 - (STB %.0f - 60) x 0.003 = %.1f%%",
        d->name, d->integrity, maxI, rarity, ds.v[STAT_STABILITY], hackChance(d, &ds) * 100), 20, my, 10, colText);
    float stab = as.v[STAT_STABILITY] + firmwareChipTotal(&a->fw, CFX_COUNTER_INTRUSION);
    text(TextFormat("SCRAMBLE RESIST %s: STB %.0f / (STB + STR)   str 40: %.1f%%   70: %.1f%%   100: %.1f%%", a->name, stab,
        formulaScrambleResist(stab, 40) * 100, formulaScrambleResist(stab, 70) * 100, formulaScrambleResist(stab, 100) * 100),
        20, my + 13, 10, colText);
    text(TextFormat("DATA for scrapping %s: wild 10 + rev %d x 4 + rand(0-3)   trainer 15 + rev x 5 + tier x 8 + rand(0-4)  ENERGY %d/round  COOLING %d/round",
        d->name, d->fw.revision, (int)as.v[STAT_ENERGY], (int)as.v[STAT_COOLING]), 20, my + 26, 10, colText);

    text("[A/D] subject  [W/S] target  [1-4] refit  [R] revision  [T] repair  [F1/ESC] close", 20, SCREEN_H - 18, 12, colHead);
    EndMode2D();
}
