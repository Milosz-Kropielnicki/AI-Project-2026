#include "battle.h"
#include "world.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

Battle battle;

static float frand(void) { return (float)rand() / ((float)RAND_MAX + 1.0f); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ============ FORMULAS ============
float formulaHitUnclamped(int weaponAcc, int accuracy, int mobility) {
    return (weaponAcc / 100.0f) * (accuracy / 100.0f) * (1.0f - mobility / 200.0f);
}

float formulaHitChance(int weaponAcc, int accuracy, int mobility) {
    return clampf(formulaHitUnclamped(weaponAcc, accuracy, mobility), HIT_CHANCE_MIN, HIT_CHANCE_MAX);
}

float formulaRawDamage(int baseDamage, float power) { return baseDamage * power; }

DamageSplit formulaDamageSplit(float raw, int penPercent, int currentArmor) {
    DamageSplit s;
    float pen = clampi(penPercent, 0, 100) / 100.0f;
    s.toIntegrity = raw * pen;
    s.toArmor = raw * (1.0f - pen);
    int armorHit = (int)roundf(s.toArmor);
    s.armorDamage = armorHit < currentArmor ? armorHit : currentArmor;
    s.spill = armorHit - s.armorDamage;
    s.integrityDamage = (int)roundf(s.toIntegrity) + s.spill;
    return s;
}

int formulaHeatAfterCooling(int heat, int cooling) { return heat > cooling ? heat - cooling : 0; }

float formulaScrambleResist(int stability, int strength) {
    if (stability + strength <= 0) return 1.0f;
    return (float)stability / (float)(stability + strength);
}

// Chips, trait and branches together
static float fwEffect(const Mech* m, ChipEffect e) { return firmwareEffect(&m->fw, e); }

static int weaponHeat(const Mech* m, const Weapon* w) {
    float heat = (float)w->heat * (1.0f + fwEffect(m, CFX_HEAT_MULT));                   // Siege / Overclock Kernel
    if (w->munition == MUN_ENERGY) {
        heat *= 1.0f - fwEffect(m, CFX_ENERGY_HEAT_CUT);                                 // Thermal Optimization
        if (fwEffect(m, CFX_OVERCHARGE) > 0) heat *= 1.5f;                               // Overcharge Routine
    }
    return (int)roundf(heat);
}

static int weaponCost(const Mech* m, const Weapon* w, int firstAction) {
    if (firstAction && fwEffect(m, CFX_FIRST_ACTION_FREE) > 0) return 0;   // Efficient Power Distribution
    return w->energyCost;
}

void attackPreview(const Mech* attacker, const Weapon* w, const Mech* target,
                   const AttackContext* ctx, AttackPreview* out) {
    const MechStats* atk = &attacker->stats;
    const MechStats* def = &target->stats;
    memset(out, 0, sizeof(*out));

    // Hit chance
    out->weaponAcc = w->accuracy;
    out->accuracy = ctx->attackerAccuracy;
    out->mobility = ctx->targetMobility;
    out->accMod = out->accuracy / 100.0f;
    out->evasionMod = 1.0f - out->mobility / 200.0f;
    out->hitUnclamped = formulaHitUnclamped(w->accuracy, out->accuracy, out->mobility);
    out->spoofMod = ctx->spoofActive ? 1.0f - fwEffect(target, CFX_TARGETING_SPOOF) : 1.0f;
    out->hitChance = clampf(out->hitUnclamped * out->spoofMod, HIT_CHANCE_MIN, HIT_CHANCE_MAX);

    // Raw damage, with the firmware multipliers
    out->baseDamage = w->baseDamage;
    out->power = atk->power;
    if (atk->integrity < atk->maxIntegrity * 0.5f) out->power += fwEffect(attacker, CFX_POWER_WHEN_DAMAGED);   // Aggressive Kernel
    out->executeMod = def->integrity < def->maxIntegrity * 0.5f ? 1.0f + fwEffect(attacker, CFX_EXECUTE) : 1.0f;
    out->overchargeMod = w->munition == MUN_ENERGY ? 1.0f + fwEffect(attacker, CFX_OVERCHARGE) : 1.0f;
    out->reductionMod = 1.0f - fwEffect(target, CFX_DAMAGE_REDUCTION);
    out->firstHitMod = ctx->firstHitOnTarget ? 1.0f - fwEffect(target, CFX_FIRST_HIT_SHIELD) : 1.0f;
    out->adaptiveMod = ctx->targetLastMunition == w->munition ? 1.0f - fwEffect(target, CFX_ADAPTIVE) : 1.0f;
    out->formatMod = ctx->formatMod > 0 ? ctx->formatMod : 1.0f;
    out->splashMod = ctx->splashMod > 0 ? ctx->splashMod : 1.0f;
    out->dmgMod = out->executeMod * out->overchargeMod * out->reductionMod * out->firstHitMod * out->adaptiveMod
        * out->formatMod * out->splashMod;
    out->raw = formulaRawDamage(w->baseDamage, out->power) * out->dmgMod;
    out->pen = w->armorPen + (int)fwEffect(attacker, CFX_PEN_BONUS);                     // Siege Kernel
    if (def->armor > ARMORED_THRESHOLD) out->pen += (int)roundf(fwEffect(attacker, CFX_PEN_VS_ARMORED) * 100);   // Armor Analysis
    out->pen = clampi(out->pen, 0, 100);
    out->armorBefore = def->armor;
    out->split = formulaDamageSplit(out->raw, out->pen, def->armor);
    if (ctx->breachReady && def->armor > 0) {   // Armor Breach Routine
        int bonus = (int)roundf(out->split.toArmor * fwEffect(attacker, CFX_ARMOR_BREACH));
        int room = def->armor - out->split.armorDamage;
        out->breachBonus = bonus < room ? bonus : room;
    }
    out->armorDamage = out->split.armorDamage + out->breachBonus;
    out->integrityDamage = out->split.integrityDamage;

    // Resources and scramble
    out->energyCost = weaponCost(attacker, w, ctx->firstAction) + ctx->energyTax;
    out->energyBefore = atk->energy;
    out->heat = weaponHeat(attacker, w);
    out->heatBefore = atk->heat;
    out->maxHeat = atk->maxHeat;
    out->scramble = w->scramble > 0 ? w->scramble + (int)fwEffect(attacker, CFX_SCRAMBLE_BONUS) : 0;   // Signal Kernel
    out->resist = out->scramble > 0
        ? formulaScrambleResist(def->stability + (int)fwEffect(target, CFX_COUNTER_INTRUSION), out->scramble)
        : 1.0f;
}

AttackContext attackContextBaseline(const Mech* attacker, const Mech* target) {
    AttackContext c;
    c.attackerAccuracy = clampi(attacker->stats.accuracy + (int)fwEffect(attacker, CFX_PRECISION_STRIKE), 0, 100);
    c.targetMobility = target->stats.mobility;
    c.spoofActive = fwEffect(target, CFX_TARGETING_SPOOF) > 0;
    c.breachReady = fwEffect(attacker, CFX_ARMOR_BREACH) > 0;
    c.firstAction = 1;
    c.firstHitOnTarget = 1;
    c.targetLastMunition = -1;
    c.energyTax = 0;
    c.targetScrambled = 0;
    c.formatMod = 1.0f;
    c.splashMod = 1.0f;
    return c;
}

// ============ ENEMY AI ============
float aiScoreAttack(const Mech* attacker, const Weapon* w, const Mech* target,
                    const AttackContext* ctx, const AIProfile* ai) {
    AttackPreview p;
    attackPreview(attacker, w, target, ctx, &p);
    const MechStats* as = &attacker->stats;
    const MechStats* ds = &target->stats;

    // Armor vs penetration: stripping Armor is worth less than Integrity damage
    float value = ai->damage * (p.armorDamage * ai->armorBias + p.integrityDamage) * p.hitChance;
    if (p.integrityDamage >= ds->integrity) value += ai->finisher * 40.0f * p.hitChance;

    // Stability: a scramble is only worth what gets past the target's resistance,
    // corruption scales with the chips it can hit, and stacking more effects on
    // an already-scrambled target is worth progressively less.
    if (p.scramble > 0) {
        int chips = 0;
        for (int i = 0; i < MAX_SOCKETS; i++) if (firmwareChipSign(&target->fw, i) > 0) chips++;
        float payload = w->virus ? 10.0f + 5.0f * chips
            : p.scramble >= 100 ? 30.0f : p.scramble >= 70 ? 18.0f : p.scramble >= 40 ? 12.0f : 6.0f;
        float weight = ai->scramble * (as->integrity < as->maxIntegrity * 0.5f ? ai->desperation : 1.0f);
        float stacked = 1.0f + ctx->targetScrambled;
        value += weight * payload * p.hitChance * (1.0f - p.resist) / (stacked * stacked);
    }

    // Energy: value per point spent (a free action counts as half a point)
    value /= p.energyCost > 0 ? (float)p.energyCost : 0.5f;

    // Heat: past half capacity (after next turn's cooling), cautious machines back off
    float after = (float)(as->heat + p.heat - as->cooling) / (float)(as->maxHeat > 0 ? as->maxHeat : 1);
    if (after > 0.5f) value *= 1.0f - ai->heatCaution * clampf((after - 0.5f) / 0.5f, 0, 1);
    return value;
}

// Hacking (capture): easier on damaged, common, low-Stability machines
int hackStrength(const Mech* hacker) {
    int best = 0;
    for (int i = 0; i < MAX_WEAPONS; i++) {
        const Weapon* w = mechWeapon(hacker, i);
        if (w && w->scramble > best) best = w->scramble;
    }
    return HACK_BASE_STRENGTH + best / 2 + (int)fwEffect(hacker, CFX_SCRAMBLE_BONUS);
}

float hackChance(int strength, int stability) {
    if (stability < 0) stability = 0;
    return clampf((float)strength / (float)(strength + stability > 0 ? strength + stability : 1), 0.05f, 0.95f);
}

// Revision Data (doc 7.3): destroying machines, fighting higher-revision
// enemies (bonus per step above the player), and simply taking part.
static int levelGapBonus(const Mech* enemy) {
    int sum = 0, n = 0;   // against the average revision of the player's mechs that took the field
    const Side* sd = &battle.side[SIDE_PLAYER];
    for (int i = 0; i < sd->count; i++) if (sd->slot[i].fielded) { sum += sd->mech[i].fw.revision; n++; }
    int gap = enemy->fw.revision - (n > 0 ? sum / n : 0);
    return gap > 0 ? gap * 3 : 0;
}
int revisionDataForWild(const Mech* enemy) { return 10 + enemy->fw.revision * 4 + levelGapBonus(enemy) + rand() % 4; }
int revisionDataForTrainer(const Mech* enemy, int tier) {
    return 15 + enemy->fw.revision * 5 + tier * 8 + levelGapBonus(enemy) + rand() % 5;
}
int revisionDataForParticipation(const Mech* enemy) { return 5 + enemy->fw.revision * 2; }

// ============ COMBATANTS ============
// ============ BATTLE LOG ============
static LogEntry logHistory[LOG_HISTORY];
static int logHead = 0, logSize = 0;
static char lastLogged[256] = "";

int battleLogCount(void) { return logSize; }
const LogEntry* battleLogEntry(int back) {
    if (back < 0 || back >= logSize) return NULL;
    return &logHistory[(logHead - 1 - back + LOG_HISTORY) % LOG_HISTORY];
}

static LogEntry* logPush(const char* text, int side, int munition) {
    LogEntry* e = &logHistory[logHead];
    memset(e, 0, sizeof(*e));
    snprintf(e->text, sizeof(e->text), "%s", text);
    e->side = side;
    e->munition = munition;
    e->round = battle.round;
    logHead = (logHead + 1) % LOG_HISTORY;
    if (logSize < LOG_HISTORY) logSize++;
    snprintf(lastLogged, sizeof(lastLogged), "%s", text);
    return e;
}

// System messages are written straight into battle.log all over this file;
// anything new there is copied into the history.
static void syncLog(void) {
    if (battle.log[0] && strcmp(battle.log, lastLogged) != 0) logPush(battle.log, -1, -1);
}

static void logClear(void) { logHead = logSize = 0; lastLogged[0] = 0; }

static int integrityBelow(const Combatant* c, float fraction) {
    return c->mech->stats.integrity < c->mech->stats.maxIntegrity * fraction;
}

int combatAccuracy(const Combatant* c) {
    int acc = c->mech->stats.accuracy - c->accPenalty;
    if (c->actionsThisTurn == 0) acc += (int)fwEffect(c->mech, CFX_PRECISION_STRIKE);
    acc += c->missStacks * (int)fwEffect(c->mech, CFX_RECURSIVE_TARGETING);
    return clampi(acc, 0, 100);
}

int combatMobility(const Combatant* c) {
    int mob = c->mech->stats.mobility + c->evasiveBonus;
    if (integrityBelow(c, 0.25f)) mob += (int)fwEffect(c->mech, CFX_EMERGENCY_EVASION);
    return clampi(mob, 0, 100);
}

// Scramble / corruption effects waiting on this side's next turn
static int pendingScrambles(const Combatant* c) {
    int n = c->nextSkipTurn + (c->nextDisabledWeapon >= 0) + (c->nextAccPenalty > 0) + c->nextEnergyLoss
        + c->nextEnergyTax + c->nextRandomTargeting;
    for (int i = 0; i < MAX_SOCKETS; i++) if (c->mech->fw.corrupt[i] > 0) n++;
    return n;
}

static AttackContext liveContext(const Combatant* a, const Combatant* d) {
    AttackContext ctx;
    ctx.attackerAccuracy = combatAccuracy(a);
    ctx.targetMobility = combatMobility(d);
    ctx.spoofActive = !d->attackedThisRound && fwEffect(d->mech, CFX_TARGETING_SPOOF) > 0;
    ctx.breachReady = !a->breachUsed && fwEffect(a->mech, CFX_ARMOR_BREACH) > 0;
    ctx.firstAction = a->actionsThisTurn == 0;
    ctx.firstHitOnTarget = !d->hitTaken;
    ctx.targetLastMunition = d->lastMunitionTaken;
    ctx.energyTax = a->energyTax;
    ctx.targetScrambled = 3 * d->nextSkipTurn + (d->nextDisabledWeapon >= 0) + (d->nextAccPenalty > 0) + d->nextEnergyLoss
        + d->nextEnergyTax + d->nextRandomTargeting;
    for (int i = 0; i < MAX_SOCKETS; i++) if (d->mech->fw.corrupt[i] > 0) ctx.targetScrambled++;
    ctx.formatMod = TEAM_DAMAGE_SCALE;
    ctx.splashMod = 1.0f;
    return ctx;
}

// ============ EXPLANATION ============
static void say(Explanation* x, int warn, const char* fmt, ...) {
    if (x->n >= EXPLAIN_LINES) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(x->line[x->n], EXPLAIN_LEN, fmt, args);
    va_end(args);
    x->warn[x->n++] = warn;
}

static const char* scrambleOutcome(int strength, int virus) {
    if (virus) return "corrupts target firmware";
    if (strength >= 100) return "costs the target a turn (or corrupts a chip)";
    if (strength >= 70) return "disables a weapon (or corrupts a chip)";
    if (strength >= 40) return "cuts target Accuracy by 15 (or corrupts a chip)";
    return "drains 1 Energy";
}

// Plain-language reasons for every number in p. Built from the same battle
// state attackPreview used, so the text always matches the result.
static void explainAttack(const Combatant* a, const Combatant* d, const Weapon* w, const AttackPreview* p, Explanation* x) {
    const MechStats* as = &a->mech->stats;
    const MechStats* ds = &d->mech->stats;
    x->n = 0;

    // ---- hit chance ----
    char accWhy[96] = "", mobWhy[96] = "";
    int precision = a->actionsThisTurn == 0 ? (int)fwEffect(a->mech, CFX_PRECISION_STRIKE) : 0;
    int recursive = a->missStacks * (int)fwEffect(a->mech, CFX_RECURSIVE_TARGETING);
    if (precision) snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", Precision Strike +%d", precision);
    if (recursive) snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", Recursive Targeting +%d", recursive);
    if (a->accPenalty) snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", scrambled -%d", a->accPenalty);
    if (d->evasiveBonus) snprintf(mobWhy + strlen(mobWhy), sizeof(mobWhy) - strlen(mobWhy), ", evading +%d", d->evasiveBonus);
    if (integrityBelow(d, 0.25f) && fwEffect(d->mech, CFX_EMERGENCY_EVASION) > 0)
        snprintf(mobWhy + strlen(mobWhy), sizeof(mobWhy) - strlen(mobWhy), ", Emergency Evasion +%d", (int)fwEffect(d->mech, CFX_EMERGENCY_EVASION));
    say(x, 0, "HIT %d%%: weapon %d%% x Accuracy %d%%%s", (int)roundf(p->hitChance * 100), p->weaponAcc, p->accuracy,
        accWhy[0] ? TextFormat(" (%s)", accWhy + 2) : "");
    say(x, 0, "   x target evasion: Mobility %d%%%s dodges %d%% of shots", p->mobility,
        mobWhy[0] ? TextFormat(" (%s)", mobWhy + 2) : "", (int)roundf(p->mobility / 2.0f));
    if (p->spoofMod < 1) say(x, 1, "   Targeting Spoof: first attack on target this round is x%.2f", p->spoofMod);
    if (p->hitUnclamped * p->spoofMod > HIT_CHANCE_MAX) say(x, 0, "   (capped at 95%% - nothing is certain)");
    if (p->hitUnclamped * p->spoofMod < HIT_CHANCE_MIN) say(x, 1, "   (floored at 5%% - a lucky shot is still possible)");

    // ---- damage ----
    if (p->baseDamage > 0) {
        char mods[112] = "";
        if (p->power > as->power + 0.001f) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", Aggressive Kernel +%.2f PWR", p->power - as->power);
        if (p->executeMod > 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", Hunter +%d%% (target under half)", (int)roundf((p->executeMod - 1) * 100));
        if (p->overchargeMod > 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", Overcharge +%d%%", (int)roundf((p->overchargeMod - 1) * 100));
        if (p->reductionMod < 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", target Bastion -%d%%", (int)roundf((1 - p->reductionMod) * 100));
        if (p->firstHitMod < 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", target Defensive Kernel -%d%%", (int)roundf((1 - p->firstHitMod) * 100));
        if (p->adaptiveMod < 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", adapted to %s -%d%%", munitionNames[w->munition], (int)roundf((1 - p->adaptiveMod) * 100));
        if (p->formatMod != 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", team battle x%.2f", p->formatMod);
        say(x, 0, "DAMAGE %.0f: %d base x %.2f Power%s", p->raw, p->baseDamage, p->power,
            p->dmgMod != 1 ? TextFormat(" x %.2f firmware", p->dmgMod) : "");
        if (mods[0]) say(x, 0, "   %s", mods + 2);
        if (p->splashMod < 1) say(x, 1, "   SPLASH: a secondary target of %s takes %d%% (-25%% per extra target)",
            targetingNames[w->targeting], (int)roundf(p->splashMod * 100));

        // ---- armor vs penetration ----
        int analysis = ds->armor > ARMORED_THRESHOLD ? (int)roundf(fwEffect(a->mech, CFX_PEN_VS_ARMORED) * 100) : 0;
        int siege = (int)fwEffect(a->mech, CFX_PEN_BONUS);
        const char* penWhy = analysis || siege ? TextFormat(" (weapon %d%%%s%s)", w->armorPen,
            analysis ? TextFormat(" + Armor Analysis %d", analysis) : "", siege ? TextFormat(" + Siege %d", siege) : "") : "";
        if (p->armorBefore <= 0)
            say(x, 0, "ARMOR: none left - all %.0f goes into Integrity", p->raw);
        else {
            say(x, 0, "PENETRATION %d%%%s: %.0f bypasses Armor, %.0f hits Armor", p->pen, penWhy, p->split.toIntegrity, p->split.toArmor);
            if (p->split.spill > 0) say(x, 1, "   Armor %d can't hold it: %d spills through to Integrity", p->armorBefore, p->split.spill);
            else if (p->pen < 25 && p->split.toArmor > p->split.toIntegrity * 2)
                say(x, 1, "   Mostly stopped by Armor - a higher-penetration weapon would hurt more");
            if (p->breachBonus > 0) say(x, 0, "   Armor Breach Routine: +%d extra Armor damage", p->breachBonus);
        }
        say(x, 0, "ON HIT: Armor -%d, Integrity -%d%s", p->armorDamage, p->integrityDamage,
            p->integrityDamage >= ds->integrity ? "  -> SCRAPS TARGET" : "");
    }

    // ---- scramble ----
    if (p->scramble > 0) {
        int stab = ds->stability + (int)fwEffect(d->mech, CFX_COUNTER_INTRUSION);
        say(x, 0, "SCRAMBLE %d vs target Stability %d: %d%% chance it lands, then it %s", p->scramble, stab,
            (int)roundf((1 - p->resist) * 100), scrambleOutcome(p->scramble, w->virus));
    }

    // ---- energy and heat ----
    const char* costWhy = p->energyCost == 0 ? " (first action free)" : a->energyTax ? TextFormat(" (+%d corruption)", a->energyTax) : "";
    int after = p->heatBefore + p->heat;
    float heatMult = w->heat > 0 ? (float)p->heat / w->heat : 1;
    say(x, after > p->maxHeat, "COST %d EN%s   HEAT +%d%s -> %d/%d%s", p->energyCost, costWhy, p->heat,
        heatMult > 1.01f || heatMult < 0.99f ? TextFormat(" (x%.2f)", heatMult) : "", after, p->maxHeat,
        after > p->maxHeat ? " OVER THE LIMIT" : "");
}

static int canFire(const Combatant* c, int mount, const char** reason) {
    const char* r = NULL;
    const MechStats* s = &c->mech->stats;
    const Weapon* w = mechWeapon(c->mech, mount);
    if (!w) r = "EMPTY MOUNT";
    else if (mount == c->disabledWeapon) r = "WEAPON SCRAMBLED";
    else if (w->ammo > 0 && c->mech->weapons[mount].ammo <= 0) r = "NO AMMO";
    else if (weaponCost(c->mech, w, c->actionsThisTurn == 0) + c->energyTax > s->energy) r = "INSUFFICIENT ENERGY";
    else if (s->heat + weaponHeat(c->mech, w) > s->maxHeat) r = "THERMAL LIMIT";
    if (reason) *reason = r;
    return r == NULL;
}

static int anyFireable(const Combatant* c) {
    for (int i = 0; i < MAX_WEAPONS; i++) if (canFire(c, i, NULL)) return 1;
    return 0;
}

// Random-targeting corruption: each attack has a 50% chance to fire a random
// usable weapon instead of the chosen one.
static int corruptedMount(const Combatant* c, int mount) {
    if (!c->randomTargeting || rand() % 2) return mount;
    int mounts[MAX_WEAPONS], n = 0;
    for (int i = 0; i < MAX_WEAPONS; i++) if (canFire(c, i, NULL)) mounts[n++] = i;
    return n > 0 ? mounts[rand() % n] : mount;
}

static void initCombatant(Combatant* c, Mech* m) {
    memset(c, 0, sizeof(*c));
    c->mech = m;
    c->disabledWeapon = -1;
    c->nextDisabledWeapon = -1;
    c->lastMunitionTaken = -1;
    c->archetype = -1;
    c->ai = &aiDefault;
}

// A mech entering the battle: clean firmware, cold, fully loaded
static void prepareForField(Mech* m) {
    firmwareClearCorruption(&m->fw);
    mechRefreshStats(m);
    m->stats.heat = 0;
    mechReloadWeapons(m);
}

// ============ SIDES ============
Combatant* battleField(int side, int pos) {
    Side* sd = &battle.side[side];
    if (pos < 0 || pos >= sd->numField || sd->field[pos] < 0) return NULL;
    return &sd->slot[sd->field[pos]];
}

int battleFieldCount(int side) {
    int n = 0;
    for (int p = 0; p < MAX_FIELD; p++) n += battleField(side, p) != NULL;
    return n;
}

static Combatant* anyOnSide(int side, int preferPos) {
    Combatant* c = battleField(side, preferPos);
    for (int p = 0; !c && p < MAX_FIELD; p++) c = battleField(side, p);
    return c ? c : &battle.side[side].slot[0];
}

Combatant* battleActing(void) {
    Combatant* c = battleField(SIDE_PLAYER, battle.actingSlot);
    return c && !c->done ? c : NULL;
}
Combatant* battleTarget(void) { return battleField(SIDE_ENEMY, battle.playerTarget); }
Combatant* battleFieldPlayer(void) { return anyOnSide(SIDE_PLAYER, battle.actingSlot); }
Combatant* battleFieldEnemy(void) { return anyOnSide(SIDE_ENEMY, battle.playerTarget); }

Mech* battleRosterMech(int teamIdx) {
    Side* sd = &battle.side[SIDE_PLAYER];
    for (int i = 0; i < sd->count; i++) if (sd->rosterIndex[i] == teamIdx) return &sd->mech[i];
    return NULL;
}

static void sideClear(Side* sd) {
    memset(sd, 0, sizeof(*sd));
    for (int i = 0; i < MAX_TEAM; i++) sd->rosterIndex[i] = -1;
    for (int i = 0; i < MAX_FIELD; i++) sd->field[i] = -1;
    sd->numField = MAX_FIELD;
}

static int slotOnField(const Side* sd, int slot) {
    for (int i = 0; i < sd->numField; i++) if (sd->field[i] == slot) return 1;
    return 0;
}

// Copies a mech into the side's next slot (a reserve until deployed)
static int sideAdd(Side* sd, const Mech* m, int rosterIdx) {
    int i = sd->count++;
    sd->mech[i] = *m;
    sd->rosterIndex[i] = rosterIdx;
    initCombatant(&sd->slot[i], &sd->mech[i]);
    prepareForField(&sd->mech[i]);
    return i;
}

int battleSlotOnField(int side, int slot) { return slotOnField(&battle.side[side], slot); }

int battleSlotStanding(int side, int slot) {
    const Side* sd = &battle.side[side];
    return slot >= 0 && slot < sd->count && !sd->slot[slot].out && sd->mech[slot].stats.integrity > 0;
}

int battleSideStanding(int side) {
    int n = 0;
    for (int i = 0; i < battle.side[side].count; i++) n += battleSlotStanding(side, i);
    return n;
}

// Standing mechs of a side that are not on the field
static int sideReserves(int side, int* out, int max) {
    const Side* sd = &battle.side[side];
    int n = 0;
    for (int i = 0; i < sd->count && n < max; i++)
        if (!slotOnField(sd, i) && battleSlotStanding(side, i)) out[n++] = i;
    return n;
}

static int emptyFieldPos(int side) {
    for (int p = 0; p < battle.side[side].numField; p++) if (battle.side[side].field[p] < 0) return p;
    return -1;
}

// Player's copies go back to team[] (the battle's mechs are copies)
static void writeBackTeam(void) {
    Side* sd = &battle.side[SIDE_PLAYER];
    for (int i = 0; i < sd->count; i++)
        if (sd->rosterIndex[i] >= 0 && sd->rosterIndex[i] < teamSize) team[sd->rosterIndex[i]] = sd->mech[i];
}

// Wild and trainer machines run their own firmware: random chips up to their
// sockets and capacity. They can be corrupted, and salvaged by hacking.
static void equipEnemyChips(Mech* m) {
    for (int s = 0; s < firmwareSockets(&m->fw); s++)
        for (int tries = 0; tries < 6; tries++) {
            int c = rand() % NUM_CHIPS;
            if (firmwareCanInstall(&m->fw, s, c)) { firmwareInstall(&m->fw, s, c); break; }
        }
    mechRefreshStats(m);
}

// ============ TARGETS ============
// Enemy field positions a weapon reaches when aimed at `primary`, with each
// one's damage share: AREA covers the whole field, CONE the target and the
// positions next to it, anything else only the target. Extra targets are
// ordered nearest first and each takes SPLASH_FALLOFF less (compounding).
static int weaponTargets(const Weapon* w, int side, int primary, int* pos, float* mod, int max) {
    int n = 0;
    if (!w || !battleField(side, primary) || max < 1) return 0;
    pos[n] = primary;
    mod[n++] = 1.0f;
    if (w->targeting != TARGET_AREA && w->targeting != TARGET_CONE) return n;
    float m = 1.0f;
    for (int d = 1; d < MAX_FIELD; d++) {
        if (w->targeting == TARGET_CONE && d > 1) break;
        for (int s = -1; s <= 1 && n < max; s += 2) {
            int p = primary + s * d;
            if (!battleField(side, p)) continue;
            m *= SPLASH_FALLOFF;
            pos[n] = p;
            mod[n++] = m;
        }
    }
    return n;
}

static AttackContext splashContext(const Combatant* a, const Combatant* d, float splash, int secondary) {
    AttackContext ctx = liveContext(a, d);
    ctx.splashMod = splash;
    if (secondary) ctx.energyTax = 0;   // the shot is paid for once
    return ctx;
}

int battlePreviewTargets(int mount, int* pos, AttackPreview* out, int max) {
    Combatant* a = battleActing();
    const Weapon* w = a ? mechWeapon(a->mech, mount) : NULL;
    int p[MAX_FIELD];
    float mod[MAX_FIELD];
    int n = weaponTargets(w, SIDE_ENEMY, battle.playerTarget, p, mod, max < MAX_FIELD ? max : MAX_FIELD);
    for (int k = 0; k < n; k++) {
        Combatant* d = battleField(SIDE_ENEMY, p[k]);
        AttackContext ctx = splashContext(a, d, mod[k], k > 0);
        attackPreview(a->mech, w, d->mech, &ctx, &out[k]);
        pos[k] = p[k];
    }
    return n;
}

int battlePreviewPlayer(int mount, AttackPreview* out) {
    int pos[MAX_FIELD];
    AttackPreview all[MAX_FIELD];
    if (battlePreviewTargets(mount, pos, all, MAX_FIELD) < 1) return 0;
    *out = all[0];
    return 1;
}

int battleExplainPlayer(int mount, Explanation* out) {
    Combatant* a = battleActing();
    Combatant* d = battleTarget();
    AttackPreview p;
    if (!a || !d || !battlePreviewPlayer(mount, &p)) return 0;
    explainAttack(a, d, mechWeapon(a->mech, mount), &p, out);
    int pos[MAX_FIELD];
    AttackPreview all[MAX_FIELD];
    int n = battlePreviewTargets(mount, pos, all, MAX_FIELD);
    for (int k = 1; k < n; k++)
        say(out, 0, "SPLASH -> %s: %d%% damage, %d%% to hit, ARM -%d INT -%d", battleField(SIDE_ENEMY, pos[k])->mech->name,
            (int)roundf(all[k].splashMod * 100), (int)roundf(all[k].hitChance * 100), all[k].armorDamage, all[k].integrityDamage);
    return 1;
}

int battleHeatBlocksNextTurn(int mount, int* blocked, int max) {
    const Combatant* c = battleActing();
    const Weapon* w = c ? mechWeapon(c->mech, mount) : NULL;
    if (!w) return 0;
    const MechStats* s = &c->mech->stats;
    int next = formulaHeatAfterCooling(s->heat + weaponHeat(c->mech, w), s->cooling);
    int n = 0;
    for (int i = 0; i < MAX_WEAPONS && n < max; i++) {
        const Weapon* o = mechWeapon(c->mech, i);
        if (o && next + weaponHeat(c->mech, o) > s->maxHeat) blocked[n++] = i;
    }
    return n;
}

// Best (weapon, target) for this attacker against the other side's field;
// area and cone weapons add the value of their splash targets. ignoreResources
// skips the Energy / Heat / scramble checks (Dead-Man Protocol's free shot).
static int aiChooseAction(const Combatant* a, int targetSide, int ignoreResources, int* targetPos, float* bestScore) {
    int best = -1, bestPos = -1;
    float bestValue = -1;
    for (int t = 0; t < MAX_FIELD; t++) {
        if (!battleField(targetSide, t)) continue;
        for (int i = 0; i < MAX_WEAPONS; i++) {
            const Weapon* w = mechWeapon(a->mech, i);
            if (!w) continue;
            if (ignoreResources ? (w->ammo > 0 && a->mech->weapons[i].ammo <= 0) : !canFire(a, i, NULL)) continue;
            int pos[MAX_FIELD];
            float mod[MAX_FIELD], v = 0;
            int n = weaponTargets(w, targetSide, t, pos, mod, MAX_FIELD);
            for (int k = 0; k < n; k++) {
                const Combatant* d = battleField(targetSide, pos[k]);
                AttackContext ctx = splashContext(a, d, mod[k], k > 0);
                v += aiScoreAttack(a->mech, w, d->mech, &ctx, a->ai);
            }
            if (v > bestValue) { bestValue = v; best = i; bestPos = t; }
        }
    }
    if (targetPos) *targetPos = bestPos;
    if (bestScore) *bestScore = bestValue;
    return best;
}

// Start of this mech's turn: tick corruption, refill Energy, cool Heat, then
// (on the field only) apply queued scrambles and run Emergency and Behavioral
// (IF/THEN) chips. A benched mech keeps its queued scrambles for its next turn
// on the field. Writes a log note.
static void turnStart(Combatant* c, int onField, char* note, int size) {
    MechStats* s = &c->mech->stats;
    note[0] = 0;
    firmwareCorruptionTick(&c->mech->fw);
    mechRefreshStats(c->mech);   // a stat chip may have come back online
    s->energy = s->maxEnergy - c->nextEnergyLoss + (int)fwEffect(c->mech, CFX_BONUS_ENERGY);
    if (onField && !c->emergencyPowerUsed && integrityBelow(c, 0.25f) && fwEffect(c->mech, CFX_EMERGENCY_POWER) > 0) {
        s->energy += (int)fwEffect(c->mech, CFX_EMERGENCY_POWER);
        c->emergencyPowerUsed = 1;
    }
    if (onField && !c->lastStandUsed && integrityBelow(c, 0.10f) && fwEffect(c->mech, CFX_LAST_STAND) > 0) {
        s->energy += (int)fwEffect(c->mech, CFX_LAST_STAND);
        c->lastStandUsed = 1;
    }
    if (s->energy < 0) s->energy = 0;
    s->heat = formulaHeatAfterCooling(s->heat, s->cooling);
    c->evasiveBonus = 0;
    c->actionsThisTurn = 0;
    c->attackedThisRound = 0;
    if (!onField) {
        c->accPenalty = c->skipTurn = c->energyTax = c->randomTargeting = c->switchLocked = 0;
        c->disabledWeapon = -1;
        return;
    }
    c->done = 0;
    c->switchLocked = c->switchLock > 0;
    if (c->switchLock > 0) c->switchLock--;
    c->accPenalty = c->nextAccPenalty;
    c->disabledWeapon = c->nextDisabledWeapon;
    c->skipTurn = c->nextSkipTurn;
    if (c->skipTurn) c->skipImmune = 2;          // no stun-lock: two normal turns before the next loss
    else if (c->skipImmune > 0) c->skipImmune--;
    c->energyTax = c->nextEnergyTax;
    c->randomTargeting = c->nextRandomTargeting;
    c->nextEnergyLoss = c->nextAccPenalty = c->nextSkipTurn = c->nextEnergyTax = c->nextRandomTargeting = 0;
    c->nextDisabledWeapon = -1;

    int repair = (int)fwEffect(c->mech, CFX_EMERGENCY_REPAIR);
    if (repair > 0 && integrityBelow(c, 0.30f) && s->energy >= 2) {
        s->energy -= 2;
        s->integrity = clampi(s->integrity + repair, 0, s->maxIntegrity);
        snprintf(note, size, " REPAIR PROTOCOL +%d INT.", repair);
    }
    int vent = (int)fwEffect(c->mech, CFX_COOLANT_DUMP);
    if (vent > 0 && s->heat > s->maxHeat * 0.75f && s->energy >= 1) {
        s->energy -= 1;
        s->heat = s->heat > vent ? s->heat - vent : 0;
        snprintf(note + strlen(note), size - strlen(note), " COOLANT DUMP -%d HEAT.", vent);
    }
}

// Every mech a side has in the battle ticks, reserves too. Returns the field
// mechs' notes.
static const char* sideTurnStart(int side) {
    static char notes[200];
    char note[64];
    Side* sd = &battle.side[side];
    notes[0] = 0;
    for (int i = 0; i < sd->count; i++) {
        if (sd->slot[i].out) continue;
        int onField = slotOnField(sd, i);
        turnStart(&sd->slot[i], onField, note, sizeof(note));
        if (onField && note[0])
            snprintf(notes + strlen(notes), sizeof(notes) - strlen(notes), " %s:%s", sd->mech[i].name, note);
    }
    return notes;
}

// ============ EVENTS ============
static BattleEvent* pushEvent(int fx, int fromPlayer, int fromSlot, int toSlot, int damage, int hit, Color color) {
    if (battle.numEvents >= MAX_BATTLE_EVENTS) return NULL;
    battle.events[battle.numEvents] = (BattleEvent){ fx, fromPlayer, damage, hit, color, -1, 0, 0, 0, fromSlot, toSlot };
    return &battle.events[battle.numEvents++];
}

int battlePopEvent(BattleEvent* out) {
    if (battle.numEvents == 0) return 0;
    *out = battle.events[0];
    memmove(battle.events, battle.events + 1, sizeof(BattleEvent) * (--battle.numEvents));
    return 1;
}

static float fxDuration(int fx) {
    switch (fx) {
    case FX_NOVA: return 0.9f;
    case FX_MISSILE: return 0.7f;
    case FX_BEAM: return 0.5f;
    case FX_SCAN: return 1.0f;
    case FX_SWITCH: return 0.8f;
    default: return 0.55f;
    }
}

// Writes a system line to the log strip and the history
static void logLine(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(battle.log, sizeof(battle.log), fmt, args);
    va_end(args);
    syncLog();
}

// ============ ACTIONS ============
// Firmware Corruption (design doc 7.19), one of four at random. The chip ones
// need an active chip and fall through to the others when there is none.
static const char* applyCorruption(Combatant* d) {
    int order[4] = { 0, 1, 2, 3 };
    for (int i = 3; i > 0; i--) { int j = rand() % (i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < 4; i++) {
        switch (order[i]) {
        case 0: case 1: {
            CorruptKind kind = order[i] == 0 ? CORRUPT_DISABLED : CORRUPT_REVERSED;
            int chip = firmwareCorruptRandom(&d->mech->fw, kind);
            if (chip < 0) continue;
            mechRefreshStats(d->mech);
            return TextFormat(kind == CORRUPT_DISABLED ? "CHIP OFFLINE (%s)" : "INSTRUCTIONS REVERSED (%s)", chipDefs[chip].name);
        }
        case 2: d->nextEnergyTax = 1; return "ENERGY COSTS +1";
        default: d->nextRandomTargeting = 1; return "TARGETING RANDOMIZED";
        }
    }
    return "ENERGY COSTS +1";
}

// Scramble outcome depends on strength (design doc 4.6); it hits the victim's
// next turn. Virus weapons always corrupt firmware; other scrambles of strength
// 40+ do so half the time.
static const char* applyScramble(Combatant* d, int strength, int virus) {
    if (virus) return TextFormat("CORRUPTION: %s", applyCorruption(d));
    if (strength >= 100 && d->skipImmune <= 0) { d->nextSkipTurn = 1; return "TURN LOST"; }
    if (strength >= 40 && rand() % 2 == 0) return TextFormat("CORRUPTION: %s", applyCorruption(d));
    if (strength >= 70) {
        int mounts[MAX_WEAPONS], n = 0;
        for (int i = 0; i < MAX_WEAPONS; i++) if (d->mech->weapons[i].weapon >= 0) mounts[n++] = i;
        if (n > 0) { d->nextDisabledWeapon = mounts[rand() % n]; return "WEAPON DISABLED"; }
    }
    if (strength >= 40) { d->nextAccPenalty = 15; return "ACCURACY -15"; }
    d->nextEnergyLoss += 1;
    return "ENERGY -1";
}

// One attack. The cost is paid once; then every target the weapon reaches (the
// primary, plus area / cone splash at -25% each) is rolled and resolved on its
// own, with its own log entry. prefix, if set, starts the log line.
static void doAttack(Combatant* a, int aSide, int aPos, int dSide, int primary, int mount, const char* prefix) {
    const Weapon* w = mechWeapon(a->mech, mount);
    int isPlayer = aSide == SIDE_PLAYER;
    int pos[MAX_FIELD];
    float mod[MAX_FIELD];
    int n = weaponTargets(w, dSide, primary, pos, mod, MAX_FIELD);
    if (n == 0) return;

    // Previews and explanations first, before anything changes
    AttackPreview p[MAX_FIELD];
    Explanation why[MAX_FIELD];
    for (int k = 0; k < n; k++) {
        Combatant* d = battleField(dSide, pos[k]);
        AttackContext ctx = splashContext(a, d, mod[k], k > 0);
        attackPreview(a->mech, w, d->mech, &ctx, &p[k]);
        explainAttack(a, d, w, &p[k], &why[k]);
    }

    MechStats* as = &a->mech->stats;
    as->energy -= p[0].energyCost;
    as->heat += p[0].heat;
    if (w->ammo > 0) a->mech->weapons[mount].ammo--;
    char who[48], first[256] = "";
    snprintf(who, sizeof(who), isPlayer ? "%s" : "Enemy %s", a->mech->name);

    for (int k = 0; k < n; k++) {
        Combatant* d = battleField(dSide, pos[k]);
        MechStats* ds = &d->mech->stats;
        int lethalBefore = p[k].integrityDamage >= ds->integrity;
        d->attackedThisRound = 1;
        float roll = frand();
        int hit = roll < p[k].hitChance;
        float scrambleRoll = -1;
        int total = 0;
        char extra[96] = "", line[256];
        if (hit) {
            ds->armor -= p[k].armorDamage;
            ds->integrity -= p[k].integrityDamage;
            if (ds->integrity < 0) ds->integrity = 0;
            if (p[k].breachBonus > 0) a->breachUsed = 1;
            total = p[k].armorDamage + p[k].integrityDamage;
            if (total > 0) {
                d->hitTaken = 1;
                d->lastMunitionTaken = w->munition;
            }
            if (p[k].scramble > 0 && ds->integrity > 0) {
                scrambleRoll = frand();
                if (scrambleRoll < p[k].resist) {
                    int heal = (int)fwEffect(d->mech, CFX_SYSTEM_RECOVERY);
                    ds->integrity = clampi(ds->integrity + heal, 0, ds->maxIntegrity);
                    snprintf(extra, sizeof(extra), " Scramble resisted%s.", heal > 0 ? " (recovered)" : "");
                }
                else snprintf(extra, sizeof(extra), " SCRAMBLED: %s!", applyScramble(d, p[k].scramble, w->virus));
            }
        }
        if (k == 0) {
            if (!hit) snprintf(line, sizeof(line), "%s%s fired %s at %s... MISSED! (%d%% to hit)", prefix ? prefix : "", who,
                w->name, d->mech->name, (int)roundf(p[k].hitChance * 100));
            else if (total > 0) snprintf(line, sizeof(line), "%s%s fired %s at %s! %d DMG (%d ARM / %d INT).%s", prefix ? prefix : "",
                who, w->name, d->mech->name, total, p[k].armorDamage, p[k].integrityDamage, extra);
            else snprintf(line, sizeof(line), "%s%s activated %s on %s.%s", prefix ? prefix : "", who, w->name, d->mech->name, extra);
            snprintf(first, sizeof(first), "%s", line);
            if (!hit && fwEffect(a->mech, CFX_RECURSIVE_TARGETING) > 0) a->missStacks++;
        }
        else if (!hit) snprintf(line, sizeof(line), "   splash misses %s (%d%% to hit)", d->mech->name, (int)roundf(p[k].hitChance * 100));
        else snprintf(line, sizeof(line), "   splash hits %s: %d DMG (%d ARM / %d INT).%s", d->mech->name, total,
            p[k].armorDamage, p[k].integrityDamage, extra);

        // History entry: the roll first, then the reasons
        LogEntry* le = logPush(line, isPlayer, w->munition);
        say(&le->why, !hit, "ROLL %d vs %d%% to hit -> %s", (int)(roll * 100), (int)roundf(p[k].hitChance * 100), hit ? "HIT" : "MISS");
        if (scrambleRoll >= 0)
            say(&le->why, 0, "SCRAMBLE ROLL %d vs %d%% resist -> %s", (int)(scrambleRoll * 100), (int)roundf(p[k].resist * 100),
                scrambleRoll < p[k].resist ? "resisted" : "landed");
        for (int i = 0; i < why[k].n; i++) say(&le->why, why[k].warn[i], "%s", why[k].line[i]);

        BattleEvent* ev = pushEvent(w->fx, isPlayer, aPos, pos[k], total, hit, munitionColor(w->munition));
        if (ev) {
            ev->munition = w->munition;
            ev->armorDamage = hit ? p[k].armorDamage : 0;
            ev->integrityDamage = hit ? p[k].integrityDamage : 0;
            ev->lethal = hit && lethalBefore;
        }
    }
    a->actionsThisTurn++;
    a->evasiveBonus = (int)fwEffect(a->mech, CFX_EVASIVE_MANEUVER);
    if (n > 1) snprintf(battle.log, sizeof(battle.log), "%s (+%d splash)", first, n - 1);
    else snprintf(battle.log, sizeof(battle.log), "%s", first);
    snprintf(lastLogged, sizeof(lastLogged), "%s", battle.log);   // already in the history
    battle.animTimer = fxDuration(w->fx);
    battle.outcomePending = 1;
}

// Revision Data from a kill (or a lost battle) is shared by every player mech
// that has taken the field this battle; a team earns TEAM_DATA_BONUS times what
// a lone mech would, split evenly.
static void awardData(int amount) {
    Side* sd = &battle.side[SIDE_PLAYER];
    int n = 0;
    for (int i = 0; i < sd->count; i++) n += sd->slot[i].fielded;
    if (n == 0 || amount <= 0) return;
    int each = n == 1 ? amount : (int)ceilf(amount * TEAM_DATA_BONUS / n);
    battle.dataEarned += each * n;
    for (int i = 0; i < sd->count; i++) {
        if (!sd->slot[i].fielded) continue;
        Mech* m = &sd->mech[i];
        int gained = firmwareAddData(&m->fw, each);
        if (gained > 0) {
            battle.revisionsGained += gained;
            mechRefreshStats(m);
        }
    }
}

static void lootAppend(const char* note) {
    size_t n = strlen(battle.loot);
    snprintf(battle.loot + n, sizeof(battle.loot) - n, "%s", note);
}

// ============ PHASES ============
static void beginPlayerTurn(void);

// Next player field mech that can still act this phase, searching from `from`
static int nextActor(int from) {
    for (int k = 0; k < MAX_FIELD; k++) {
        int p = ((from % MAX_FIELD) + MAX_FIELD + k) % MAX_FIELD;
        Combatant* c = battleField(SIDE_PLAYER, p);
        if (c && !c->done) return p;
    }
    return -1;
}

static void fixActor(void) {
    if (battleActing()) return;
    int p = nextActor(battle.actingSlot + 1);
    if (p >= 0) battle.actingSlot = p;
}

static void fixTarget(void) {
    if (battleTarget()) return;
    for (int p = 0; p < MAX_FIELD; p++) if (battleField(SIDE_ENEMY, p)) { battle.playerTarget = p; return; }
}

void battleSelectActor(int pos) {
    Combatant* c = battleField(SIDE_PLAYER, pos);
    if (battle.phase == BP_PLAYER_TURN && c && !c->done) battle.actingSlot = pos;
}

void battleNextActor(void) {
    if (battle.phase != BP_PLAYER_TURN) return;
    int p = nextActor(battle.actingSlot + 1);
    if (p >= 0) battle.actingSlot = p;
}

void battleSetTarget(int pos) { if (battleField(SIDE_ENEMY, pos)) battle.playerTarget = pos; }

void battleCycleTarget(int dir) {
    for (int k = 1; k <= MAX_FIELD; k++) {
        int p = ((battle.playerTarget + dir * k) % MAX_FIELD + MAX_FIELD) % MAX_FIELD;
        if (battleField(SIDE_ENEMY, p)) { battle.playerTarget = p; return; }
    }
}

static void enemyFillField(void);

static void beginEnemyTurn(void) {
    const char* note = sideTurnStart(SIDE_ENEMY);
    battle.phase = BP_ENEMY_TURN;
    battle.enemyActing = 0;
    if (note[0]) logLine("Enemy:%s", note);
    enemyFillField();
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* c = battleField(SIDE_ENEMY, p);
        if (!c || !c->skipTurn) continue;
        c->done = 1;
        c->mech->stats.energy = 0;
        logLine("Enemy %s is scrambled and loses its turn!", c->mech->name);
        battle.animTimer = 1.2f;
    }
}

static int anyPlayerActor(void) { return nextActor(0) >= 0; }

// The player phase: every field mech gets its turn. Scrambled mechs lose it;
// with nobody on the field the player must deploy (or yield).
static void beginPlayerTurn(void) {
    battle.round++;
    const char* note = sideTurnStart(SIDE_PLAYER);
    battle.phase = BP_PLAYER_TURN;
    battle.phaseActed = 0;
    logLine("ROUND %d. Reactors recharged, heat vented.%s", battle.round, note);
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* c = battleField(SIDE_PLAYER, p);
        if (!c || !c->skipTurn) continue;
        c->done = 1;
        logLine("SYSTEMS SCRAMBLED! %s loses its turn.", c->mech->name);
    }
    fixTarget();
    battle.actingSlot = nextActor(0) >= 0 ? nextActor(0) : battle.actingSlot;
    if (battleFieldCount(SIDE_PLAYER) == 0) {
        battle.phase = BP_DEPLOY;
        logLine("No mechs on the field - deploy a reserve.");
    }
    else if (!anyPlayerActor() && !battleCanDeploy()) {   // everyone scrambled
        beginEnemyTurn();
        battle.animTimer = 1.2f;
    }
}

// ============ SWITCHING ============
// Why this field mech can't be switched out this turn, NULL if it can
static const char* switchBlock(const Combatant* c) {
    if (c->switchLocked) return "LOCKED IN (just deployed)";
    if (c->skipTurn) return "SYSTEMS SCRAMBLED";
    if (c->mech->stats.energy < SWITCH_ENERGY_COST) return "INSUFFICIENT ENERGY";
    return NULL;
}

// Puts reserve `slot` on the side's field position. A paid switch drains the
// outgoing mech's Energy and clears its turn state, and the incoming mech is
// locked in for its next turn; a free deploy fills an empty position with no
// lockout. Either way the new mech doesn't act the round it arrives.
static void putOnField(int side, int pos, int slot, int paid) {
    Side* sd = &battle.side[side];
    Combatant* out = battleField(side, pos);
    Combatant* in = &sd->slot[slot];
    if (paid && out) {
        out->mech->stats.energy = 0;            // outgoing Energy is lost (covers the 1 EN cost)
        out->actionsThisTurn = out->attackedThisRound = out->evasiveBonus = out->missStacks = 0;
        out->accPenalty = out->skipTurn = out->energyTax = out->randomTargeting = out->switchLocked = 0;
        out->disabledWeapon = -1;
        out->done = 0;
    }
    sd->field[pos] = slot;
    in->switchLock = paid ? 1 : 0;
    in->switchLocked = 0;
    in->actionsThisTurn = 0;
    in->done = 1;
    in->fielded = 1;
    pushEvent(FX_SWITCH, side == SIDE_PLAYER, pos, pos, 0, 1, mechModel(in->mech)->accent);
    battle.animTimer = fxDuration(FX_SWITCH);
}

int battleCanSwitch(const char** reason) {
    int res[MAX_TEAM];
    const char* r = NULL;
    Combatant* a = battleActing();
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) r = "STANDBY";
    else if (!a) r = "NO MECH TO COMMAND";
    else if (sideReserves(SIDE_PLAYER, res, MAX_TEAM) == 0) r = "NO RESERVES";
    else r = switchBlock(a);
    if (reason) *reason = r;
    return r == NULL;
}

int battleSwitchList(int* out, int max) { return sideReserves(SIDE_PLAYER, out, max); }

static int isReserve(int side, int slot) {
    return battleSlotStanding(side, slot) && !slotOnField(&battle.side[side], slot);
}

void battleSwitchTo(int slot) {
    if (!battleCanSwitch(NULL) || !isReserve(SIDE_PLAYER, slot)) return;
    const char* from = battleActing()->mech->name;
    logLine("%s withdraws (%d EN). %s takes its place next round - it can't switch out next turn.",
        from, SWITCH_ENERGY_COST, battle.side[SIDE_PLAYER].mech[slot].name);
    putOnField(SIDE_PLAYER, battle.actingSlot, slot, 1);
    battle.phaseActed = 1;
    fixActor();
}

int battleCanDeploy(void) {
    int res[MAX_TEAM];
    return (battle.phase == BP_PLAYER_TURN || battle.phase == BP_DEPLOY) && !battleBusy()
        && emptyFieldPos(SIDE_PLAYER) >= 0 && sideReserves(SIDE_PLAYER, res, MAX_TEAM) > 0;
}

void battleDeploy(int slot) {
    if (!battleCanDeploy() || !isReserve(SIDE_PLAYER, slot)) return;
    int pos = emptyFieldPos(SIDE_PLAYER);
    putOnField(SIDE_PLAYER, pos, slot, 0);
    logLine("%s deployed. It acts next round.", battle.side[SIDE_PLAYER].mech[slot].name);
    battle.phase = BP_PLAYER_TURN;
    battle.phaseActed = 1;
    fixActor();
}

static void playerDefeated(void);
void battleYield(void) {
    if ((battle.phase != BP_PLAYER_TURN && battle.phase != BP_DEPLOY) || battleBusy()) return;
    playerDefeated();
    logLine("You withdraw your remaining mechs. Battle lost.");
}

// The squad's healthiest standing reserve, -1 if none
static int nextEnemy(float atLeast) {
    int res[MAX_TEAM], n = sideReserves(SIDE_ENEMY, res, MAX_TEAM), best = -1;
    float bestFrac = atLeast;
    for (int i = 0; i < n; i++) {
        const MechStats* s = &battle.side[SIDE_ENEMY].mech[res[i]].stats;
        float frac = s->maxIntegrity > 0 ? (float)s->integrity / s->maxIntegrity : 0;
        if (frac > bestFrac) { bestFrac = frac; best = res[i]; }
    }
    return best;
}

static const char* enemyCommander(void) {
    return battle.trainer >= 0 ? trainers[battle.trainer].name : "The pack";
}

// Enemy reserves fill empty positions at the start of the enemy phase
static void enemyFillField(void) {
    for (int p = emptyFieldPos(SIDE_ENEMY); p >= 0; p = emptyFieldPos(SIDE_ENEMY)) {
        int next = nextEnemy(-1);
        if (next < 0) return;
        putOnField(SIDE_ENEMY, p, next, 0);
        logLine("%s sends in %s! (%d left)", enemyCommander(), battle.side[SIDE_ENEMY].mech[next].name, battleSideStanding(SIDE_ENEMY));
    }
}

// Basic enemy swap rule: at the start of its turn, a machine below 25% Integrity
// pulls out if a healthy reserve (50%+ Integrity) can take its place.
static int enemyConsiderSwitch(int pos) {
    Combatant* c = battleField(SIDE_ENEMY, pos);
    if (!c || c->actionsThisTurn > 0 || switchBlock(c) || !integrityBelow(c, AI_SWITCH_BELOW)) return 0;
    int best = nextEnemy(HEALTHY_RESERVE - 0.0001f);
    if (best < 0) return 0;
    logLine("Enemy %s pulls back! %s moves up.", c->mech->name, battle.side[SIDE_ENEMY].mech[best].name);
    putOnField(SIDE_ENEMY, pos, best, 1);
    return 1;
}

// ============ START ============
// Adds a machine to the enemy squad (a reserve until it takes the field)
static void addEnemy(const Mech* m, int archetype) {
    Side* sd = &battle.side[SIDE_ENEMY];
    int slot = sideAdd(sd, m, -1);
    Combatant* c = &sd->slot[slot];
    c->archetype = archetype;
    c->ai = archetype >= 0 ? &archetypes[archetype].ai : &aiDefault;
}

// Fills the side's field positions with its first mechs (order[] of slots)
static void sideOpenField(int side, const int* order, int n) {
    Side* sd = &battle.side[side];
    sd->numField = n < MAX_FIELD ? n : MAX_FIELD;
    for (int p = 0; p < sd->numField; p++) {
        sd->field[p] = order[p];
        sd->slot[order[p]].fielded = 1;
    }
}

// The player side carries the whole team: the active mech and the next ones in
// team order on the field, the rest in reserve.
static void setupPlayerSide(void) {
    Side* sd = &battle.side[SIDE_PLAYER];
    int order[MAX_TEAM], n = 0;
    sideClear(sd);
    for (int i = 0; i < teamSize; i++) {
        int slot = sideAdd(sd, &team[i], i);
        sd->startRevision[slot] = sd->mech[slot].fw.revision;
    }
    for (int k = 0; k < teamSize; k++) {
        int i = (activeTeamSlot + k) % teamSize;
        if (sd->mech[i].stats.integrity > 0) order[n++] = i;
    }
    if (n == 0) order[n++] = activeTeamSlot;
    sideOpenField(SIDE_PLAYER, order, n);
}

static int fieldMobility(int side) {
    int sum = 0, n = 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* c = battleField(side, p);
        if (c) { sum += c->mech->stats.mobility; n++; }
    }
    return n ? sum / n : 0;
}

// The enemy squad must already be added (addEnemy); its first machines open
static void beginBattle(void) {
    battle.phase = BP_PLAYER_TURN;
    battle.dialogue = DLG_NONE;
    battle.testRange = 0;
    battle.round = 0;
    battle.animTimer = 0;
    battle.outcomePending = 0;
    battle.dataEarned = 0;
    battle.revisionsGained = 0;
    battle.numRevisions = 0;
    battle.hacked = 0;
    battle.loot[0] = 0;
    battle.result = RESULT_NONE;
    battle.numEvents = 0;
    battle.actingSlot = 0;
    battle.playerTarget = 0;
    logClear();
    setupPlayerSide();
    int order[MAX_TEAM];
    for (int i = 0; i < battle.side[SIDE_ENEMY].count; i++) order[i] = i;
    sideOpenField(SIDE_ENEMY, order, battle.side[SIDE_ENEMY].count);
    // Initiative (balance rule, not in the design doc): the faster team (average
    // Mobility on the field) opens the fight; ties go to the player.
    int pm = fieldMobility(SIDE_PLAYER), em = fieldMobility(SIDE_ENEMY);
    if (em > pm) {
        logPush(TextFormat("The enemy is faster (Mobility %d vs %d) and moves first.", em, pm), -1, -1);
        beginEnemyTurn();
    }
    else beginPlayerTurn();
}

// Wild machines hunt in packs up to the zone's size, never outnumbering the team
static int wildPackSize(void) {
    int cap = worldCurrentZone() == 0 ? 2 : MAX_FIELD;
    int max = teamSize < cap ? teamSize : cap;
    if (max < 1) max = 1;
    return max > 1 && rand() % 3 == 0 ? max - 1 : max;
}

static void addWildMachine(int level) {
    int weights[NUM_MODELS], total = 0;
    for (int i = 0; i < NUM_MODELS; i++) {
        int r = mechModels[i].rarity;
        weights[i] = r > 0 ? 4 - r : 0;   // rarity 1 -> 3, 2 -> 2, 3 -> 1, 0 -> never
        total += weights[i];
    }
    int pick = rand() % total, model = 0;
    for (int i = 0; i < NUM_MODELS; i++) {
        if (pick < weights[i]) { model = i; break; }
        pick -= weights[i];
    }
    Mech enemy;
    int archetype = -1;
    if (rand() % 10 < 7) {
        archetype = rand() % NUM_WILD_ARCHETYPES;
        enemy = archetypeBuild(archetype, level);
    }
    else {   // a stock chassis running random chips
        enemy = mechCreateStock(model, level);
        equipEnemyChips(&enemy);
    }
    addEnemy(&enemy, archetype);
}

void battleStartWild(void) {
    battle.trainer = -1;
    int level = gameWildLevel(worldCurrentZone());
    sideClear(&battle.side[SIDE_ENEMY]);
    int n = wildPackSize();
    for (int i = 0; i < n; i++) addWildMachine(level);
    beginBattle();
    char names[160] = "";
    for (int i = 0; i < n; i++)
        snprintf(names + strlen(names), sizeof(names) - strlen(names), "%s%s", i ? ", " : "", battle.side[SIDE_ENEMY].mech[i].name);
    logLine(n > 1 ? "HOSTILE PACK detected: %s!" : "HOSTILE %s detected!", names);
}

// The whole squad is built up front: up to three take the field, the rest wait
// in reserve. The roster is visible on the HUD.
void battleStartTrainer(int trainerIdx) {
    if (trainerIdx < 0 || trainerIdx >= NUM_TRAINERS) { battleStartWild(); return; }
    Trainer* t = &trainers[trainerIdx];
    t->numDefeated = 0;
    battle.trainer = trainerIdx;
    sideClear(&battle.side[SIDE_ENEMY]);
    for (int k = 0; k < t->numMechs && k < MAX_TEAM; k++) {
        Mech m = archetypeBuild(t->teamArchetypes[k], t->teamRevisions[k]);
        addEnemy(&m, t->teamArchetypes[k]);
    }
    beginBattle();
    logLine("%s deploys a squad of %d!", t->name, t->numMechs);
    battle.dialogue = DLG_INTRO;
    snprintf(battle.dialogueText, sizeof(battle.dialogueText), "\"%s\"", t->introLine);
}

static Mech makeDummy(void) {
    Mech m = mechCreateStock(MODEL_DUMMY, 0);
    snprintf(m.name, sizeof(m.name), "TARGET DUMMY");
    return m;
}

// Rebuilds the dummy in place on the enemy field
void battleResetDummy(void) {
    Combatant* c = &battle.side[SIDE_ENEMY].slot[0];
    Mech* m = c->mech;
    *m = makeDummy();
    initCombatant(c, m);
    prepareForField(m);
    c->fielded = 1;
    battle.side[SIDE_ENEMY].field[0] = 0;
    battle.playerTarget = 0;
}

void battleStartTestRange(void) {
    battle.trainer = -1;
    battle.dummyKills = 0;
    Mech dummy = makeDummy();
    sideClear(&battle.side[SIDE_ENEMY]);
    addEnemy(&dummy, -1);
    beginBattle();
    battle.testRange = 1;
    logLine("TEST RANGE: your team vs TARGET DUMMY. The dummy never fires back.");
}

// Nothing that happened on the range sticks except firmware edits: pools go
// back to what the team had.
void battleEndTestRange(void) {
    Side* sd = &battle.side[SIDE_PLAYER];
    for (int i = 0; i < sd->count; i++) {
        int r = sd->rosterIndex[i];
        if (r < 0 || r >= teamSize) continue;
        Mech* m = &sd->mech[i];
        firmwareClearCorruption(&m->fw);
        mechRefreshStats(m);
        m->stats.integrity = team[r].stats.integrity < m->stats.maxIntegrity ? team[r].stats.integrity : m->stats.maxIntegrity;
        m->stats.armor = team[r].stats.armor < m->stats.maxArmor ? team[r].stats.armor : m->stats.maxArmor;
        m->stats.heat = 0;
        m->stats.energy = m->stats.maxEnergy;
        mechReloadWeapons(m);
    }
    writeBackTeam();
    battle.testRange = 0;
}

// ============ OUTCOME ============
// A standing enemy's mech, for participation data
static const Mech* anyEnemyMech(void) {
    for (int i = 0; i < battle.side[SIDE_ENEMY].count; i++)
        if (battleSlotStanding(SIDE_ENEMY, i)) return &battle.side[SIDE_ENEMY].mech[i];
    return &battle.side[SIDE_ENEMY].mech[0];
}

static void enemyDown(int pos) {
    Combatant* c = battleField(SIDE_ENEMY, pos);
    c->out = 1;
    battle.side[SIDE_ENEMY].field[pos] = -1;
    if (battle.trainer >= 0) {
        Trainer* t = &trainers[battle.trainer];
        t->numDefeated++;
        awardData(revisionDataForTrainer(c->mech, t->tier));
        logLine("%s SCRAPPED! %d of %s's squad left.", c->mech->name, battleSideStanding(SIDE_ENEMY), t->name);
    }
    else {
        int data = revisionDataForWild(c->mech);
        awardData(data);
        char note[160];
        gameOnWildScrapped(c->mech, note, sizeof(note));
        lootAppend(note);
        logLine("TARGET %s SCRAPPED! +%d DATA.", c->mech->name, data);
    }
}

static void victory(void) {
    battle.phase = BP_VICTORY;
    if (battle.trainer >= 0) {
        Trainer* t = &trainers[battle.trainer];
        t->defeated = 1;
        char note[160];
        gameOnEncounterDefeated(battle.trainer, note, sizeof(note));
        lootAppend(note);
        logLine("ALL MECHS DOWN! %s defeated!", t->name);
        battle.dialogue = DLG_DEFEAT;
        snprintf(battle.dialogueText, sizeof(battle.dialogueText), "\"%s\"", t->defeatLine);
    }
    else logLine(battle.revisionsGained > 0 ? "HOSTILES CLEARED! Firmware revision ready!" : "HOSTILES CLEARED! +%d DATA.",
        battle.dataEarned);
}

// The player's last mech is down (or the player yielded)
static void playerDefeated(void) {
    if (!battle.testRange) awardData(revisionDataForParticipation(anyEnemyMech()));
    battle.phase = BP_DEFEAT;
    logLine("ALL MECHS DISABLED!");
}

// A player mech hit 0 Integrity: the recovery crew is paid for it and its
// position stays empty until a reserve deploys there.
static void playerDown(int pos) {
    Combatant* c = battleField(SIDE_PLAYER, pos);
    c->out = 1;
    battle.side[SIDE_PLAYER].field[pos] = -1;
    char note[160];
    gameOnPlayerDisabled(note, sizeof(note));
    lootAppend(note);
    logLine("%s DISABLED!", c->mech->name);
}

// Dead-Man Protocol: a machine reduced to 0 Integrity fires one last shot,
// its best weapon, free of Energy and Heat.
static int tryDeadMan(Combatant* c, int side, int pos) {
    MechStats* s = &c->mech->stats;
    int other = side == SIDE_PLAYER ? SIDE_ENEMY : SIDE_PLAYER;
    if (s->integrity > 0 || c->deadManUsed || battleFieldCount(other) == 0) return 0;
    if (fwEffect(c->mech, CFX_DEAD_MAN) <= 0) return 0;
    c->deadManUsed = 1;
    int target;
    int mount = aiChooseAction(c, other, 1, &target, NULL);
    if (mount < 0) return 0;
    int energy = s->energy, heat = s->heat;
    doAttack(c, side, pos, other, target, mount, "DEAD-MAN PROTOCOL! ");
    s->energy = energy;
    s->heat = heat;
    return 1;
}

// Mechs at 0 Integrity leave the field. Dead-Man shots resolve first (one per
// call, each with its animation). If both sides are wiped out, the player
// loses. Returns 1 if the battle state changed.
static int resolveDowns(void) {
    battle.outcomePending = 0;
    for (int side = 0; side < 2; side++)
        for (int p = 0; p < MAX_FIELD; p++) {
            Combatant* c = battleField(side, p);
            if (c && c->mech->stats.integrity <= 0 && tryDeadMan(c, side, p)) return 1;
        }
    int changed = 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* c = battleField(SIDE_ENEMY, p);
        if (!c || c->mech->stats.integrity > 0) continue;
        changed = 1;
        if (battle.testRange) {
            battle.dummyKills++;
            battleResetDummy();
            logLine("TARGET DUMMY destroyed (%d). A new dummy is online.", battle.dummyKills);
        }
        else enemyDown(p);
    }
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* c = battleField(SIDE_PLAYER, p);
        if (c && c->mech->stats.integrity <= 0) { changed = 1; playerDown(p); }
    }
    if (!changed) return 0;
    if (battleSideStanding(SIDE_PLAYER) == 0) { playerDefeated(); return 1; }
    if (battleSideStanding(SIDE_ENEMY) == 0) { victory(); return 1; }
    fixTarget();
    fixActor();
    if (battle.phase == BP_PLAYER_TURN && battleFieldCount(SIDE_PLAYER) == 0) {
        battle.phase = BP_DEPLOY;
        logLine("No mechs on the field - deploy a reserve.");
    }
    return 1;
}

// Leaving a battle: disabled mechs are recovered and repaired, the rest are
// replated; all vent heat and refill energy for the overworld. The copies go
// back to the team and the first mech still on the field becomes active.
static void finish(void) {
    Side* sd = &battle.side[SIDE_PLAYER];
    battle.numRevisions = 0;
    for (int i = 0; i < sd->count; i++) {
        Mech* m = &sd->mech[i];
        firmwareClearCorruption(&m->fw);
        mechRefreshStats(m);
        if (m->stats.integrity <= 0) mechRepair(m);
        else mechReplate(m);
        m->stats.heat = 0;
        m->stats.energy = m->stats.maxEnergy;
        if (m->fw.revision > sd->startRevision[i] && sd->rosterIndex[i] >= 0) {
            battle.revTeam[battle.numRevisions] = sd->rosterIndex[i];
            battle.revFrom[battle.numRevisions++] = sd->startRevision[i];
        }
    }
    writeBackTeam();
    for (int p = 0; p < MAX_FIELD; p++) {
        int slot = sd->field[p];
        if (slot >= 0 && sd->rosterIndex[slot] >= 0 && sd->rosterIndex[slot] < teamSize) {
            activeTeamSlot = sd->rosterIndex[slot];
            break;
        }
    }
    battle.result = battle.numRevisions > 0 ? RESULT_TO_REVISION : RESULT_TO_WORLD;
    battle.phase = BP_OVER;
}

// ============ UPDATE / INPUT ============
int battleBusy(void) { return battle.dialogue != DLG_NONE || battle.animTimer > 0; }

// One enemy action per call: each field machine in turn switches out, attacks
// until it holds or runs dry, then the next one goes.
static void enemyStep(void) {
    for (; battle.enemyActing < MAX_FIELD; battle.enemyActing++) {
        Combatant* c = battleField(SIDE_ENEMY, battle.enemyActing);
        if (!c || c->done) continue;
        if (enemyConsiderSwitch(battle.enemyActing)) return;
        float score;
        int target;
        int mount = aiChooseAction(c, SIDE_PLAYER, 0, &target, &score);
        if (mount >= 0 && c->actionsThisTurn > 0 && score < AI_HOLD_SCORE) mount = -1;   // hold fire
        if (mount < 0) { c->done = 1; continue; }
        int fired = corruptedMount(c, mount);
        doAttack(c, SIDE_ENEMY, battle.enemyActing, SIDE_PLAYER, target, fired, fired != mount ? "TARGETING CORRUPTED! " : NULL);
        return;
    }
    beginPlayerTurn();
}

static void battleUpdateInner(float dt);
void battleUpdate(float dt) {
    battleUpdateInner(dt);
    syncLog();
}

static void battleUpdateInner(float dt) {
    if (battle.dialogue != DLG_NONE) return;
    if (battle.animTimer > 0) {
        battle.animTimer -= dt;
        if (battle.animTimer > 0) return;
    }
    if (battle.outcomePending && resolveDowns()) return;

    if (battle.phase == BP_PLAYER_TURN) {
        Combatant* a = battleActing();
        if (a && a->actionsThisTurn > 0 && !anyFireable(a)) a->done = 1;   // spent
        fixActor();
        if (battle.phaseActed && !anyPlayerActor() && !battleCanDeploy()) {
            logLine("All mechs have acted. Enemy phase.");
            beginEnemyTurn();
        }
    }
    else if (battle.phase == BP_ENEMY_TURN) enemyStep();
}

int battleCanFire(int mount, const char** reason) {
    const char* r = NULL;
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) r = "STANDBY";
    else if (!battleActing()) r = "NO MECH TO COMMAND";
    else if (!battleTarget()) r = "NO TARGET";
    if (r) {
        if (reason) *reason = r;
        return 0;
    }
    return canFire(battleActing(), mount, reason);
}

void battleFire(int mount) {
    if (!battleCanFire(mount, NULL)) return;
    Combatant* a = battleActing();
    int fired = corruptedMount(a, mount);
    battle.phaseActed = 1;
    doAttack(a, SIDE_PLAYER, battle.actingSlot, SIDE_ENEMY, battle.playerTarget, fired, fired != mount ? "TARGETING CORRUPTED! " : NULL);
    syncLog();
}

// Autoplay for tests: the enemy AI's pick for the commanded mech (sets the target)
int battleAIChooseForPlayer(void) {
    if (battle.phase != BP_PLAYER_TURN || battleBusy() || !battleActing()) return -1;
    int target;
    int mount = aiChooseAction(battleActing(), SIDE_ENEMY, 0, &target, NULL);
    if (mount >= 0) battle.playerTarget = target;
    return mount;
}

void battleEndTurn(void) {
    if ((battle.phase != BP_PLAYER_TURN && battle.phase != BP_DEPLOY) || battleBusy()) return;
    if (battleFieldCount(SIDE_PLAYER) == 0) return;   // someone has to take the field (or yield)
    logLine("Ending phase. Enemy taking action...");
    beginEnemyTurn();
    syncLog();
}

// Wild machines are rogue AI; encounter squads only if their faction is rogue
// AI, and never a boss.
static int enemyHackable(const Combatant* d) {
    if (battle.testRange || !d) return 0;
    if (battle.trainer < 0) return factionHackable(FAC_WILD);
    return factionHackable(trainers[battle.trainer].faction) && !d->mech->boss;
}

int battleCanHack(void) {
    return battle.phase == BP_PLAYER_TURN && battleActing() && enemyHackable(battleTarget()) && teamSize < MAX_TEAM;
}

int battleHackStability(void) {
    Combatant* d = battleFieldEnemy();
    return d->mech->stats.stability + (int)fwEffect(d->mech, CFX_COUNTER_INTRUSION)
        - HACK_STABILITY_PER_EFFECT * pendingScrambles(d);
}

float battleHackChance(void) { return hackChance(hackStrength(battleFieldPlayer()->mech), battleHackStability()); }

// Hacking is the commanded mech's action for this phase
void battleHack(void) {
    if (!battleCanHack() || battleBusy()) return;
    Combatant* a = battleActing();
    Combatant* d = battleTarget();
    pushEvent(FX_SCAN, 1, battle.actingSlot, battle.playerTarget, 0, 1, (Color) { 120, 255, 220, 255 });
    battle.animTimer = fxDuration(FX_SCAN);
    battle.phaseActed = 1;
    a->done = 1;
    if (frand() < battleHackChance()) {
        Mech caught = *d->mech;
        firmwareClearCorruption(&caught.fw);
        mechRepair(&caught);
        mechReloadWeapons(&caught);
        caught.fw.data = 0;
        rosterAdd(&caught);
        char note[160];
        gameOnHacked(&caught, d->archetype, note, sizeof(note));   // its parts and chips join the inventory
        lootAppend(note);
        battle.hacked = 1;
        d->out = 1;
        battle.side[SIDE_ENEMY].field[battle.playerTarget] = -1;
        if (battle.trainer >= 0) {
            trainers[battle.trainer].numDefeated++;
            awardData(revisionDataForTrainer(&caught, trainers[battle.trainer].tier));
        }
        else awardData(revisionDataForWild(&caught) / 2);
        if (battleSideStanding(SIDE_ENEMY) == 0) victory();
        logLine("REPROGRAMMED %s! Added to team (%d/%d).", caught.name, teamSize, MAX_TEAM);
        fixTarget();
    }
    else logLine("HACK FAILED! %s rejected the intrusion.", d->mech->name);
    fixActor();
}

void battleConfirm(void) {
    if (battle.dialogue != DLG_NONE) {
        BattleDialogue was = battle.dialogue;
        battle.dialogue = DLG_NONE;
        if (was != DLG_DEFEAT) return;
    }
    else if (battle.animTimer > 0) return;
    if (battle.phase == BP_VICTORY || battle.phase == BP_DEFEAT) finish();
}
