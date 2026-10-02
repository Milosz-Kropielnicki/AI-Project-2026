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
static float fwEffect(const Mech* m, ChipEffect e) { return mechEffect(m, e); }   // chassis perk included

// A weapon's pattern as this attacker fires it: Wide Band turns its
// single-target scramblers into cones
static int targetingOf(const Combatant* a, const Weapon* w) {
    if (a && w->targeting == TARGET_SINGLE && w->scramble > 0 && fwEffect(a->mech, CFX_SCRAMBLE_ARC) > 0) return TARGET_CONE;
    return w->targeting;
}

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
    out->decoyMod = ctx->decoyMod > 0 ? ctx->decoyMod : 1.0f;   // Holo Decoy
    out->hitChance = clampf(out->hitUnclamped * out->spoofMod * out->decoyMod, HIT_CHANCE_MIN, HIT_CHANCE_MAX);

    // Raw damage, with the firmware multipliers
    out->baseDamage = w->baseDamage;
    out->power = atk->power;
    if (atk->integrity < atk->maxIntegrity * 0.5f) out->power += fwEffect(attacker, CFX_POWER_WHEN_DAMAGED);   // Aggressive Kernel
    out->powerBonus = ctx->powerBonus;   // Prowler Ambush
    out->power += out->powerBonus;
    out->executeMod = def->integrity < def->maxIntegrity * 0.5f ? 1.0f + fwEffect(attacker, CFX_EXECUTE) : 1.0f;
    out->overchargeMod = w->munition == MUN_ENERGY ? 1.0f + fwEffect(attacker, CFX_OVERCHARGE) : 1.0f;
    out->reductionMod = 1.0f - fwEffect(target, CFX_DAMAGE_REDUCTION);
    out->firstHitMod = ctx->firstHitOnTarget ? 1.0f - fwEffect(target, CFX_FIRST_HIT_SHIELD) : 1.0f;
    out->adaptiveMod = ctx->targetLastMunition == w->munition ? 1.0f - fwEffect(target, CFX_ADAPTIVE) : 1.0f;
    out->formatMod = ctx->formatMod > 0 ? ctx->formatMod : 1.0f;
    out->splashMod = ctx->splashMod > 0 ? ctx->splashMod : 1.0f;
    out->critMod = ctx->critMod > 0 ? ctx->critMod : 1.0f;
    out->guardMod = ctx->guardMod > 0 ? ctx->guardMod : 1.0f;
    out->critChance = ctx->critChance;
    out->linkAccuracy = ctx->linkAccuracy;
    out->perkMod = ctx->perkMod > 0 ? ctx->perkMod : 1.0f;
    out->auraMod = ctx->auraMod > 0 ? ctx->auraMod : 1.0f;
    out->cmdMod = ctx->cmdMod > 0 ? ctx->cmdMod : 1.0f;
    out->cmdAccuracy = ctx->cmdAccuracy;
    out->dmgMod = out->executeMod * out->overchargeMod * out->reductionMod * out->firstHitMod * out->adaptiveMod
        * out->formatMod * out->splashMod * out->critMod * out->guardMod * out->perkMod * out->auraMod * out->cmdMod;
    out->raw = formulaRawDamage(w->baseDamage, out->power) * out->dmgMod;
    out->pen = w->armorPen + (int)fwEffect(attacker, CFX_PEN_BONUS);                     // Siege Kernel
    if (def->armor > ARMORED_THRESHOLD) out->pen += (int)roundf(fwEffect(attacker, CFX_PEN_VS_ARMORED) * 100);   // Armor Analysis
    out->pen = clampi(out->pen, 0, 100);
    if (ctx->armorIgnore > 0) {   // flanking: that share of what Armor would stop goes through
        int before = out->pen;
        out->pen = clampi(out->pen + (int)roundf((100 - out->pen) * ctx->armorIgnore), 0, 100);
        out->flankPen = out->pen - before;
    }
    out->armorBefore = def->armor;
    out->split = formulaDamageSplit(out->raw, out->pen, def->armor);
    if (ctx->breachReady && def->armor > 0) {   // Armor Breach Routine
        int bonus = (int)roundf(out->split.toArmor * fwEffect(attacker, CFX_ARMOR_BREACH));
        int room = def->armor - out->split.armorDamage;
        out->breachBonus = bonus < room ? bonus : room;
    }
    float shred = fwEffect(attacker, CFX_ARMOR_SHRED);   // Plate Stripper: more Armor off every hit (never spills)
    if (shred > 0 && def->armor > 0) {
        int room = def->armor - out->split.armorDamage - out->breachBonus, extra = (int)roundf(out->split.armorDamage * shred);
        out->shredBonus = extra < room ? extra : (room > 0 ? room : 0);
    }
    out->armorDamage = out->split.armorDamage + out->breachBonus + out->shredBonus;
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
    c.armorIgnore = 0;
    c.linkAccuracy = 0;
    c.critChance = 0;
    c.critMod = 1.0f;
    c.guardMod = 1.0f;
    c.perkMod = 1.0f;
    c.auraMod = 1.0f;
    c.decoyMod = 1.0f;
    c.cmdAccuracy = 0;
    c.cmdMod = 1.0f;
    c.powerBonus = 0;
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
    float value = ai->damage * (p.armorDamage * ai->armorBias + p.integrityDamage) * p.hitChance
        * (1.0f + p.critChance * (CRIT_MULT - 1.0f));
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

static int logTotal = 0;
int battleLogCount(void) { return logSize; }
int battleLogTotal(void) { return logTotal; }
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
    logTotal++;
    snprintf(lastLogged, sizeof(lastLogged), "%s", text);
    return e;
}

// System messages are written straight into battle.log all over this file;
// anything new there is copied into the history.
static void syncLog(void) {
    if (battle.log[0] && strcmp(battle.log, lastLogged) != 0) logPush(battle.log, -1, -1);
}

static void logClear(void) { logHead = logSize = logTotal = 0; lastLogged[0] = 0; }

static int integrityBelow(const Combatant* c, float fraction) {
    return c->mech->stats.integrity < c->mech->stats.maxIntegrity * fraction;
}

int combatAccuracy(const Combatant* c) {
    int acc = c->mech->stats.accuracy - c->accPenalty;
    if (c->actionsThisTurn == 0) acc += (int)fwEffect(c->mech, CFX_PRECISION_STRIKE);
    acc += c->missStacks * (int)fwEffect(c->mech, CFX_RECURSIVE_TARGETING);
    if (c->relayTurns > 0) acc += c->relayAccuracy;   // an outgoing ally's Catcher Relay
    return clampi(acc, 0, 100);
}

int combatMobility(const Combatant* c) {
    int mob = c->mech->stats.mobility + c->evasiveBonus;
    if (integrityBelow(c, 0.25f)) mob += (int)fwEffect(c->mech, CFX_EMERGENCY_EVASION);
    if (c->flanking) mob -= FLANK_MOBILITY_PENALTY;   // exposed out on the flank
    mob -= c->slowed;                                 // a Sapper's snare field
    return clampi(mob, 0, 100) + (int)fwEffect(c->mech, CFX_TRUE_DODGE);   // Slipstream goes past the cap
}

// Scramble / corruption effects waiting on this side's next turn
static int pendingScrambles(const Combatant* c) {
    int n = c->nextSkipTurn + (c->nextDisabledWeapon >= 0) + (c->nextAccPenalty > 0) + c->nextEnergyLoss
        + c->nextEnergyTax + c->nextRandomTargeting;
    for (int i = 0; i < MAX_SOCKETS; i++) if (c->mech->fw.corrupt[i] > 0) n++;
    return n;
}

static float linkEffect(const Combatant* c, ChipEffect e, const Combatant* vs);
static int sideReserves(int side, int* out, int max);
static Combatant* guardOf(const Combatant* d, float* share);
static int sideOf(const Combatant* c);
static int slotOf(const Combatant* c);
static float sideAura(int side, ChipEffect e, const Combatant* except);

static LogEntry* logNote(const char* text, int side);

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
    ctx.armorIgnore = a->flanking ? FLANK_ARMOR_IGNORE + fwEffect(a->mech, CFX_FLANK_BOOST) : 0;
    ctx.linkAccuracy = (int)linkEffect(a, CFX_LINK_MARK_ACCURACY, d);   // on top of the 0-100 clamp
    ctx.attackerAccuracy += ctx.linkAccuracy + (int)fwEffect(a->mech, CFX_TRUE_AIM);   // both may pass 100
    ctx.auraMod = ctx.decoyMod = 1.0f;
    ctx.perkMod = 1.0f + (a->actionsThisTurn == 0 ? fwEffect(a->mech, CFX_OPENER) : 0)
        + (a->strikeReady ? fwEffect(a->mech, CFX_REPOSITION_STRIKE) : 0)
        + (a->fresh ? fwEffect(a->mech, CFX_FIRST_STRIKE) : 0)                    // Ambush
        + (!a->everMoved ? fwEffect(a->mech, CFX_ENTRENCHED) : 0)                 // Emplacement
        + ((d->hitBy & ~(1u << slotOf(a))) ? fwEffect(a->mech, CFX_PACK_HUNTER) : 0);   // an ally hit it first
    int ownMark = a->mark >= 0 && sideOf(d) != sideOf(a) && a->mark == slotOf(d);
    ctx.attackerAccuracy += (ownMark ? (int)fwEffect(a->mech, CFX_SELF_MARK) : 0)  // Hunter's Mark
        + (int)sideAura(sideOf(a), CFX_ACCURACY_AURA, a);                        // Foresight
    float aura = sideAura(sideOf(d), CFX_BARRIER_AURA, NULL)
        + (d->lane == LANE_FRONT && !d->flanking ? sideAura(sideOf(d), CFX_FRONT_AURA, NULL) : 0);
    ctx.auraMod = 1.0f - (aura < 0.5f ? aura : 0.5f);
    ctx.decoyMod = 1.0f - fwEffect(d->mech, CFX_DECOY);
    ctx.critChance = linkEffect(a, CFX_LINK_MARK_CRIT, d);
    if (a->fresh && fwEffect(a->mech, CFX_STEALTH_CRIT) > 0) ctx.critChance = 1.0f;   // Cloak: the opener crits
    ctx.critMod = 1.0f;
    float share = 0;
    ctx.guardMod = guardOf(d, &share) ? 1.0f - share : 1.0f;
    // Command Points: the attacker's side focusing this target, the target's side holding the line
    int as = sideOf(a), ds = sideOf(d);
    ctx.cmdAccuracy = as != ds && battle.focusTarget[as] == slotOf(d) ? FOCUS_FIRE_ACCURACY : 0;
    ctx.attackerAccuracy += ctx.cmdAccuracy;
    ctx.cmdMod = as != ds && battle.defensiveLine[ds] ? 1.0f - DEFENSIVE_LINE_CUT : 1.0f;
    ctx.powerBonus = a->deployed ? fwEffect(a->mech, CFX_DEPLOY_BUFF) : 0;   // Prowler Ambush
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

// A long sentence over as many lines as it needs, broken between words
static void sayWrapped(Explanation* x, int warn, const char* text) {
    const int width = EXPLAIN_LEN - 12;
    while (*text) {
        int n = (int)strlen(text);
        if (n > width) {
            n = width;
            while (n > 0 && text[n] != ' ') n--;
            if (n == 0) n = width;
        }
        say(x, warn, "%.*s", n, text);
        text += n;
        while (*text == ' ') text++;
    }
}

// The name to show for an effect: the chassis perk when only the frame has it,
// otherwise the chip / branch that usually grants it
static const char* effectName(const Mech* m, ChipEffect e, const char* usual) {
    const MechModel* mm = mechModel(m);
    return (mm->perk == e || mm->perk2 == e) && firmwareEffect(&m->fw, e) == 0 ? mm->perkName : usual;
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
    if (precision) snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", %s +%d",
        effectName(a->mech, CFX_PRECISION_STRIKE, "Precision Strike"), precision);
    if (recursive) snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", Recursive Targeting +%d", recursive);
    if (a->accPenalty) snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", scrambled -%d", a->accPenalty);
    if (a->relayTurns > 0 && a->relayAccuracy)
        snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", Catcher Relay +%d", a->relayAccuracy);
    if (p->linkAccuracy) snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", link +%d", p->linkAccuracy);
    int trueAim = (int)fwEffect(a->mech, CFX_TRUE_AIM);
    if (trueAim) snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", %s +%d", mechModel(a->mech)->perkName, trueAim);
    if (d->slowed) snprintf(mobWhy + strlen(mobWhy), sizeof(mobWhy) - strlen(mobWhy), ", slowed -%d", d->slowed);
    int dodge = (int)fwEffect(d->mech, CFX_TRUE_DODGE);
    if (dodge) snprintf(mobWhy + strlen(mobWhy), sizeof(mobWhy) - strlen(mobWhy), ", %s +%d", mechModel(d->mech)->perkName, dodge);
    int ownMark = a->mark >= 0 && sideOf(d) != sideOf(a) && a->mark == slotOf(d), auraAcc = (int)sideAura(sideOf(a), CFX_ACCURACY_AURA, a);
    if (ownMark && fwEffect(a->mech, CFX_SELF_MARK) > 0)
        snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", %s +%d", mechModel(a->mech)->perkName, (int)fwEffect(a->mech, CFX_SELF_MARK));
    if (auraAcc) snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", ally Foresight +%d", auraAcc);
    if (p->cmdAccuracy) snprintf(accWhy + strlen(accWhy), sizeof(accWhy) - strlen(accWhy), ", Focus Fire +%d", p->cmdAccuracy);
    if (d->evasiveBonus) snprintf(mobWhy + strlen(mobWhy), sizeof(mobWhy) - strlen(mobWhy), ", evading +%d", d->evasiveBonus);
    if (integrityBelow(d, 0.25f) && fwEffect(d->mech, CFX_EMERGENCY_EVASION) > 0)
        snprintf(mobWhy + strlen(mobWhy), sizeof(mobWhy) - strlen(mobWhy), ", Emergency Evasion +%d", (int)fwEffect(d->mech, CFX_EMERGENCY_EVASION));
    if (d->flanking) snprintf(mobWhy + strlen(mobWhy), sizeof(mobWhy) - strlen(mobWhy), ", flanking -%d", FLANK_MOBILITY_PENALTY);
    say(x, 0, "HIT %d%%: weapon %d%% x Accuracy %d%%%s", (int)roundf(p->hitChance * 100), p->weaponAcc, p->accuracy,
        accWhy[0] ? TextFormat(" (%s)", accWhy + 2) : "");
    say(x, 0, "   x target evasion: Mobility %d%%%s dodges %d%% of shots", p->mobility,
        mobWhy[0] ? TextFormat(" (%s)", mobWhy + 2) : "", (int)roundf(p->mobility / 2.0f));
    if (p->spoofMod < 1) say(x, 1, "   %s: first attack on target this round is x%.2f", effectName(d->mech, CFX_TARGETING_SPOOF, "Targeting Spoof"),
        p->spoofMod);
    if (p->decoyMod < 1) say(x, 1, "   %s: x%.2f to hit (a holo decoy draws the shot)", mechModel(d->mech)->perkName, p->decoyMod);
    if (p->hitUnclamped * p->spoofMod > HIT_CHANCE_MAX) say(x, 0, "   (capped at 95%% - nothing is certain)");
    if (p->hitUnclamped * p->spoofMod < HIT_CHANCE_MIN) say(x, 1, "   (floored at 5%% - a lucky shot is still possible)");

    // ---- damage ----
    if (p->baseDamage > 0) {
        char mods[112] = "";
        if (p->power > as->power + p->powerBonus + 0.001f)
            snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", Aggressive Kernel +%.2f PWR", p->power - as->power - p->powerBonus);
        if (p->powerBonus > 0) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", %s +%.2f PWR",
            effectName(a->mech, CFX_DEPLOY_BUFF, "Prowler Ambush"), p->powerBonus);
        if (p->executeMod > 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", %s +%d%% (target under half)",
            effectName(a->mech, CFX_EXECUTE, "Hunter"), (int)roundf((p->executeMod - 1) * 100));
        if (p->overchargeMod > 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", Overcharge +%d%%", (int)roundf((p->overchargeMod - 1) * 100));
        if (p->reductionMod < 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", target %s -%d%%",
            effectName(d->mech, CFX_DAMAGE_REDUCTION, "Bastion"), (int)roundf((1 - p->reductionMod) * 100));
        if (p->firstHitMod < 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", target Defensive Kernel -%d%%", (int)roundf((1 - p->firstHitMod) * 100));
        if (p->adaptiveMod < 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", adapted to %s -%d%%", munitionNames[w->munition], (int)roundf((1 - p->adaptiveMod) * 100));
        if (p->formatMod != 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", team battle x%.2f", p->formatMod);
        if (p->critMod > 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", CRITICAL x%.1f", p->critMod);
        if (p->perkMod > 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", %s +%d%%", mechModel(a->mech)->perkName,
            (int)roundf((p->perkMod - 1) * 100));
        if (p->auraMod < 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", ally shields -%d%%", (int)roundf((1 - p->auraMod) * 100));
        if (p->guardMod < 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", Defense Link x%.2f", p->guardMod);
        if (p->cmdMod < 1) snprintf(mods + strlen(mods), sizeof(mods) - strlen(mods), ", Defensive Line -%d%%", (int)roundf((1 - p->cmdMod) * 100));
        say(x, 0, "DAMAGE %.0f: %d base x %.2f Power%s", p->raw, p->baseDamage, p->power,
            p->dmgMod != 1 ? TextFormat(" x %.2f firmware", p->dmgMod) : "");
        if (mods[0]) say(x, 0, "   %s", mods + 2);
        if (p->splashMod < 1) say(x, 1, "   SPLASH: a secondary target of %s takes %d%% (-25%% per extra target)",
            targetingNames[targetingOf(a, w)], (int)roundf(p->splashMod * 100));
        const Combatant* guard = p->guardMod < 1 ? guardOf(d, NULL) : NULL;
        if (guard) say(x, 1, "   DEFENSE LINK: %s takes %d%% of this hit through its own Armor", guard->mech->name,
            (int)roundf((1 - p->guardMod) * 100));
        if (p->critChance > 0 && p->critMod <= 1)
            say(x, 0, "   CRIT %d%% (%s): a crit deals x%.1f", (int)roundf(p->critChance * 100),
                p->critChance >= 1 ? "Cloak - the first strike from stealth" : "Targeting Link vs the mark", CRIT_MULT);

        // ---- armor vs penetration ----
        int analysis = ds->armor > ARMORED_THRESHOLD ? (int)roundf(fwEffect(a->mech, CFX_PEN_VS_ARMORED) * 100) : 0;
        int siege = (int)fwEffect(a->mech, CFX_PEN_BONUS);
        const char* penWhy = analysis || siege || p->flankPen ? TextFormat(" (weapon %d%%%s%s%s)", w->armorPen,
            analysis ? TextFormat(" + %s %d", effectName(a->mech, CFX_PEN_VS_ARMORED, "Armor Analysis"), analysis) : "",
            siege ? TextFormat(" + %s %d", effectName(a->mech, CFX_PEN_BONUS, "Siege"), siege) : "",
            p->flankPen ? TextFormat(" + flank %d", p->flankPen) : "") : "";
        if (p->armorBefore <= 0)
            say(x, 0, "ARMOR: none left - all %.0f goes into Integrity", p->raw);
        else {
            say(x, 0, "PENETRATION %d%%%s: %.0f bypasses Armor, %.0f hits Armor", p->pen, penWhy, p->split.toIntegrity, p->split.toArmor);
            if (p->split.spill > 0) say(x, 1, "   Armor %d can't hold it: %d spills through to Integrity", p->armorBefore, p->split.spill);
            else if (p->pen < 25 && p->split.toArmor > p->split.toIntegrity * 2)
                say(x, 1, "   Mostly stopped by Armor - a higher-penetration weapon would hurt more");
            if (p->shredBonus > 0) say(x, 0, "   %s: +%d extra Armor damage", mechModel(a->mech)->perkName, p->shredBonus);
            if (p->breachBonus > 0) say(x, 0, "   %s: +%d extra Armor damage", effectName(a->mech, CFX_ARMOR_BREACH, "Armor Breach Routine"),
                p->breachBonus);
            if (p->flankPen > 0) say(x, 0, "   FLANKING: ignores %d%% of their Armor (+%d%% penetration)",
                (int)roundf(FLANK_ARMOR_IGNORE * 100), p->flankPen);
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

    // ---- swap effects (Force Swap hits only the primary target) ----
    int res[MAX_TEAM];
    if (p->splashMod >= 1 && (w->forceSwap || (!a->displaceUsed && fwEffect(a->mech, CFX_FORCE_SWAP) > 0))
        && sideReserves(sideOf(d), res, MAX_TEAM) > 0)
        say(x, 0, battleAnchored(d) ? "FORCE SWAP (%s): %s is locked in - it holds its ground"
            : "FORCE SWAP (%s): on hit, %s is pulled out for a random reserve", w->forceSwap ? w->name
            : effectName(a->mech, CFX_FORCE_SWAP, "Displacement Routine"), d->mech->name);
    if (fwEffect(a->mech, CFX_SWITCH_LOCK) > 0 && !battleAnchored(d))
        say(x, 0, "LOCKDOWN: on hit, %s can't switch out (or be forced out) on its next turn", d->mech->name);

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
    c->mark = -1;
    c->jammedBy = -1;
    c->interceptPos = -1;
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

// The side's healthiest standing reserve above `atLeast` (Integrity share), -1 if none
static int healthiestReserve(int side, float atLeast) {
    int res[MAX_TEAM], n = sideReserves(side, res, MAX_TEAM), best = -1;
    float bestFrac = atLeast;
    for (int i = 0; i < n; i++) {
        const MechStats* s = &battle.side[side].mech[res[i]].stats;
        float frac = s->maxIntegrity > 0 ? (float)s->integrity / s->maxIntegrity : 0;
        if (frac > bestFrac) { bestFrac = frac; best = res[i]; }
    }
    return best;
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

// ============ FORMATION ============
int battleCanFlank(const Combatant* c) {
    MechClass k = mechClass(c->mech);
    return k == CLASS_RECON || k == CLASS_EW;
}
int battleMoveCost(const Combatant* c) { return battleCanFlank(c) ? 0 : MOVE_ENERGY_COST; }

static int guards(const Combatant* c) { return c->lane == LANE_FRONT && !c->flanking; }
static int coverable(const Combatant* c) { return c->lane == LANE_REAR && !c->flanking && !c->provoking; }

int battleInterceptor(int side, int pos) {
    Combatant* d = battleField(side, pos);
#ifdef NO_FORMATION_TEST   // test builds only: nobody is ever covered, for comparison
    if (d) return -1;
#endif
    if (!d) return -1;
    if (!coverable(d)) {   // Shield Wall: a Front guard also covers the Front allies next to it
        if (d->lane != LANE_FRONT || d->flanking || d->provoking) return -1;
        for (int s = -1; s <= 1; s += 2) {
            Combatant* g = battleField(side, pos + s);
            if (g && guards(g) && fwEffect(g->mech, CFX_SHIELD_WALL) > 0) return pos + s;
        }
        return -1;
    }
    int best = -1;
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* g = battleField(side, p);
        if (!g || !guards(g)) continue;
        int dp = abs(p - pos), db = abs(best - pos);
        if (best < 0 || dp < db || (dp == db && g->mech->stats.integrity > battleField(side, best)->mech->stats.integrity)) best = p;
    }
    return best;
}

int battleDeclaredInterceptor(int side, int pos) {
    if (!battleField(side, pos)) return -1;
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* g = battleField(side, p);
        if (g && p != pos && g->interceptPos == pos && g->mech->stats.integrity > 0) return p;
    }
    return -1;
}

// Single-target and line shots are stopped by cover; area and cone reach past it
static int coverStops(const Combatant* a, const Weapon* w) { int t = targetingOf(a, w); return t == TARGET_SINGLE || t == TARGET_LINE; }

// The field position a weapon aimed at `aim` actually hits first. A Spotter
// Link lets the attacker reach its partner's mark past cover.
static int aimedAt(const Combatant* a, const Weapon* w, int side, int aim) {
    if (!w || !coverStops(a, w)) return aim;
    int ic = battleDeclaredInterceptor(side, aim);   // Intercept Protocol: declared, so no reach trick gets past it
    if (ic >= 0) return ic;
    if (a && linkEffect(a, CFX_LINK_MARK_REACH, battleField(side, aim)) > 0) return aim;
    if (a && fwEffect(a->mech, CFX_IGNORE_COVER) > 0) return aim;   // Infiltration
    Combatant* d = battleField(side, aim);
    if (d && integrityBelow(d, 0.30f))   // Last Line: an Aegis throws itself in front of a dying ally
        for (int p = 0; p < MAX_FIELD; p++) {
            Combatant* e = battleField(side, p);
            if (e && e != d && fwEffect(e->mech, CFX_EMERGENCY_COVER) > 0 && !integrityBelow(e, 0.25f)) return p;
        }
    int g = battleInterceptor(side, aim);
    return g >= 0 ? g : aim;
}

// Heavy Assault takes the front, Artillery and EW the rear; Recon holds the
// front only when no one else on its side does
static int defaultLane(int side, const Combatant* c) {
    switch (mechClass(c->mech)) {
    case CLASS_HEAVY_ASSAULT: return LANE_FRONT;
    case CLASS_RECON:
        for (int p = 0; p < MAX_FIELD; p++) {
            const Combatant* o = battleField(side, p);
            if (o && o != c && guards(o)) return LANE_REAR;
        }
        return LANE_FRONT;
    default: return LANE_REAR;
    }
}

// ============ COMBAT LINKS: STATE ============
const LinkDef linkDefs[NUM_LINK_TYPES] = {
    { "NONE", "", ROLE_NONE, CLASS_HEAVY_ASSAULT, CFX_NONE, 0, CFX_NONE, 0, { 0, 0, 0, 0 }, "" },
    { "TARGETING LINK", "TGT", ROLE_CATCHER, CLASS_ARTILLERY, CFX_LINK_MARK_ACCURACY, 15, CFX_LINK_MARK_CRIT, 0.10f,
      { 120, 255, 170, 255 }, "The Artillery gets +15 Accuracy (even past 100) and a 10% chance to crit for x1.5 damage against "
      "whatever the Catcher last aimed at." },
    { "DEFENSE LINK", "DEF", ROLE_AEGIS, CLASS_HEAVY_ASSAULT, CFX_LINK_GUARD, 0.20f, CFX_NONE, 0,
      { 130, 200, 255, 255 }, "The Aegis takes 20% of every hit on the Heavy Assault, through its own Armor." },
    { "SPOTTER LINK", "SPOT", ROLE_SCOUT, CLASS_ARTILLERY, CFX_LINK_MARK_ACCURACY, 10, CFX_LINK_MARK_REACH, 1,
      { 255, 210, 90, 255 }, "The Artillery gets +10 Accuracy against whatever the Scout last aimed at, and its single-target "
      "and line shots reach that mech even when it is covered in the Rear." },
    { "SIGNAL BLACKOUT", "BLK", ROLE_DISRUPTOR, CLASS_RECON, CFX_LINK_BLACKOUT, 1, CFX_NONE, 0,
      { 200, 150, 255, 255 }, "Mechs the Disruptor jams (a scramble that lands) can't target the Recon until after their next turn." },
};

static int sideOf(const Combatant* c) {
    const Combatant* p = battle.side[SIDE_PLAYER].slot;
    return c >= p && c < p + MAX_TEAM ? SIDE_PLAYER : SIDE_ENEMY;
}
static int slotOf(const Combatant* c) { return (int)(c - battle.side[sideOf(c)].slot); }

// Sum of an aura effect over a side's field mechs (except one)
static float sideAura(int side, ChipEffect e, const Combatant* except) {
    float v = 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        const Combatant* c = battleField(side, p);
        if (c && c != except) v += fwEffect(c->mech, e);
    }
    return v;
}
static int fieldPosOf(int side, int slot) {
    for (int p = 0; slot >= 0 && p < MAX_FIELD; p++) if (battle.side[side].field[p] == slot) return p;
    return -1;
}

int battleLinkInitiator(const Combatant* c) {
#ifdef NO_LINKS_TEST   // test builds only: the player's mechs never link, for comparison
    if (sideOf(c) == SIDE_PLAYER) return LINK_NONE;
#endif
    for (int t = 1; t < NUM_LINK_TYPES; t++) if (mechRole(c->mech) == linkDefs[t].initiator) return t;
    return LINK_NONE;
}
static int linkTypeFor(const Combatant* from, const Combatant* to) {
    int t = battleLinkInitiator(from);
    return t != LINK_NONE && mechClass(to->mech) == linkDefs[t].partner ? t : LINK_NONE;
}
// Partner of slot's outgoing link / initiator of its incoming link, -1 if none
static int linkOut(int side, int slot, int* type) {
    for (int j = 0; j < MAX_TEAM; j++)
        if (battle.side[side].link[slot][j]) { if (type) *type = battle.side[side].link[slot][j]; return j; }
    return -1;
}
static int linkIn(int side, int slot, int* type) {
    for (int i = 0; i < MAX_TEAM; i++)
        if (battle.side[side].link[i][slot]) { if (type) *type = battle.side[side].link[i][slot]; return i; }
    return -1;
}

int battleLinks(int side, ActiveLink* out, int max) {
    int n = 0;
    for (int i = 0; i < MAX_TEAM; i++)
        for (int j = 0; j < MAX_TEAM && n < max; j++)
            if (battle.side[side].link[i][j]) out[n++] = (ActiveLink){ i, j, battle.side[side].link[i][j] };
    return n;
}

float battleLinkScale(int side, int from) {
    return 1.0f + fwEffect(battle.side[side].slot[from].mech, CFX_LINK_HUB)
        + (battle.linkBoostTurns[side] > 0 ? battle.linkBoost[side] : 0);   // Link Amplifier
}

int battleMarkPos(int side, int slot) {
    int m = battle.side[side].slot[slot].mark;
    return m >= 0 ? fieldPosOf(1 - side, m) : -1;
}

static int isMarkEffect(ChipEffect e) { return e == CFX_LINK_MARK_ACCURACY || e == CFX_LINK_MARK_CRIT || e == CFX_LINK_MARK_REACH; }

// What links grant a mech, resolved like a chip effect: the effects of its
// incoming link. Mark effects only count against the initiator's mark (vs).
static float linkEffect(const Combatant* c, ChipEffect e, const Combatant* vs) {
    int side = sideOf(c), type, from = linkIn(side, slotOf(c), &type);
    if (from < 0) return 0;
    const LinkDef* L = &linkDefs[type];
    if (isMarkEffect(e)) {
        int m = battleMarkPos(side, from);
        if (!vs || m < 0 || battleField(1 - side, m) != vs) return 0;
    }
    return ((L->effect == e ? L->value : 0) + (L->effect2 == e ? L->value2 : 0)) * battleLinkScale(side, from);
}

// CFX_LINK_GUARD: the initiator guarding d, and the share of each hit it takes
static Combatant* guardOf(const Combatant* d, float* share) {
    int side = sideOf(d), type, from = linkIn(side, slotOf(d), &type);
    if (from < 0 || linkDefs[type].effect != CFX_LINK_GUARD) return NULL;
    if (share) *share = linkDefs[type].value * battleLinkScale(side, from);
    return &battle.side[side].slot[from];
}

// CFX_LINK_BLACKOUT: a was jammed by the initiator linked to d
static int hiddenFrom(const Combatant* a, const Combatant* d) {
    if (!a || !d) return 0;
    if (d->fresh && fwEffect(d->mech, CFX_STEALTH) > 0 && sideOf(a) != sideOf(d)) return 1;   // Unseen until it strikes
    if (a->jammed <= 0) return 0;
    int type, from = linkIn(sideOf(d), slotOf(d), &type);
    return from >= 0 && linkDefs[type].effect == CFX_LINK_BLACKOUT && a->jammedBy == from;
}
int battleHidden(int pos) { return hiddenFrom(battleActing(), battleField(SIDE_ENEMY, pos)); }

// ============ TARGETS ============
// Enemy field positions a weapon reaches when aimed at `primary`, with each
// one's damage share: AREA covers the whole field, CONE the target and the
// positions next to it, anything else only the target. A single-target or line
// weapon aimed at a covered Rear mech hits its Front guard instead. Extra targets are
// ordered nearest first and each takes SPLASH_FALLOFF less (compounding).
static int weaponTargets(const Combatant* a, const Weapon* w, int side, int primary, int* pos, float* mod, int max) {
    int n = 0;
    if (!w || !battleField(side, primary) || max < 1) return 0;
    primary = aimedAt(a, w, side, primary);
    pos[n] = primary;
    mod[n++] = 1.0f;
    int pattern = targetingOf(a, w);
    if (pattern != TARGET_AREA && pattern != TARGET_CONE) return n;
    float m = 1.0f;
    for (int d = 1; d < MAX_FIELD; d++) {
        if (pattern == TARGET_CONE && d > 1) break;
        for (int s = -1; s <= 1 && n < max; s += 2) {
            int p = primary + s * d;
            if (!battleField(side, p)) continue;
            m *= SPLASH_FALLOFF + (a ? fwEffect(a->mech, CFX_SPLASH_BOOST) : 0);   // Saturation falls off less
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

// Context for one target of a shot: splash, and Overwatch when an
// interceptor takes a shot meant for someone else
static AttackContext hitContext(const Combatant* a, const Combatant* d, float splash, int k, int intercepted) {
    AttackContext ctx = splashContext(a, d, splash, k > 0);
    if (k == 0 && intercepted) ctx.guardMod *= 1.0f - fwEffect(d->mech, CFX_INTERCEPT_GUARD);
    return ctx;
}

int battlePreviewTargets(int mount, int* pos, AttackPreview* out, int max) {
    Combatant* a = battleActing();
    const Weapon* w = a ? mechWeapon(a->mech, mount) : NULL;
    int p[MAX_FIELD];
    float mod[MAX_FIELD];
    int n = weaponTargets(a, w, SIDE_ENEMY, battle.playerTarget, p, mod, max < MAX_FIELD ? max : MAX_FIELD);
    for (int k = 0; k < n; k++) {
        Combatant* d = battleField(SIDE_ENEMY, p[k]);
        AttackContext ctx = hitContext(a, d, mod[k], k, p[0] != battle.playerTarget);
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
    Combatant* aim = battleTarget();
    int pos[MAX_FIELD];
    AttackPreview all[MAX_FIELD];
    int n = a && aim ? battlePreviewTargets(mount, pos, all, MAX_FIELD) : 0;
    if (n < 1) return 0;
    const Weapon* w = mechWeapon(a->mech, mount);
    explainAttack(a, battleField(SIDE_ENEMY, pos[0]), w, &all[0], out);
    if (pos[0] != battle.playerTarget && battleDeclaredInterceptor(SIDE_ENEMY, battle.playerTarget) == pos[0])
        say(out, 1, "INTERCEPTED: %s guards %s's position - single-target and line shots hit it instead. Area and cone weapons reach past.",
            battleField(SIDE_ENEMY, pos[0])->mech->name, aim->mech->name);
    else if (pos[0] != battle.playerTarget)
        say(out, 1, "COVERED: %s is in the Rear - %s in the Front takes single-target and line shots. Area and cone weapons reach it.",
            aim->mech->name, battleField(SIDE_ENEMY, pos[0])->mech->name);
    else if (battleInterceptor(SIDE_ENEMY, battle.playerTarget) >= 0)
        say(out, 0, !coverStops(a, w) ? TextFormat("REACHES THE REAR: %s weapons ignore the Front guard.", targetingNames[targetingOf(a, w)])
            : fwEffect(a->mech, CFX_IGNORE_COVER) > 0 ? "INFILTRATION: this mech's shots reach past the Front guard."
            : "SPOTTED: your Spotter Link calls this shot past the Front guard.");
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

// ============ THREAT ============
void battleAddThreat(Combatant* c, int amount) {
    if (c) c->threat = clampi(c->threat + amount, 0, THREAT_MAX);
}

#ifdef NO_THREAT_TEST   // test builds only: targeting without threat, for comparison
float battleThreatFactor(const Combatant* c) { (void)c; return 1.0f; }
#else
float battleThreatFactor(const Combatant* c) { return 1.0f + c->threat / 100.0f; }
#endif

int battleProvoker(int side) {
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* c = battleField(side, p);
        if (c && c->provoking) return p;
    }
    return -1;
}

// Area and cone weapons reach past a provoker; everything else is single-target
static int singleTarget(const Combatant* a, const Weapon* w) { int t = targetingOf(a, w); return t != TARGET_AREA && t != TARGET_CONE; }

// Best (weapon, target) for this attacker against the other side's field. Each
// target's value is multiplied by its threat factor (1 + threat / 100); area and
// cone weapons add their splash targets the same way. While the other side has
// a provoker, single-target weapons may only aim at it; covered Rear mechs are
// only reachable with area and cone weapons. ignoreResources skips
// the Energy / Heat / scramble checks (Dead-Man Protocol's free shot). If why is
// given, it gets a plain explanation of the pick.
static int aiChooseAction(const Combatant* a, int targetSide, int ignoreResources, int* targetPos, float* bestScore,
                          char* why, int whySize);

// Teamwork for link initiators: what aiming at each position on the other side
// is worth to the partner. A Catcher or Scout marks what it aims at, so the
// gain is how much better the partner's best shot gets with that mark (zero
// once the partner has acted). A Disruptor's jam blinds a mech to its Recon, so
// the gain is what that mech would otherwise do to the Recon. Returns the link's
// kind: 0 none, 1 mark, 2 jam.
static int linkAimBonus(const Combatant* a, int targetSide, float* bonus) {
    static int busy = 0;   // the partner's own choice doesn't recurse
    for (int t = 0; t < MAX_FIELD; t++) bonus[t] = 0;
    int side = sideOf(a), type, partner = linkOut(side, slotOf(a), &type);
    if (busy || partner < 0) return 0;
    const LinkDef* L = &linkDefs[type];
    Combatant* b = &battle.side[side].slot[partner];
    int kind = isMarkEffect(L->effect) || isMarkEffect(L->effect2) ? 1 : L->effect == CFX_LINK_BLACKOUT ? 2 : 0;
    busy = 1;
    if (kind == 1 && !b->done) {
        Combatant* m = (Combatant*)a;
        int keep = m->mark;
        float base = 0, with = 0;
        aiChooseAction(b, targetSide, 0, NULL, &base, NULL, 0);
        for (int t = 0; t < MAX_FIELD; t++) {
            if (!battleField(targetSide, t)) continue;
            m->mark = battle.side[targetSide].field[t];
            aiChooseAction(b, targetSide, 0, NULL, &with, NULL, 0);
            bonus[t] = with > base ? with - base : 0;
        }
        m->mark = keep;
    }
    else if (kind == 2) {
        int pp = fieldPosOf(side, partner);
        for (int t = 0; t < MAX_FIELD; t++) {
            Combatant* e = battleField(targetSide, t);
            int at = -1;
            float score = 0;
            if (!e || e->jammed > 0) continue;
            aiChooseAction(e, side, 0, &at, &score, NULL, 0);
            if (at == pp && score > 0) bonus[t] = score;
        }
    }
    busy = 0;
    return kind;
}

// onlyPos >= 0: that target only, and a covered aim still counts (the shot hits
// its guard). ignoreResources 2: a free shot - weapons are weighed as if they
// cost no Energy, and a scrambled mount still can't fire.
static int aiChooseActionAt(const Combatant* a, int targetSide, int onlyPos, int ignoreResources, int* targetPos,
                            float* bestScore, char* why, int whySize) {
    int best = -1, bestPos = -1, provoker = battleProvoker(targetSide);
    float bestValue = -1, perTarget[MAX_FIELD], rawOf[MAX_FIELD], aimBonus[MAX_FIELD];
    int linkKind = linkAimBonus(a, targetSide, aimBonus);
    for (int t = 0; t < MAX_FIELD; t++) perTarget[t] = rawOf[t] = -1;
    for (int t = 0; t < MAX_FIELD; t++) {
        if (onlyPos >= 0 && t != onlyPos) continue;
        if (!battleField(targetSide, t) || hiddenFrom(a, battleField(targetSide, t))) continue;   // Signal Blackout
        for (int i = 0; i < MAX_WEAPONS; i++) {
            const Weapon* w = mechWeapon(a->mech, i);
            if (!w) continue;
            if (ignoreResources ? (w->ammo > 0 && a->mech->weapons[i].ammo <= 0) : !canFire(a, i, NULL)) continue;
            if (ignoreResources == 2 && i == a->disabledWeapon) continue;
            if (provoker >= 0 && t != provoker && singleTarget(a, w)) continue;
            // covered: same as aiming at its guard - unless the aim itself is worth something (a mark)
            if (onlyPos < 0 && aimedAt(a, w, targetSide, t) != t && !(linkKind == 1 && aimBonus[t] > 0)) continue;
            float free = 1;   // a free shot: undo the per-Energy weighting
            if (ignoreResources == 2) { int k = weaponCost(a->mech, w, a->actionsThisTurn == 0) + a->energyTax; free = k > 0 ? (float)k : 0.5f; }
            int pos[MAX_FIELD];
            float mod[MAX_FIELD], v = 0, raw = 0;
            int n = weaponTargets(a, w, targetSide, t, pos, mod, MAX_FIELD);
            for (int k = 0; k < n; k++) {
                const Combatant* d = battleField(targetSide, pos[k]);
                AttackContext ctx = splashContext(a, d, mod[k], k > 0);
                float s = aiScoreAttack(a->mech, w, d->mech, &ctx, a->ai) * free;
                raw += s;
                v += s * battleThreatFactor(d);
                if (k == 0 && linkKind == 2 && w->scramble > 0 && aimBonus[pos[0]] > 0) {   // jam it before it shoots the Recon
                    AttackPreview jp;
                    attackPreview(a->mech, w, d->mech, &ctx, &jp);
                    v += aimBonus[pos[0]] * jp.hitChance * (1 - jp.resist);
                }
            }
            if (linkKind == 1) v += aimBonus[t];   // marking it for the partner
            if (v > perTarget[t]) { perTarget[t] = v; rawOf[t] = raw; }
            if (v > bestValue) { bestValue = v; best = i; bestPos = t; }
        }
    }
    if (targetPos) *targetPos = bestPos;
    if (bestScore) *bestScore = bestValue;
    if (why && whySize > 0) {
        why[0] = 0;
        if (best >= 0) {
            const Combatant* d = battleField(targetSide, bestPos);
            int runner = -1;
            for (int t = 0; t < MAX_FIELD; t++) if (t != bestPos && perTarget[t] >= 0 && (runner < 0 || perTarget[t] > perTarget[runner])) runner = t;
            if (provoker >= 0 && bestPos == provoker && singleTarget(a, mechWeapon(a->mech, best)))
                snprintf(why, whySize, "TARGET: %s is PROVOKING - single-target shots must aim at it", d->mech->name);
            else if (runner >= 0)
                snprintf(why, whySize, "TARGET: %s, priority %.0f (value %.0f x threat %d = x%.2f) over %s %.0f (threat %d)",
                    d->mech->name, perTarget[bestPos], rawOf[bestPos], d->threat, battleThreatFactor(d),
                    battleField(targetSide, runner)->mech->name, perTarget[runner], battleField(targetSide, runner)->threat);
            else snprintf(why, whySize, "TARGET: %s, the only one in reach (threat %d)", d->mech->name, d->threat);
        }
    }
    return best;
}
static int aiChooseAction(const Combatant* a, int targetSide, int ignoreResources, int* targetPos, float* bestScore,
                          char* why, int whySize) {
    return aiChooseActionAt(a, targetSide, -1, ignoreResources, targetPos, bestScore, why, whySize);
}

// Start of this mech's turn: tick corruption, refill Energy, cool Heat, then
// (on the field only) apply queued scrambles and run Emergency and Behavioral
// (IF/THEN) chips. A benched mech keeps its queued scrambles for its next turn
// on the field. Writes a log note.
static void turnStart(Combatant* c, int onField, char* note, int size) {
    MechStats* s = &c->mech->stats;
    note[0] = 0;
    if (c->jammed > 0) c->jammed--;   // Signal Blackout: active through the jammed mech's next turn
    c->slowed = 0;                    // a snare field lasts until its next turn
    c->strikeReady = 0;
    c->displaceUsed = 0;
    c->interceptPos = -1;             // an intercept lasts until its next turn
    if (c->relayTurns > 0) c->relayTurns--;   // Catcher Relay: through the newcomer's first turn
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
    c->hitBy = 0;
    c->attackThreat = 0;
    c->overwatch = 0;   // an Overwatch nobody triggered lapses at its own side's next phase
    if (!onField) {
        c->hazard = 0;   // a hazard field doesn't follow a mech off the field
        c->accPenalty = c->skipTurn = c->energyTax = c->randomTargeting = c->switchLocked = 0;
        c->disabledWeapon = -1;
        return;
    }
    if (fwEffect(c->mech, CFX_CLEANSE) > 0)   // Decryptor: its side's corruption wears off once more
        for (int p = 0; p < MAX_FIELD; p++) {
            Combatant* o = battleField(sideOf(c), p);
            if (!o || o == c) continue;
            firmwareCorruptionTick(&o->mech->fw);
            mechRefreshStats(o->mech);
        }
    c->done = 0;
    c->provoking = 0;   // Provocation lasts until the provoker's next turn
    c->flanking = 0;    // so does the flank
    c->moved = 0;
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
        battleAddThreat(c, THREAT_REPAIR);
    }
    int vent = (int)fwEffect(c->mech, CFX_COOLANT_DUMP);
    if (vent > 0 && s->heat > s->maxHeat * 0.75f && s->energy >= 1) {
        s->energy -= 1;
        s->heat = s->heat > vent ? s->heat - vent : 0;
        snprintf(note + strlen(note), size - strlen(note), " COOLANT DUMP -%d HEAT.", vent);
        battleAddThreat(c, THREAT_BUFF);
    }
    if (c->hazard > 0) {   // a Sapper's hazard field wears it down (never below 1 Integrity)
        int dmg = c->hazard < s->integrity ? c->hazard : s->integrity - 1;
        s->integrity -= dmg;
        snprintf(note + strlen(note), size - strlen(note), " HAZARD -%d INT.", dmg);
        c->hazard = 0;
    }
}

// Every mech a side has in the battle ticks, reserves too. Returns the field
// mechs' notes.
static const char* sideTurnStart(int side) {
    static char notes[200];
    char note[64];
    Side* sd = &battle.side[side];
    notes[0] = 0;
    if (battle.linkBoostTurns[side] > 0 && --battle.linkBoostTurns[side] == 0)
        logNote(TextFormat("%sLINK AMPLIFIER fades: links back to normal strength.", side == SIDE_PLAYER ? "" : "Enemy "), side == SIDE_PLAYER);
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
    case FX_GUARD: return 0.5f;
    case FX_LINK: return 0.7f;
    case FX_COMMAND: return 0.6f;
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

// ============ COMBAT LINKS: FORMING AND BREAKING ============
// A history entry that leaves the log strip alone
static LogEntry* logNote(const char* text, int side) {
    char keep[256];
    snprintf(keep, sizeof(keep), "%s", lastLogged);
    LogEntry* e = logPush(text, side, -1);
    snprintf(lastLogged, sizeof(lastLogged), "%s", keep);
    return e;
}

static int canHoldLink(int side, int slot) {
    return battleSlotStanding(side, slot) && slotOnField(&battle.side[side], slot)
        && !integrityBelow(&battle.side[side].slot[slot], LINK_BREAK_BELOW);
}

// `from` can link to `to` now (to may already be its partner)
static int linkEligible(int side, int from, int to) {
    if (from == to || !canHoldLink(side, from) || !canHoldLink(side, to)) return 0;
    if (linkTypeFor(&battle.side[side].slot[from], &battle.side[side].slot[to]) == LINK_NONE) return 0;
    int in = linkIn(side, to, NULL);
    return in < 0 || in == from;
}

static LogEntry* setLink(int side, int from, int to) {
    Side* sd = &battle.side[side];
    int type = linkTypeFor(&sd->slot[from], &sd->slot[to]);
    for (int j = 0; j < MAX_TEAM; j++) sd->link[from][j] = LINK_NONE;
    sd->link[from][to] = (unsigned char)type;
    const LinkDef* L = &linkDefs[type];
    LogEntry* e = logNote(TextFormat("%s%s: %s > %s.", side == SIDE_PLAYER ? "" : "Enemy ", L->name, sd->mech[from].name,
        sd->mech[to].name), side == SIDE_PLAYER);
    sayWrapped(&e->why, 0, L->rule);
    say(&e->why, 0, "It breaks if either mech drops below 25%% Integrity, is switched out or is disabled.");
    pushEvent(FX_LINK, side == SIDE_PLAYER, fieldPosOf(side, from), fieldPosOf(side, to), 0, 1, L->color);
    return e;
}

// Breaks every link a mech can no longer hold: below 25% Integrity, off the
// field, disabled
static void linkUpkeep(void) {
    for (int side = 0; side < 2; side++) {
        Side* sd = &battle.side[side];
        for (int i = 0; i < MAX_TEAM; i++)
            for (int j = 0; j < MAX_TEAM; j++) {
                if (!sd->link[i][j] || (canHoldLink(side, i) && canHoldLink(side, j))) continue;
                int bad = canHoldLink(side, i) ? j : i;
                const char* why = !battleSlotStanding(side, bad) ? "is out of the fight"
                    : !slotOnField(sd, bad) ? "switched out" : "dropped below 25% Integrity";
                logNote(TextFormat("%s%s LOST: %s > %s (%s %s).", side == SIDE_PLAYER ? "" : "Enemy ", linkDefs[sd->link[i][j]].name,
                    sd->mech[i].name, sd->mech[j].name, sd->mech[bad].name, why), side == SIDE_PLAYER);
                sd->link[i][j] = LINK_NONE;
            }
    }
}

// Links form by themselves at battle start and when a mech takes the field:
// each initiator on the field without a link picks a partner (onlySlot >= 0:
// only pairs that involve that mech). An Aegis prefers a Front guard, the
// others the hardest-hitting partner.
static void autoLink(int side, int onlySlot) {
    Side* sd = &battle.side[side];
    for (int p = 0; p < MAX_FIELD; p++) {
        int i = sd->field[p];
        if (i < 0 || linkOut(side, i, NULL) >= 0 || battleLinkInitiator(&sd->slot[i]) == LINK_NONE) continue;
        int best = -1;
        float bestScore = -1;
        for (int q = 0; q < MAX_FIELD; q++) {
            int j = sd->field[q];
            if (j < 0 || !linkEligible(side, i, j) || (onlySlot >= 0 && i != onlySlot && j != onlySlot)) continue;
            const Combatant* o = &sd->slot[j];
            float score = battleLinkInitiator(&sd->slot[i]) == LINK_DEFENSE ? guards(o) * 1000.0f + o->threat : o->mech->stats.power * 100;
            if (score > bestScore) { bestScore = score; best = j; }
        }
        if (best >= 0) setLink(side, i, best);
    }
}

int battleLinkCost(const Combatant* c) {
    int k = LINK_ENERGY_COST - (int)fwEffect(c->mech, CFX_LINK_DISCOUNT);
    return k > 0 ? k : 0;
}

// The partner the LINK action picks: the next eligible one after the current
// partner, in field order
static int nextLinkPartner(int side, int from) {
    int cur = linkOut(side, from, NULL), start = cur >= 0 ? fieldPosOf(side, cur) + 1 : 0;
    for (int k = 0; k < MAX_FIELD; k++) {
        int j = battle.side[side].field[(start + k) % MAX_FIELD];
        if (j >= 0 && j != cur && linkEligible(side, from, j)) return j;
    }
    return -1;
}

static const char* linkBlock(int side, const Combatant* c) {
    if (battleLinkInitiator(c) == LINK_NONE) return "ONLY CATCHER / AEGIS / SCOUT / DISRUPTOR";
    if (integrityBelow(c, LINK_BREAK_BELOW)) return "BELOW 25% INTEGRITY";
    if (c->mech->stats.energy < battleLinkCost(c)) return "INSUFFICIENT ENERGY";
    if (nextLinkPartner(side, slotOf(c)) < 0) return linkOut(side, slotOf(c), NULL) >= 0 ? "NO OTHER PARTNER" : "NO PARTNER ON THE FIELD";
    return NULL;
}

static void doLink(Combatant* c, int side) {
    int to = nextLinkPartner(side, slotOf(c));
    int cost = battleLinkCost(c);
    c->mech->stats.energy -= cost;
    c->actionsThisTurn++;
    LogEntry* e = setLink(side, slotOf(c), to);
    say(&e->why, 0, "LINK action: %d EN.", cost);
    snprintf(battle.log, sizeof(battle.log), "%s", e->text);
    snprintf(lastLogged, sizeof(lastLogged), "%s", e->text);
    battle.animTimer = 0.6f;
}

int battleCanLink(const char** reason) {
    const char* r = NULL;
    Combatant* a = battleActing();
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) r = "STANDBY";
    else if (!a) r = "NO MECH TO COMMAND";
    else r = linkBlock(SIDE_PLAYER, a);
    if (reason) *reason = r;
    return r == NULL;
}

int battleLinkCandidate(void) {
    Combatant* a = battleActing();
    return a && battleLinkInitiator(a) != LINK_NONE ? nextLinkPartner(SIDE_PLAYER, slotOf(a)) : -1;
}

void battleLink(void) {
    if (!battleCanLink(NULL)) return;
    doLink(battleActing(), SIDE_PLAYER);
    battle.phaseActed = 1;
}

// AI: an initiator with no link re-links before it acts
static int aiConsiderLink(int side, int pos) {
    Combatant* c = battleField(side, pos);
    if (!c || c->actionsThisTurn > 0 || linkOut(side, slotOf(c), NULL) >= 0 || linkBlock(side, c)) return 0;
    doLink(c, side);
    return 1;
}

// ============ ACTIONS ============
static char targetWhy[160];   // why the AI picked its target; goes into that attack's log entry
static int overwatchTrigger(int side, int slot);
static const char* fireBlock(int mount);

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
    if (strength >= 100 && d->skipImmune <= 0 && fwEffect(d->mech, CFX_SKIP_IMMUNE) <= 0) { d->nextSkipTurn = 1; return "TURN LOST"; }
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
    int n = weaponTargets(a, w, dSide, primary, pos, mod, MAX_FIELD);
    if (n == 0) return;
    // Formation, as it stood when the shot was fired
    const char* coveredName = pos[0] != primary ? battleField(dSide, primary)->mech->name : NULL;
    char formation[EXPLAIN_LEN] = "";
    if (coveredName && battleDeclaredInterceptor(dSide, primary) == pos[0])
        snprintf(formation, sizeof(formation), "INTERCEPTED: %s stepped in front of %s and took the %s shot",
            battleField(dSide, pos[0])->mech->name, coveredName, w->targeting == TARGET_LINE ? "line" : "single-target");
    else if (coveredName)
        snprintf(formation, sizeof(formation), "COVERED: %s is covered, so %s took the %s shot",
            coveredName, battleField(dSide, pos[0])->mech->name, w->targeting == TARGET_LINE ? "line" : "single-target");
    else if (battleInterceptor(dSide, primary) >= 0 && coverStops(a, w))
        snprintf(formation, sizeof(formation), fwEffect(a->mech, CFX_IGNORE_COVER) > 0
            ? "INFILTRATION: its shots reach past the Front guard" : "SPOTTED: the Spotter Link calls this shot past the Front guard");
    else if (battleInterceptor(dSide, primary) >= 0)
        snprintf(formation, sizeof(formation), "REACHES THE REAR: %s weapons ignore the Front guard", targetingNames[targetingOf(a, w)]);
    else if (coverStops(a, w))
        for (int t = 0; t < MAX_FIELD; t++)
            if (battleInterceptor(dSide, t) >= 0) {
                snprintf(formation, sizeof(formation), "FORMATION: %s is covered in the Rear - only area and cone weapons reach it",
                    battleField(dSide, t)->mech->name);
                break;
            }

    a->mark = battle.side[dSide].field[primary];   // what a link initiator aims at is its mark

    // Previews and explanations first, before anything changes
    AttackPreview p[MAX_FIELD];
    Explanation why[MAX_FIELD];
    for (int k = 0; k < n; k++) {
        Combatant* d = battleField(dSide, pos[k]);
        AttackContext ctx = hitContext(a, d, mod[k], k, pos[0] != primary);
        attackPreview(a->mech, w, d->mech, &ctx, &p[k]);
        explainAttack(a, d, w, &p[k], &why[k]);
    }

    MechStats* as = &a->mech->stats;
    as->energy -= p[0].energyCost;
    as->heat += p[0].heat;
    if (w->ammo > 0) a->mech->weapons[mount].ammo--;
    char who[48], first[256] = "";
    snprintf(who, sizeof(who), "%s%s%s", isPlayer ? "" : "Enemy ", a->mech->name, a->flanking ? " (FLANK)" : "");

    for (int k = 0; k < n; k++) {
        Combatant* d = battleField(dSide, pos[k]);
        MechStats* ds = &d->mech->stats;
        d->attackedThisRound = 1;
        float roll = frand(), critRoll = -1;
        int hit = roll < p[k].hitChance, crit = 0;
        if (hit && p[k].critChance > 0) {   // Targeting Link: the numbers again at x1.5
            critRoll = frand();
            crit = critRoll < p[k].critChance;
            if (crit) {
                AttackContext cc = hitContext(a, d, mod[k], k, pos[0] != primary);
                cc.critMod = CRIT_MULT;
                attackPreview(a->mech, w, d->mech, &cc, &p[k]);
                explainAttack(a, d, w, &p[k], &why[k]);   // the breakdown shows the crit's numbers
            }
        }
        int lethalBefore = p[k].integrityDamage >= ds->integrity;
        float scrambleRoll = -1;
        int total = 0, guardPos = -1, guardTotal = 0;
        AttackPreview gp;
        char extra[160] = "", line[256], guardLine[160] = "";
        if (hit) {
            ds->armor -= p[k].armorDamage;
            ds->integrity -= p[k].integrityDamage;
            if (ds->integrity < 0) ds->integrity = 0;
            if (p[k].breachBonus > 0) a->breachUsed = 1;
            total = p[k].armorDamage + p[k].integrityDamage;
            if (total > 0) {
                d->hitTaken = 1;
                d->lastMunitionTaken = w->munition;
                d->hitBy |= 1u << slotOf(a);
            }
            if (p[k].scramble > 0 && ds->integrity > 0) {
                scrambleRoll = frand();
                if (scrambleRoll < p[k].resist) {
                    int heal = (int)fwEffect(d->mech, CFX_SYSTEM_RECOVERY);
                    ds->integrity = clampi(ds->integrity + heal, 0, ds->maxIntegrity);
                    if (heal > 0) battleAddThreat(d, THREAT_REPAIR);
                    snprintf(extra, sizeof(extra), " Scramble resisted%s.", heal > 0 ? " (recovered)" : "");
                }
                else {
                    snprintf(extra, sizeof(extra), " SCRAMBLED: %s!", applyScramble(d, p[k].scramble, w->virus));
                    int type, partner = linkOut(aSide, slotOf(a), &type);
                    if (partner >= 0 && linkDefs[type].effect == CFX_LINK_BLACKOUT) {   // jammed: it loses sight of the partner
                        d->jammed = 2;
                        d->jammedBy = slotOf(a);
                        snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), " BLACKOUT: it can't see %s.",
                            battle.side[aSide].mech[partner].name);
                    }
                }
            }
            // Sapper fields: a hazard that bites at the start of its next turn, a snare that slows it now
            int hazard = (int)fwEffect(a->mech, CFX_HAZARD), slow = (int)fwEffect(a->mech, CFX_SLOW), pattern = targetingOf(a, w);
            if (hazard > d->hazard && (pattern == TARGET_AREA || pattern == TARGET_CONE) && ds->integrity > 0) {
                d->hazard = hazard;
                snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), " HAZARD: -%d INT next turn.", hazard);
            }
            if (slow > d->slowed && ds->integrity > 0) {
                d->slowed = slow;
                snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), " SLOWED: -%d MOB.", slow);
            }
            // Force Swap (a forceSwap weapon, or Displacement Routine once a turn) on the primary target, else Lockdown
            if (ds->integrity > 0) {
                int pull = k == 0 && (w->forceSwap || (!a->displaceUsed && fwEffect(a->mech, CFX_FORCE_SWAP) > 0)), res[MAX_TEAM];
                if (pull && battleAnchored(d))
                    snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), " HOLDS: locked in, it can't be forced out.");
                else if (pull && sideReserves(dSide, res, MAX_TEAM) > 0) {
                    battle.forcedSide = dSide;   // swapped once the hit's animation ends
                    battle.forcedSlot = slotOf(d);
                    if (!w->forceSwap) a->displaceUsed = 1;
                    snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), " FORCED OUT!");
                }
                else if (fwEffect(a->mech, CFX_SWITCH_LOCK) > 0 && !battleAnchored(d)) {
                    d->switchLock = 1;
                    snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), " LOCKED DOWN.");
                }
            }
            // Defense Link: the Aegis takes its share of the hit through its own Armor
            float share = 0;
            Combatant* g = w->baseDamage > 0 ? guardOf(d, &share) : NULL;
            if (g && g->mech->stats.integrity > 0) {
                AttackContext gc = liveContext(a, g);
                gc.splashMod = mod[k];
                gc.guardMod = share;
                gc.critMod = crit ? CRIT_MULT : 1.0f;
                gc.breachReady = 0;
                gc.energyTax = 0;
                attackPreview(a->mech, w, g->mech, &gc, &gp);
                g->mech->stats.armor -= gp.armorDamage;
                g->mech->stats.integrity = clampi(g->mech->stats.integrity - gp.integrityDamage, 0, g->mech->stats.maxIntegrity);
                guardTotal = gp.armorDamage + gp.integrityDamage;
                guardPos = fieldPosOf(dSide, slotOf(g));
                snprintf(guardLine, sizeof(guardLine), "   DEFENSE LINK: %s absorbs %d%% - %d DMG (%d ARM / %d INT).", g->mech->name,
                    (int)roundf(share * 100), guardTotal, gp.armorDamage, gp.integrityDamage);
            }
        }
        if (k == 0) {
            char victim[80];
            if (coveredName) snprintf(victim, sizeof(victim), "%s (covering %s)", d->mech->name, coveredName);
            else snprintf(victim, sizeof(victim), "%s", d->mech->name);
            if (!hit) snprintf(line, sizeof(line), "%s%s fired %s at %s... MISSED! (%d%% to hit)", prefix ? prefix : "", who,
                w->name, victim, (int)roundf(p[k].hitChance * 100));
            else if (total > 0) snprintf(line, sizeof(line), "%s%s fired %s at %s! %s%d DMG (%d ARM / %d INT).%s", prefix ? prefix : "",
                who, w->name, victim, crit ? "CRITICAL! " : "", total, p[k].armorDamage, p[k].integrityDamage, extra);
            else snprintf(line, sizeof(line), "%s%s activated %s on %s.%s", prefix ? prefix : "", who, w->name, victim, extra);
            snprintf(first, sizeof(first), "%s", line);
            if (!hit && fwEffect(a->mech, CFX_RECURSIVE_TARGETING) > 0) a->missStacks++;
        }
        else if (!hit) snprintf(line, sizeof(line), "   splash misses %s (%d%% to hit)", d->mech->name, (int)roundf(p[k].hitChance * 100));
        else snprintf(line, sizeof(line), "   splash hits %s: %s%d DMG (%d ARM / %d INT).%s", d->mech->name, crit ? "CRITICAL! " : "",
            total, p[k].armorDamage, p[k].integrityDamage, extra);

        // History entry: the roll first, then the reasons
        LogEntry* le = logPush(line, isPlayer, w->munition);
        say(&le->why, !hit, "ROLL %d vs %d%% to hit -> %s", (int)(roll * 100), (int)roundf(p[k].hitChance * 100), hit ? "HIT" : "MISS");
        if (critRoll >= 0)
            say(&le->why, 0, "CRIT ROLL %d vs %d%% -> %s", (int)(critRoll * 100), (int)roundf(p[k].critChance * 100),
                crit ? TextFormat("CRITICAL, x%.1f damage", CRIT_MULT) : "normal hit");
        if (scrambleRoll >= 0)
            say(&le->why, 0, "SCRAMBLE ROLL %d vs %d%% resist -> %s", (int)(scrambleRoll * 100), (int)roundf(p[k].resist * 100),
                scrambleRoll < p[k].resist ? "resisted" : "landed");
        if (k == 0 && targetWhy[0]) say(&le->why, 0, "%s", targetWhy);
        if (k == 0 && formation[0]) say(&le->why, coveredName != NULL, "%s", formation);
        for (int i = 0; i < why[k].n; i++) say(&le->why, why[k].warn[i], "%s", why[k].line[i]);

        BattleEvent* ev = pushEvent(w->fx, isPlayer, aPos, pos[k], total, hit, munitionColor(w->munition));
        if (ev) {
            ev->munition = w->munition;
            ev->armorDamage = hit ? p[k].armorDamage : 0;
            ev->integrityDamage = hit ? p[k].integrityDamage : 0;
            ev->lethal = hit && lethalBefore;
            ev->crit = crit;
        }
        if (guardLine[0]) {
            LogEntry* ge = logPush(guardLine, isPlayer, w->munition);
            sayWrapped(&ge->why, 0, linkDefs[LINK_DEFENSE].rule);
            BattleEvent* gev = pushEvent(FX_GUARD, dSide == SIDE_PLAYER, pos[k], guardPos, guardTotal, 1, linkDefs[LINK_DEFENSE].color);
            if (gev) {
                gev->munition = w->munition;
                gev->armorDamage = gp.armorDamage;
                gev->integrityDamage = gp.integrityDamage;
                gev->lethal = battleField(dSide, guardPos)->mech->stats.integrity <= 0;
            }
        }
    }
    a->actionsThisTurn++;
    a->strikeReady = 0;
    a->fresh = 0;   // out of stealth
    a->deployed = 0;   // Prowler Ambush spent
    a->evasiveBonus = (int)fwEffect(a->mech, CFX_EVASIVE_MANEUVER);
    int gain = n > 1 || !singleTarget(a, w) || w->energyCost >= 2 ? THREAT_HEAVY_ATTACK : THREAT_ATTACK;
    if (gain > a->attackThreat) { battleAddThreat(a, gain - a->attackThreat); a->attackThreat = gain; }   // once per turn
    targetWhy[0] = 0;
    linkUpkeep();   // a mech knocked below 25% loses its link
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
    int provoker = battleProvoker(SIDE_ENEMY);
    if (provoker >= 0) { battle.playerTarget = provoker; return; }
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

static void commandRoundStart(int side);

static void beginEnemyTurn(void) {
    const char* note = sideTurnStart(SIDE_ENEMY);
    battle.phase = BP_ENEMY_TURN;
    battle.enemyActing = 0;
    if (note[0]) logLine("Enemy:%s", note);
    enemyFillField();
    commandRoundStart(SIDE_ENEMY);
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
// Round start: every field mech loses THREAT_DECAY, then gains its passive
// threat for standing on the field (an Ironclad more)
static void threatRoundTick(void) {
    for (int side = 0; side < 2; side++)
        for (int p = 0; p < MAX_FIELD; p++) {
            Combatant* c = battleField(side, p);
            if (!c) continue;
            battleAddThreat(c, -THREAT_DECAY);
            battleAddThreat(c, mechRole(c->mech) == ROLE_IRONCLAD ? THREAT_PASSIVE_IRONCLAD : THREAT_PASSIVE);
        }
}

static void beginPlayerTurn(void) {
    battle.round++;
    threatRoundTick();
    const char* note = sideTurnStart(SIDE_PLAYER);
    battle.phase = BP_PLAYER_TURN;
    battle.phaseActed = 0;
    logLine("ROUND %d. Reactors recharged, heat vented.%s", battle.round, note);
    commandRoundStart(SIDE_PLAYER);
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
int battleSwitchFree(const Combatant* c) { return !c->freeSwitchUsed && fwEffect(c->mech, CFX_SWITCH_FREE) > 0; }
int battleSwitchCost(const Combatant* c) { return battleSwitchFree(c) ? 0 : SWITCH_ENERGY_COST; }
int battleAnchored(const Combatant* c) { return c->switchLocked || c->switchLock > 0; }

// Why this field mech can't be switched out this turn, NULL if it can.
// Emergency Redeploy (once a battle) gets it out at 0 Energy, even locked in.
static const char* switchBlock(const Combatant* c) {
    if (c->switchLocked && !battleSwitchFree(c)) return "LOCKED IN";
    if (c->skipTurn) return "SYSTEMS SCRAMBLED";
    if (c->mech->stats.energy < battleSwitchCost(c)) return "INSUFFICIENT ENERGY";
    return NULL;
}

// A mech taking the field: stealth and ambush are ready, the emplacement is
// fresh, and a Sensor Veil jams the other side's field
static void onEnterField(int side, int slot) {
    Combatant* c = &battle.side[side].slot[slot];
    c->fresh = 1;
    c->everMoved = 0;
    c->hitBy = 0;
    c->interceptPos = -1;
    float amp = fwEffect(c->mech, CFX_LINK_BOOST);   // Link Amplifier: through the end of its first turn
    if (amp > 0) {
        if (battle.linkBoostTurns[side] <= 0 || amp > battle.linkBoost[side]) battle.linkBoost[side] = amp;
        battle.linkBoostTurns[side] = 2;   // ticks at the side's phase starts: this one (if any) and its first turn
        LogEntry* e = logNote(TextFormat("%sLINK AMPLIFIER: %s takes the field - %s links are %d%% stronger until the end of its "
            "first turn.", side == SIDE_PLAYER ? "" : "Enemy ", c->mech->name, side == SIDE_PLAYER ? "your" : "their",
            (int)roundf(battle.linkBoost[side] * 100)), side == SIDE_PLAYER);
        say(&e->why, 0, "Every link on that side: more Accuracy and crit chance vs the mark, a bigger Defense Link share.");
    }
    int jam = (int)fwEffect(c->mech, CFX_ENTRY_JAM);
    if (jam <= 0) return;
    int n = 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* e = battleField(1 - side, p);
        if (e && e->nextAccPenalty < jam) { e->nextAccPenalty = jam; n++; }
    }
    if (n) logNote(TextFormat("%s%s jams sensors on entry: %d enemy mech%s at -%d Accuracy next turn.", side == SIDE_PLAYER ? "" : "Enemy ",
        c->mech->name, n, n > 1 ? "s" : "", jam), side == SIDE_PLAYER);
}

// Puts reserve `slot` on the side's field position. A paid switch (paid 1)
// drains the outgoing mech's Energy and clears its turn state, and the incoming
// mech is locked in for its next turn; an Emergency Deployment (paid 2) clears
// the outgoing mech the same way but locks nobody in; a free deploy fills an
// empty position with no lockout. Either way the new mech doesn't act the round
// it arrives.
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
        out->threat = out->provoking = 0;   // out of sight
        out->flanking = out->moved = out->overwatch = 0;
        out->interceptPos = -1;
        int relay = (int)fwEffect(out->mech, CFX_WITHDRAW_BUFF);   // Catcher Relay: hands its lock-on over
        if (relay > 0 && out->mech->stats.integrity > 0) {
            in->relayAccuracy = relay;
            in->relayTurns = 2;   // ticks at its turn starts: active through its first turn
            logNote(TextFormat("%sCATCHER RELAY: %s hands its lock-on to %s - +%d Accuracy through its first turn.",
                side == SIDE_PLAYER ? "" : "Enemy ", out->mech->name, in->mech->name, relay), side == SIDE_PLAYER);
        }
    }
    sd->field[pos] = slot;
    in->deployed = 1;       // a mid-battle arrival (Prowler Ambush)
    onEnterField(side, slot);
    in->flanking = in->moved = 0;
    in->lane = defaultLane(side, in);
    in->switchLock = paid == 1 ? 1 : 0;
    in->switchLocked = 0;
    in->actionsThisTurn = 0;
    in->done = 1;
    in->fielded = 1;
    pushEvent(FX_SWITCH, side == SIDE_PLAYER, pos, pos, 0, 1, mechModel(in->mech)->accent);
    battle.animTimer = fxDuration(FX_SWITCH);
    linkUpkeep();           // the outgoing mech's links break...
    autoLink(side, slot);   // ...and the incoming one links up with whoever fits
    overwatchTrigger(side, slot);   // stepping onto the field under the other side's Overwatch
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

// A field mech's switch-out for reserve `slot`. Emergency Redeploy, if ready,
// is spent: no Energy, and the newcomer isn't locked in. 1 if it was free.
static int switchOut(int side, int pos, int slot) {
    Combatant* c = battleField(side, pos);
    int free = battleSwitchFree(c);
    if (free) c->freeSwitchUsed = 1;
    putOnField(side, pos, slot, free ? 2 : 1);
    return free;
}

void battleSwitchTo(int slot) {
    if (!battleCanSwitch(NULL) || !isReserve(SIDE_PLAYER, slot)) return;
    const char* from = battleActing()->mech->name;
    const char* in = battle.side[SIDE_PLAYER].mech[slot].name;
    if (battleSwitchFree(battleActing()))
        logLine("EMERGENCY REDEPLOY: %s withdraws (0 EN). %s takes its place next round - not locked in.", from, in);
    else logLine("%s withdraws (%d EN). %s takes its place next round - it can't switch out next turn.", from, SWITCH_ENERGY_COST, in);
    switchOut(SIDE_PLAYER, battle.actingSlot, slot);
    battle.phaseActed = 1;
    fixActor();
}

// ============ PROVOCATION ============
int battleProvokeCost(const Combatant* c) {
    int k = PROVOKE_ENERGY_COST - (int)fwEffect(c->mech, CFX_PROVOKE_DISCOUNT);
    return k > 0 ? k : 0;
}

int battleTargetingOf(int mount) {
    const Combatant* a = battleActing();
    const Weapon* w = a ? mechWeapon(a->mech, mount) : NULL;
    return w ? targetingOf(a, w) : TARGET_SINGLE;
}

static const char* provokeBlock(const Combatant* c) {
    if (fwEffect(c->mech, CFX_PROVOCATION) <= 0) return "NEEDS PROVOCATION PROTOCOL";
    if (c->provoking) return "ALREADY PROVOKING";
    if (c->mech->stats.energy < battleProvokeCost(c)) return "INSUFFICIENT ENERGY";
    return NULL;
}

// A mech taunts the other side: +50 Threat, and their single-target attacks
// must aim at it until its next turn
static void doProvoke(Combatant* c, int side, int pos) {
    int cost = battleProvokeCost(c);
    c->mech->stats.energy -= cost;
    c->provoking = 1;
    c->actionsThisTurn++;
    battleAddThreat(c, THREAT_PROVOKE);
    pushEvent(FX_PROVOKE, side == SIDE_PLAYER, pos, pos, 0, 1, mechModel(c->mech)->accent);
    battle.animTimer = 0.9f;
    LogEntry* e = logPush(TextFormat("%s%s PROVOKES! %s single-target attacks must target it until its next turn.",
        side == SIDE_PLAYER ? "" : "Enemy ", c->mech->name, side == SIDE_PLAYER ? "Enemy" : "Your"), side == SIDE_PLAYER, -1);
    say(&e->why, 0, "PROVOCATION PROTOCOL: %d EN, Threat +%d (now %d).", cost, THREAT_PROVOKE, c->threat);
    say(&e->why, 0, "Area and cone weapons are not single-target: they still hit everyone.");
    snprintf(battle.log, sizeof(battle.log), "%s", e->text);
    snprintf(lastLogged, sizeof(lastLogged), "%s", e->text);
}

int battleCanProvoke(const char** reason) {
    const char* r = NULL;
    Combatant* a = battleActing();
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) r = "STANDBY";
    else if (!a) r = "NO MECH TO COMMAND";
    else r = provokeBlock(a);
    if (reason) *reason = r;
    return r == NULL;
}

void battleProvoke(void) {
    if (!battleCanProvoke(NULL)) return;
    doProvoke(battleActing(), SIDE_PLAYER, battle.actingSlot);
    battle.phaseActed = 1;
}

// Enemy Ironclads provoke at the start of their turn to shield a hurt or
// louder squadmate, as long as they are healthy enough to take the fire
static int enemyConsiderProvoke(int pos) {
    Combatant* c = battleField(SIDE_ENEMY, pos);
    if (!c || c->actionsThisTurn > 0 || provokeBlock(c) || integrityBelow(c, 0.40f)) return 0;
    int reason = 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* o = battleField(SIDE_ENEMY, p);
        if (o && o != c && (integrityBelow(o, 0.5f) || o->threat > c->threat)) reason = 1;
    }
    if (!reason) return 0;
    doProvoke(c, SIDE_ENEMY, pos);
    return 1;
}

// ============ INTERCEPT ============
// The ally position INTERCEPT guards: the most hurt ally (by Integrity share)
// that it isn't guarding already, -1 if none
static int interceptCandidate(int side, const Combatant* c) {
    int self = fieldPosOf(side, slotOf(c)), best = -1;
    float bestFrac = 2;
    for (int p = 0; p < MAX_FIELD; p++) {
        const Combatant* o = battleField(side, p);
        if (!o || p == self || p == c->interceptPos) continue;
        float frac = (float)o->mech->stats.integrity / (o->mech->stats.maxIntegrity > 0 ? o->mech->stats.maxIntegrity : 1);
        if (frac < bestFrac) { bestFrac = frac; best = p; }
    }
    return best;
}

static const char* interceptBlock(int side, const Combatant* c) {
    if (fwEffect(c->mech, CFX_INTERCEPT) <= 0) return "NEEDS INTERCEPT PROTOCOL";
    if (c->mech->stats.energy < INTERCEPT_ENERGY_COST) return "INSUFFICIENT ENERGY";
    if (interceptCandidate(side, c) < 0) return c->interceptPos >= 0 ? "NO OTHER ALLY TO GUARD" : "NO ALLY TO GUARD";
    return NULL;
}

// The mech declares an ally's position: until its next turn, single-target and
// line shots aimed there hit it instead. It's an action, like PROVOKE.
static void doIntercept(Combatant* c, int side, int to) {
    int pos = fieldPosOf(side, slotOf(c));
    c->mech->stats.energy -= INTERCEPT_ENERGY_COST;
    c->interceptPos = to;
    c->actionsThisTurn++;
    battleAddThreat(c, THREAT_BUFF);
    pushEvent(FX_LINK, side == SIDE_PLAYER, pos, to, 0, 1, linkDefs[LINK_DEFENSE].color);
    battle.animTimer = 0.6f;
    LogEntry* e = logPush(TextFormat("%s%s INTERCEPTS for %s's position until its next turn.", side == SIDE_PLAYER ? "" : "Enemy ",
        c->mech->name, battleField(side, to)->mech->name), side == SIDE_PLAYER, -1);
    say(&e->why, 0, "INTERCEPT PROTOCOL: %d EN, Threat +%d.", INTERCEPT_ENERGY_COST, THREAT_BUFF);
    sayWrapped(&e->why, 0, "Single-target and line shots aimed at that position hit the interceptor instead - whoever stands there, "
        "so a reserve swapped in behind it is guarded too. Spotter reach and Infiltration don't get past it; area and cone weapons do.");
    snprintf(battle.log, sizeof(battle.log), "%s", e->text);
    snprintf(lastLogged, sizeof(lastLogged), "%s", e->text);
}

int battleCanIntercept(const char** reason) {
    const char* r = NULL;
    Combatant* a = battleActing();
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) r = "STANDBY";
    else if (!a) r = "NO MECH TO COMMAND";
    else r = interceptBlock(SIDE_PLAYER, a);
    if (reason) *reason = r;
    return r == NULL;
}

int battleInterceptCandidate(void) {
    const Combatant* a = battleActing();
    return a && fwEffect(a->mech, CFX_INTERCEPT) > 0 ? interceptCandidate(SIDE_PLAYER, a) : -1;
}

void battleIntercept(void) {
    if (!battleCanIntercept(NULL)) return;
    doIntercept(battleActing(), SIDE_PLAYER, battleInterceptCandidate());
    battle.phaseActed = 1;
}

// AI: a healthy interceptor guards the most hurt ally that is hurt or louder
// than itself and not already covered in the Rear
static int aiConsiderIntercept(int side, int pos) {
    Combatant* c = battleField(side, pos);
    if (!c || c->actionsThisTurn > 0 || c->provoking || interceptBlock(side, c) || integrityBelow(c, 0.40f)) return 0;
    int best = -1;
    float bestFrac = 2;
    for (int p = 0; p < MAX_FIELD; p++) {
        const Combatant* o = battleField(side, p);
        if (!o || o == c || battleInterceptor(side, p) >= 0 || !(integrityBelow(o, 0.5f) || o->threat > c->threat)) continue;
        float frac = (float)o->mech->stats.integrity / (o->mech->stats.maxIntegrity > 0 ? o->mech->stats.maxIntegrity : 1);
        if (frac < bestFrac) { bestFrac = frac; best = p; }
    }
    if (best < 0) return 0;
    doIntercept(c, side, best);
    return 1;
}

// ============ FORMATION MOVES ============
static const char* moveBlock(const Combatant* c, int to) {
    if (c->moved) return c->flanking ? "FLANKING UNTIL NEXT TURN" : "ALREADY MOVED THIS TURN";
    if (to == MOVE_FLANK && !battleCanFlank(c)) return "ONLY RECON / EW CAN FLANK";
    if (to != MOVE_FLANK && c->lane == to) return to == LANE_FRONT ? "ALREADY IN FRONT" : "ALREADY IN THE REAR";
    if (c->mech->stats.energy < battleMoveCost(c)) return "INSUFFICIENT ENERGY";
    return NULL;
}

// Changes lane or goes out on the flank. Not an action: the mech can still fire.
static void doMove(Combatant* c, int side, int to) {
    int cost = battleMoveCost(c);
    c->mech->stats.energy -= cost;
    c->moved = 1;
    c->everMoved = 1;     // Emplacement is given up
    c->strikeReady = 1;   // Strike and Fade: its next attack this turn
    float trap = 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        const Combatant* t = battleField(1 - side, p);
        if (t && fwEffect(t->mech, CFX_TRAP) > trap) trap = fwEffect(t->mech, CFX_TRAP);
    }
    if (trap > 0) {   // Caltrops: moving under a Sapper's field hurts
        MechStats* ms = &c->mech->stats;
        ms->integrity = clampi(ms->integrity - (int)trap, 0, ms->maxIntegrity);
        logNote(TextFormat("CALTROPS: %s%s takes %d moving.", side == SIDE_PLAYER ? "" : "Enemy ", c->mech->name, (int)trap), side == SIDE_PLAYER);
        battle.outcomePending = 1;
    }
    const char* who = TextFormat("%s%s", side == SIDE_PLAYER ? "" : "Enemy ", c->mech->name);
    const char* paid = cost ? TextFormat(" (%d EN)", cost) : "";
    LogEntry* e;
    if (to == MOVE_FLANK) {
        c->flanking = 1;
        e = logPush(TextFormat("%s moves out to the FLANK%s: its attacks ignore %d%% of Armor until its next turn.", who, paid,
            (int)roundf(FLANK_ARMOR_IGNORE * 100)), side == SIDE_PLAYER, -1);
        say(&e->why, 0, "FLANK: every attack ignores %d%% of the target's Armor - that share of the hit bypasses it into Integrity.",
            (int)roundf(FLANK_ARMOR_IGNORE * 100));
        say(&e->why, 1, "EXPOSED: -%d Mobility against incoming attacks, and out of formation: it neither covers nor is covered.",
            FLANK_MOBILITY_PENALTY);
        say(&e->why, 0, "It returns to its lane at the start of its next turn.");
    }
    else {
        c->lane = to;
        e = logPush(TextFormat("%s moves to the %s%s.", who, to == LANE_FRONT ? "FRONT" : "REAR", paid), side == SIDE_PLAYER, -1);
        say(&e->why, 0, to == LANE_FRONT ? "FRONT: single-target and line attacks aimed at Rear allies hit it instead."
            : "REAR: a Front ally takes single-target and line attacks aimed at it. Area and cone weapons still reach it.");
    }
    say(&e->why, 0, "Moving costs %d EN (Recon and EW move free), once a turn. It doesn't use up the mech's action.", MOVE_ENERGY_COST);
    snprintf(battle.log, sizeof(battle.log), "%s", e->text);
    snprintf(lastLogged, sizeof(lastLogged), "%s", e->text);
    battle.animTimer = 0.35f;   // let the sprite slide over before anything else happens
    overwatchTrigger(side, slotOf(c));   // caught moving
}

int battleCanMove(int to, const char** reason) {
    const char* r = NULL;
    Combatant* a = battleActing();
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) r = "STANDBY";
    else if (!a) r = "NO MECH TO COMMAND";
    else r = moveBlock(a, to);
    if (reason) *reason = r;
    return r == NULL;
}

void battleMove(int to) {
    if (!battleCanMove(to, NULL)) return;
    doMove(battleActing(), SIDE_PLAYER, to);
    battle.phaseActed = 1;
}

// AI formation, before a mech acts: a badly hurt guard falls back behind a
// healthier one, and a healthy Recon / EW flanks before a damaging attack on an
// armored target, as long as that doesn't strip cover from a Rear ally.
static int aiConsiderMove(int side, int pos) {
    Combatant* c = battleField(side, pos);
    if (!c || c->moved || c->actionsThisTurn > 0) return 0;
    int other = side == SIDE_PLAYER ? SIDE_ENEMY : SIDE_PLAYER, otherGuard = 0, coveredAlly = 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* o = battleField(side, p);
        if (!o || o == c) continue;
        if (guards(o) && !integrityBelow(o, 0.5f)) otherGuard = 1;
        if (battleInterceptor(side, p) >= 0) coveredAlly = 1;
    }
    if (guards(c) && otherGuard && integrityBelow(c, AI_FALL_BACK_BELOW) && !moveBlock(c, LANE_REAR)) {
        doMove(c, side, LANE_REAR);
        return 1;
    }
    if (!battleCanFlank(c) || moveBlock(c, MOVE_FLANK) || integrityBelow(c, 0.5f)) return 0;
    if (guards(c) ? coveredAlly && !otherGuard : battleInterceptor(side, pos) >= 0 && integrityBelow(c, 0.75f)) return 0;
    int target;
    int mount = aiChooseAction(c, other, 0, &target, NULL, NULL, 0);
    const Weapon* w = mount >= 0 ? mechWeapon(c->mech, mount) : NULL;
    if (!w || w->baseDamage <= 0 || battleField(other, aimedAt(c, w, other, target))->mech->stats.armor <= 0) return 0;
    doMove(c, side, MOVE_FLANK);
    return 1;
}

// ============ COMMAND POINTS ============
const CommandDef commandDefs[NUM_COMMANDS] = {
    //                         tag       cost (enemy, player)
    { "FOCUS FIRE",           "FOCUS",  { 2, 2 }, { 255, 120, 90, 255 },
      "Your attacks on the target get +10 Accuracy (even past 100) until your next phase." },
    { "EMERGENCY DEPLOYMENT", "DEPLOY", { 2, 2 }, { 120, 200, 255, 255 },
      "The commanded mech swaps with a reserve for 0 Energy, even when locked in. The new mech isn't locked in, "
      "but like any arrival it acts next round." },
    { "COORDINATED STRIKE",   "STRIKE", { 3, 3 }, { 255, 200, 90, 255 },
      "The commanded mech and the best-placed ally each fire a free shot (no Energy or Heat) at the target, in sequence. "
      "If the first scraps it, the second picks a new one." },
    { "DEFENSIVE LINE",       "DEFEND", { 3, 3 }, { 130, 230, 255, 255 },
      "Every mech on your side takes 25% less damage until your next phase." },
    { "OVERWATCH",            "WATCH",  { 4, 4 }, { 180, 255, 140, 255 },
      "Ends your phase now. Every field mech holds one free shot for the first enemy that fires, moves or takes the "
      "field - fired before the enemy's own shot. Unused, it lapses at your next phase." },
};

static int catchersOnField(int side) {
    int n = 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        const Combatant* c = battleField(side, p);
        if (c && mechRole(c->mech) == ROLE_CATCHER) n++;
    }
    return n;
}

int battleCommandIncome(int side, int* catchers) {
    int n = catchersOnField(side);
    if (catchers) *catchers = n;
    return n > 0 ? CP_BASE_INCOME + n * CP_CATCHER_INCOME : 0;
}

static void gainCommand(int side, int amount, const char* why) {
#ifdef NO_COMMANDS_TEST   // test builds only: nobody earns Command Points, for comparison
    return;
#endif
    int before = battle.commandPoints[side];
    battle.commandPoints[side] = clampi(before + amount, 0, CP_MAX);
    LogEntry* e = logNote(TextFormat("%sCOMMAND +%d (%s): %d/%d CP.", side == SIDE_PLAYER ? "" : "Enemy ", amount, why,
        battle.commandPoints[side], CP_MAX), side == SIDE_PLAYER);
    if (before + amount > CP_MAX) say(&e->why, 1, "Capped at %d: %d went to waste.", CP_MAX, before + amount - CP_MAX);
    sayWrapped(&e->why, 0, TextFormat("Income at the start of each phase: +%d while a Catcher is on the field, +%d more for every Catcher "
        "there. +%d whenever an enemy field mech is scrapped.", CP_BASE_INCOME, CP_CATCHER_INCOME, CP_KILL_BONUS));
}

// A side's phase begins: last round's Focus Fire and Defensive Line end, and
// its Catchers bring in Command Points
static void commandRoundStart(int side) {
    battle.focusTarget[side] = -1;
    battle.defensiveLine[side] = 0;
    int catchers, gain = battleCommandIncome(side, &catchers);
    if (gain > 0) gainCommand(side, gain, TextFormat("%d Catcher%s on the field", catchers, catchers > 1 ? "s" : ""));
}

static int queueShot(int side, int slot, int aim, int kind, int mount) {
    if (battle.numQueued >= MAX_QUEUED_SHOTS) return 0;
    battle.queue[battle.numQueued++] = (QueuedShot){ side, slot, aim, kind, mount };
    return 1;
}

// A free shot (Coordinated Strike, Overwatch) from c at the other side's slot
// `aimSlot`: its best weapon there, no Energy or Heat. A strike whose target
// already fell picks a new one. 1 if it fired.
static int freeShot(Combatant* c, int side, int aimSlot, int kind) {
    int other = 1 - side, pos = fieldPosOf(side, slotOf(c)), at = fieldPosOf(other, aimSlot), target;
    if (pos < 0 || c->mech->stats.integrity <= 0 || (at < 0 && kind != CMD_COORDINATED_STRIKE)) return 0;
    int mount = aiChooseActionAt(c, other, at, 2, &target, NULL, NULL, 0);
    if (mount < 0) return 0;
    snprintf(targetWhy, sizeof(targetWhy), "%s: a free shot (no Energy or Heat), paid for with Command Points%s.",
        commandDefs[kind].name, at < 0 ? " - its target already fell, so it picked another" : "");
    MechStats* s = &c->mech->stats;
    int energy = s->energy, heat = s->heat;
    doAttack(c, side, pos, other, target, mount, kind == CMD_OVERWATCH ? "OVERWATCH! " : "COORDINATED STRIKE! ");
    s->energy = energy;
    s->heat = heat;
    return 1;
}

// The ally that joins c's Coordinated Strike on the other side's position aimPos:
// the field mech (not scrambled out of its turn) with the best free shot there, -1
static int coordinatedPartner(int side, const Combatant* c, int aimPos) {
    int best = -1;
    float bestScore = 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        const Combatant* o = battleField(side, p);
        float score;
        if (!o || o == c || o->skipTurn || o->mech->stats.integrity <= 0) continue;
        if (aiChooseActionAt(o, 1 - side, aimPos, 2, NULL, &score, NULL, 0) < 0) continue;
        if (best < 0 || score > bestScore) { best = p; bestScore = score; }
    }
    return best;
}

static int canWatch(const Combatant* c) {
    if (!c || c->skipTurn || c->mech->stats.integrity <= 0) return 0;
    for (int i = 0; i < MAX_WEAPONS; i++) {
        const Weapon* w = mechWeapon(c->mech, i);
        if (w && i != c->disabledWeapon && (w->ammo <= 0 || c->mech->weapons[i].ammo > 0)) return 1;
    }
    return 0;
}

// Why `side` can't issue cmd now with mech c (its commanded mech) aiming at the
// other side's position aimPos, NULL if it can
static const char* commandBlock(int side, int cmd, const Combatant* c, int aimPos) {
    int other = 1 - side, res[MAX_TEAM];
    if (battle.commandPoints[side] < commandDefs[cmd].cost[side]) return TextFormat("NEEDS %d CP", commandDefs[cmd].cost[side]);
    const Combatant* d = battleField(other, aimPos);
    switch (cmd) {
    case CMD_FOCUS_FIRE:
        if (!d) return "NO TARGET";
        return battle.focusTarget[side] == slotOf(d) ? "ALREADY FOCUSED ON IT" : NULL;
    case CMD_EMERGENCY_DEPLOY:
        if (!c) return "NO MECH TO COMMAND";
        return sideReserves(side, res, MAX_TEAM) == 0 ? "NO RESERVES" : NULL;
    case CMD_COORDINATED_STRIKE:
        if (!c || !d) return "NO TARGET";
        if (aiChooseActionAt(c, other, aimPos, 2, NULL, NULL, NULL, 0) < 0) return "NO SHOT AT THAT TARGET";
        return coordinatedPartner(side, c, aimPos) < 0 ? "NO ALLY CAN JOIN" : NULL;
    case CMD_DEFENSIVE_LINE:
        return battle.defensiveLine[side] ? "LINE ALREADY HELD" : NULL;
    default:
        for (int p = 0; p < MAX_FIELD; p++) if (canWatch(battleField(side, p))) return NULL;
        return "NO MECH CAN WATCH";
    }
}

// The command's log entry: what happened, then the cost and the rule
static void commandLog(int side, int cmd, const char* text) {
    LogEntry* e = logPush(text, side == SIDE_PLAYER, -1);
    say(&e->why, 0, "%s: %d CP (%d/%d left).", commandDefs[cmd].name, commandDefs[cmd].cost[side], battle.commandPoints[side], CP_MAX);
    sayWrapped(&e->why, 0, commandDefs[cmd].rule);
    snprintf(battle.log, sizeof(battle.log), "%s", e->text);
    snprintf(lastLogged, sizeof(lastLogged), "%s", e->text);
}

// Spends the Command Points and carries the command out. pos: the commanded
// mech's field position; aimPos: the other side's position it aims at;
// reserve: the slot an Emergency Deployment brings in.
static void doCommand(int side, int cmd, int pos, int aimPos, int reserve) {
    Combatant* c = battleField(side, pos);
    Combatant* d = battleField(1 - side, aimPos);
    const Side* sd = &battle.side[side];
    const char* who = side == SIDE_PLAYER ? "" : "ENEMY ";
    Color col = commandDefs[cmd].color;
    battle.commandPoints[side] -= commandDefs[cmd].cost[side];
    battle.animTimer = fxDuration(FX_COMMAND);
    switch (cmd) {
    case CMD_FOCUS_FIRE:
        battle.focusTarget[side] = slotOf(d);
        commandLog(side, cmd, TextFormat("%sFOCUS FIRE on %s: +%d Accuracy against it until %s next phase.", who, d->mech->name,
            FOCUS_FIRE_ACCURACY, side == SIDE_PLAYER ? "your" : "their"));
        pushEvent(FX_COMMAND, side == SIDE_PLAYER, pos, aimPos, cmd, 1, col);
        break;
    case CMD_EMERGENCY_DEPLOY:
        commandLog(side, cmd, TextFormat("%sEMERGENCY DEPLOYMENT: %s pulls out, %s takes its place (0 EN, not locked in).", who,
            c->mech->name, sd->mech[reserve].name));
        putOnField(side, pos, reserve, 2);
        break;
    case CMD_COORDINATED_STRIKE: {
        Combatant* o = battleField(side, coordinatedPartner(side, c, aimPos));
        commandLog(side, cmd, TextFormat("%sCOORDINATED STRIKE on %s: %s and %s fire in sequence.", who, d->mech->name,
            c->mech->name, o->mech->name));
        pushEvent(FX_COMMAND, side == SIDE_PLAYER, pos, aimPos, cmd, 1, col);
        queueShot(side, slotOf(c), slotOf(d), cmd, -1);
        queueShot(side, slotOf(o), slotOf(d), cmd, -1);
        break;
    }
    case CMD_DEFENSIVE_LINE:
        battle.defensiveLine[side] = 1;
        commandLog(side, cmd, TextFormat("%sDEFENSIVE LINE: %s take %d%% less damage until %s next phase.", who,
            side == SIDE_PLAYER ? "your mechs" : "their mechs", (int)roundf(DEFENSIVE_LINE_CUT * 100), side == SIDE_PLAYER ? "your" : "their"));
        for (int p = 0; p < MAX_FIELD; p++) if (battleField(side, p)) pushEvent(FX_COMMAND, side == SIDE_PLAYER, p, p, cmd, 0, col);
        break;
    default: {   // Overwatch: everyone who can holds a shot, and the phase is over
        int n = 0;
        for (int p = 0; p < MAX_FIELD; p++) {
            Combatant* o = battleField(side, p);
            if (!o) continue;
            if (canWatch(o)) { o->overwatch = 1; n++; pushEvent(FX_COMMAND, side == SIDE_PLAYER, p, p, cmd, 0, col); }
            o->done = 1;
        }
        commandLog(side, cmd, TextFormat("%sOVERWATCH: %d mech%s hold%s fire for the first %s that fires, moves or takes the field. Phase over.",
            who, n, n > 1 ? "s" : "", n > 1 ? "" : "s", side == SIDE_PLAYER ? "enemy" : "of your mechs"));
        break;
    }
    }
}

// An opposing mech steps into the open (opens fire, moves, takes the field):
// every mech of the other side holding Overwatch that can reach it queues its
// free shot. 1 if any did.
static int overwatchTrigger(int side, int slot) {
    int other = 1 - side, at = fieldPosOf(side, slot), n = 0;
    if (at < 0) return 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* o = battleField(other, p);
        if (!o || o->overwatch != 1 || o->mech->stats.integrity <= 0) continue;
        if (aiChooseActionAt(o, side, at, 2, NULL, NULL, NULL, 0) < 0) continue;   // can't reach it: keeps waiting
        if (queueShot(other, slotOf(o), slot, CMD_OVERWATCH, -1)) { o->overwatch = 2; n++; }
    }
    if (n) logNote(TextFormat("%s%s steps into the open - OVERWATCH: %d shot%s incoming!", side == SIDE_PLAYER ? "" : "Enemy ",
        battle.side[side].mech[slot].name, n, n > 1 ? "s" : ""), other == SIDE_PLAYER);
    return n;
}

// Resolves the next queued shot (one per update, so each gets its animation)
static void runQueued(void) {
    QueuedShot q = battle.queue[0];
    memmove(battle.queue, battle.queue + 1, sizeof(QueuedShot) * (--battle.numQueued));
    Combatant* c = &battle.side[q.side].slot[q.slot];
    if (q.kind == SHOT_OWN) {   // the player's shot, after the enemy Overwatch it walked into
        int pos = fieldPosOf(SIDE_PLAYER, q.slot), at = fieldPosOf(SIDE_ENEMY, q.aim);
        if (battle.phase != BP_PLAYER_TURN || pos < 0 || at < 0 || c->done) {
            logLine("%s's shot is called off.", c->mech->name);
            return;
        }
        battle.actingSlot = pos;
        battle.playerTarget = at;
        const char* r = fireBlock(q.mount);
        if (r) { logLine("%s holds fire: %s.", c->mech->name, r); return; }
        int fired = corruptedMount(c, q.mount);
        doAttack(c, SIDE_PLAYER, pos, SIDE_ENEMY, at, fired, fired != q.mount ? "TARGETING CORRUPTED! " : NULL);
        return;
    }
    if (q.kind == CMD_OVERWATCH) c->overwatch = 0;   // spent, fired or not
    if (!freeShot(c, q.side, q.aim, q.kind) && fieldPosOf(q.side, q.slot) >= 0)
        logLine("%s%s can't take its %s shot.", q.side == SIDE_PLAYER ? "" : "Enemy ", c->mech->name,
            q.kind == CMD_OVERWATCH ? "Overwatch" : "Coordinated Strike");
}

int battleCanCommand(int cmd, const char** reason) {
    const char* r = NULL;
    if (cmd < 0 || cmd >= NUM_COMMANDS) r = "-";
    else if (battle.phase != BP_PLAYER_TURN || battleBusy()) r = "STANDBY";
    else if (!battleActing()) r = "NO MECH TO COMMAND";
    else r = commandBlock(SIDE_PLAYER, cmd, battleActing(), battle.playerTarget);
    if (reason) *reason = r;
    return r == NULL;
}

int battleCoordinatedPartner(void) {
    const Combatant* a = battleActing();
    return a ? coordinatedPartner(SIDE_PLAYER, a, battle.playerTarget) : -1;
}

static void playerCommand(int cmd, int reserve) {
    doCommand(SIDE_PLAYER, cmd, battle.actingSlot, battle.playerTarget, reserve);
    battle.phaseActed = 1;
    if (cmd == CMD_OVERWATCH) beginEnemyTurn();
    else fixActor();
}

void battleCommand(int cmd) {
    if (!battleCanCommand(cmd, NULL)) return;
    playerCommand(cmd, cmd == CMD_EMERGENCY_DEPLOY ? healthiestReserve(SIDE_PLAYER, -1) : -1);
}

void battleEmergencyDeploy(int slot) {
    if (!battleCanCommand(CMD_EMERGENCY_DEPLOY, NULL) || !isReserve(SIDE_PLAYER, slot)) return;
    playerCommand(CMD_EMERGENCY_DEPLOY, slot);
}

static int actorsLeft(int side) {
    int n = 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        const Combatant* c = battleField(side, p);
        n += c && !c->done;
    }
    return n;
}

// Expected Integrity damage of c's free shot at the other side's position pos
// (0 if a guard would take it instead)
static float freeShotDamage(const Combatant* c, int side, int pos) {
    int at, mount = c ? aiChooseActionAt(c, side, pos, 2, &at, NULL, NULL, 0) : -1;
    if (mount < 0) return 0;
    const Weapon* w = mechWeapon(c->mech, mount);
    if (aimedAt(c, w, side, pos) != pos) return 0;
    const Combatant* d = battleField(side, pos);
    AttackContext ctx = liveContext(c, d);
    AttackPreview p;
    attackPreview(c->mech, w, d->mech, &ctx, &p);
    return p.hitChance * p.integrityDamage * (1.0f + p.critChance * (CRIT_MULT - 1.0f));
}

// What the AI expects from an Overwatch: each watcher's free shot at an average
// mech of the other side (whoever acts first), minus the rest of the last
// mech's own turn
static float overwatchValue(int side, const Combatant* last) {
    float v = 0, own = 0;
    for (int p = 0; p < MAX_FIELD; p++) {
        const Combatant* o = battleField(side, p);
        if (!canWatch(o)) continue;
        float sum = 0;
        int n = 0;
        for (int t = 0; t < MAX_FIELD; t++) {
            float sc;
            if (!battleField(1 - side, t)) continue;
            n++;
            if (aiChooseActionAt(o, 1 - side, t, 2, NULL, &sc, NULL, 0) >= 0) sum += sc;
        }
        if (n) v += sum / n;
    }
    float best;
    if (aiChooseAction(last, 1 - side, 2, NULL, &best, NULL, 0) >= 0) own = best * (last->actionsThisTurn == 0 ? 1.5f : 0.5f);
    return v - own;
}

// ...and from a Defensive Line: a quarter of what the other side's field mechs
// would do to it next phase (about two shots each)
static float defensiveLineValue(int side) {
    float v = 0;
    for (int t = 0; t < MAX_FIELD; t++) {
        const Combatant* e = battleField(1 - side, t);
        float sc;
        if (e && aiChooseAction(e, side, 2, NULL, &sc, NULL, 0) >= 0) v += 2 * sc;
    }
    return v * DEFENSIVE_LINE_CUT;
}

// The AI's Command Point spending, before its field mech `pos` acts. Its own
// priorities, in order: pull a dying mech out, finish a target with a
// Coordinated Strike, focus the squad's fire while there's CP to spare, and
// with only the last mech left to act and a hurt squad or CP to spare, a
// Defensive Line or an Overwatch, whichever it expects to be worth more. Near
// the cap it strikes rather than waste income. Returns the command issued + 1, 0 if none.
static int aiConsiderCommand(int side, int pos) {
    Combatant* c = battleField(side, pos);
    int other = 1 - side, cp = battle.commandPoints[side];
    if (!c || c->done || cp <= 0) return 0;
    int cost[NUM_COMMANDS];
    for (int k = 0; k < NUM_COMMANDS; k++) cost[k] = commandDefs[k].cost[side];
    // 1. a dying mech that can't switch out on its own (or CP to burn) is swapped for a healthy reserve
    if (integrityBelow(c, AI_SWITCH_BELOW) && c->actionsThisTurn == 0 && !commandBlock(side, CMD_EMERGENCY_DEPLOY, c, -1)) {
        int in = healthiestReserve(side, HEALTHY_RESERVE - 0.0001f);
        if (in >= 0 && (switchBlock(c) || cp >= CP_MAX - 1)) { doCommand(side, CMD_EMERGENCY_DEPLOY, pos, -1, in); return CMD_EMERGENCY_DEPLOY + 1; }
    }
    // 2. two free shots that should scrap a target: the most hurt one
    int kill = -1;
    for (int t = 0; cp >= cost[CMD_COORDINATED_STRIKE] && t < MAX_FIELD; t++) {
        const Combatant* d = battleField(other, t);
        if (!d || commandBlock(side, CMD_COORDINATED_STRIKE, c, t)) continue;
        float dmg = freeShotDamage(c, other, t) + freeShotDamage(battleField(side, coordinatedPartner(side, c, t)), other, t);
        if (dmg >= d->mech->stats.integrity && (kill < 0 || d->mech->stats.integrity < battleField(other, kill)->mech->stats.integrity)) kill = t;
    }
    if (kill >= 0) { doCommand(side, CMD_COORDINATED_STRIKE, pos, kill, -1); return CMD_COORDINATED_STRIKE + 1; }
    int target;
    float score;
    int mount = aiChooseAction(c, other, 0, &target, &score, NULL, 0);
    // 3. focus the squad on what this mech is about to shoot, keeping a strike in hand
    if (mount >= 0 && battle.focusTarget[side] < 0 && actorsLeft(side) >= 2
        && cp >= cost[CMD_FOCUS_FIRE] + cost[CMD_COORDINATED_STRIKE] && !commandBlock(side, CMD_FOCUS_FIRE, c, target)) {
        doCommand(side, CMD_FOCUS_FIRE, pos, target, -1);
        return CMD_FOCUS_FIRE + 1;
    }
    // 4. the last to act, with a hurt squad or CP to spare: whichever is worth more for the other
    // side's phase - a Defensive Line, or trading its own turn for the squad's Overwatch
    if (actorsLeft(side) == 1) {
        int hurt = 0, watchers = 0;
        for (int p = 0; p < MAX_FIELD; p++) {
            const Combatant* o = battleField(side, p);
            hurt |= o && integrityBelow(o, 0.5f);
            watchers += canWatch(o);
        }
        if (hurt || cp >= CP_MAX - 1) {
            float line = commandBlock(side, CMD_DEFENSIVE_LINE, c, -1) ? -1 : defensiveLineValue(side) * (hurt ? 1.5f : 1.0f);
            float watch = watchers < 2 || commandBlock(side, CMD_OVERWATCH, c, -1) ? -1 : overwatchValue(side, c);
            if (watch > 0 && watch > line) { doCommand(side, CMD_OVERWATCH, pos, -1, -1); return CMD_OVERWATCH + 1; }
            if (line > 0) { doCommand(side, CMD_DEFENSIVE_LINE, pos, -1, -1); return CMD_DEFENSIVE_LINE + 1; }
        }
    }
    // 6. income is about to overflow: strike with what's there
    if (cp >= CP_MAX - 1 && mount >= 0 && !commandBlock(side, CMD_COORDINATED_STRIKE, c, target)) {
        doCommand(side, CMD_COORDINATED_STRIKE, pos, target, -1);
        return CMD_COORDINATED_STRIKE + 1;
    }
    return 0;
}

int battleAICommandForPlayer(void) {
    if (battle.phase != BP_PLAYER_TURN || battleBusy() || !battleActing()) return 0;
    int cmd = aiConsiderCommand(SIDE_PLAYER, battle.actingSlot) - 1;
    if (cmd < 0) return 0;
    battle.phaseActed = 1;
    if (cmd == CMD_OVERWATCH) beginEnemyTurn();
    else fixActor();
    return 1;
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
static int nextEnemy(float atLeast) { return healthiestReserve(SIDE_ENEMY, atLeast); }

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
    logLine("Enemy %s pulls back%s! %s moves up.", c->mech->name, battleSwitchFree(c) ? " (EMERGENCY REDEPLOY)" : "",
        battle.side[SIDE_ENEMY].mech[best].name);
    switchOut(SIDE_ENEMY, pos, best);
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
    // Lanes: everyone but Recon first, so Recon knows whether the front is held
    for (int pass = 0; pass < 2; pass++)
        for (int p = 0; p < sd->numField; p++) {
            Combatant* c = &sd->slot[order[p]];
            int recon = mechClass(c->mech) == CLASS_RECON;
            if (recon == pass) c->lane = defaultLane(side, c);
            else if (recon) c->lane = LANE_REAR;   // until its turn to choose
        }
    autoLink(side, -1);   // links form from the roles on the field
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
    battle.commandPoints[SIDE_ENEMY] = battle.commandPoints[SIDE_PLAYER] = 0;
    battle.focusTarget[SIDE_ENEMY] = battle.focusTarget[SIDE_PLAYER] = -1;
    battle.defensiveLine[SIDE_ENEMY] = battle.defensiveLine[SIDE_PLAYER] = 0;
    battle.numQueued = 0;
    battle.linkBoost[SIDE_ENEMY] = battle.linkBoost[SIDE_PLAYER] = 0;
    battle.linkBoostTurns[SIDE_ENEMY] = battle.linkBoostTurns[SIDE_PLAYER] = 0;
    battle.forcedSlot = -1;
    logClear();
    setupPlayerSide();
    int order[MAX_TEAM];
    for (int i = 0; i < battle.side[SIDE_ENEMY].count; i++) order[i] = i;
    sideOpenField(SIDE_ENEMY, order, battle.side[SIDE_ENEMY].count);
    for (int side = 0; side < 2; side++)   // both fields are set: entry effects can reach across
        for (int p = 0; p < MAX_FIELD; p++)
            if (battle.side[side].field[p] >= 0) onEnterField(side, battle.side[side].field[p]);
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
    battle.numQueued = 0;
    battle.forcedSlot = -1;
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
    battle.numQueued = 0;
    battle.forcedSlot = -1;
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
    int mount = aiChooseAction(c, other, 1, &target, NULL, targetWhy, sizeof(targetWhy));
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
        gainCommand(SIDE_PLAYER, CP_KILL_BONUS, "enemy scrapped");
        if (battle.testRange) {
            battle.dummyKills++;
            battleResetDummy();
            logLine("TARGET DUMMY destroyed (%d). A new dummy is online.", battle.dummyKills);
        }
        else enemyDown(p);
    }
    for (int p = 0; p < MAX_FIELD; p++) {
        Combatant* c = battleField(SIDE_PLAYER, p);
        if (c && c->mech->stats.integrity <= 0) { changed = 1; gainCommand(SIDE_ENEMY, CP_KILL_BONUS, "your mech disabled"); playerDown(p); }
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

// A Force Swap, once its hit's animation is over: the target is pulled for a
// random standing reserve, if it's still on the field and not locked in
static void resolveForcedSwap(void) {
    int side = battle.forcedSide, slot = battle.forcedSlot, pos = fieldPosOf(side, slot), res[MAX_TEAM];
    battle.forcedSlot = -1;
    Combatant* c = &battle.side[side].slot[slot];
    int n = sideReserves(side, res, MAX_TEAM);
    if (pos < 0 || !battleSlotStanding(side, slot) || n == 0 || battleAnchored(c)) return;
    int in = res[rand() % n];
    LogEntry* e = logPush(TextFormat("%s%s is FORCED OUT! %s is shoved onto the field in its place.", side == SIDE_PLAYER ? "" : "Enemy ",
        c->mech->name, battle.side[side].mech[in].name), side != SIDE_PLAYER, -1);
    sayWrapped(&e->why, 0, "FORCE SWAP: a random standing reserve takes its position. Its links break and its turn state is cleared; "
        "the newcomer isn't locked in and acts from its side's next phase.");
    say(&e->why, 0, "A locked-in mech (just switched in, or hit by a Lockdown Routine) can't be forced out.");
    snprintf(battle.log, sizeof(battle.log), "%s", e->text);
    snprintf(lastLogged, sizeof(lastLogged), "%s", e->text);
    putOnField(side, pos, in, 2);
    fixTarget();
    fixActor();
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
int battleBusy(void) {
    return battle.dialogue != DLG_NONE || battle.animTimer > 0 || battle.numQueued > 0 || battle.forcedSlot >= 0;
}

// One enemy action per call: each field machine in turn switches out, provokes
// or changes position, then attacks until it holds or runs dry, then the next
// one goes.
static void enemyStep(void) {
    for (; battle.enemyActing < MAX_FIELD; battle.enemyActing++) {
        Combatant* c = battleField(SIDE_ENEMY, battle.enemyActing);
        if (!c || c->done) continue;
        if (aiConsiderCommand(SIDE_ENEMY, battle.enemyActing)) return;
        if (enemyConsiderSwitch(battle.enemyActing)) return;
        if (enemyConsiderProvoke(battle.enemyActing)) return;
        if (aiConsiderIntercept(SIDE_ENEMY, battle.enemyActing)) return;
        if (aiConsiderLink(SIDE_ENEMY, battle.enemyActing)) return;
        if (aiConsiderMove(SIDE_ENEMY, battle.enemyActing)) return;
        float score;
        int target;
        int mount = aiChooseAction(c, SIDE_PLAYER, 0, &target, &score, targetWhy, sizeof(targetWhy));
        if (mount >= 0 && c->actionsThisTurn > 0 && score < AI_HOLD_SCORE) mount = -1;   // hold fire
        if (mount < 0) { c->done = 1; continue; }
        if (overwatchTrigger(SIDE_ENEMY, slotOf(c))) return;   // your Overwatch fires first
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
    if (battle.forcedSlot >= 0) {   // a Force Swap, after its hit
        if (battle.phase == BP_PLAYER_TURN || battle.phase == BP_ENEMY_TURN || battle.phase == BP_DEPLOY) resolveForcedSwap();
        else battle.forcedSlot = -1;
        return;
    }
    if (battle.numQueued > 0) {   // Coordinated Strikes and Overwatch shots, one at a time
        if (battle.phase == BP_PLAYER_TURN || battle.phase == BP_ENEMY_TURN || battle.phase == BP_DEPLOY) runQueued();
        else battle.numQueued = 0;
        return;
    }

    if (battle.phase == BP_PLAYER_TURN) {
        Combatant* a = battleActing();
        if (a && (a->actionsThisTurn > 0 || a->moved) && !anyFireable(a)) a->done = 1;   // spent
        fixActor();
        if (battle.phaseActed && !anyPlayerActor() && !battleCanDeploy()) {
            logLine("All mechs have acted. Enemy phase.");
            beginEnemyTurn();
        }
    }
    else if (battle.phase == BP_ENEMY_TURN) enemyStep();
}

// Why the commanded mech can't fire this mount at the current target, NULL if it can
static const char* fireBlock(int mount) {
    if (!battleActing()) return "NO MECH TO COMMAND";
    if (!battleTarget()) return "NO TARGET";
    if (battleHidden(battle.playerTarget)) {
        const Combatant* d = battleTarget();
        return TextFormat(d->fresh && fwEffect(d->mech, CFX_STEALTH) > 0 ? "UNSEEN - CAN'T TARGET %s" : "BLACKOUT - CAN'T SEE %s",
            d->mech->name);
    }
    const Weapon* w = mechWeapon(battleActing()->mech, mount);
    int provoker = battleProvoker(SIDE_ENEMY);
    if (w && provoker >= 0 && provoker != battle.playerTarget && singleTarget(battleActing(), w))
        return TextFormat("PROVOKED - MUST TARGET %s", battleField(SIDE_ENEMY, provoker)->mech->name);
    const char* r = NULL;
    canFire(battleActing(), mount, &r);
    return r;
}

int battleCanFire(int mount, const char** reason) {
    const char* r = battle.phase != BP_PLAYER_TURN || battleBusy() ? "STANDBY" : fireBlock(mount);
    if (reason) *reason = r;
    return r == NULL;
}

void battleFire(int mount) {
    if (!battleCanFire(mount, NULL)) return;
    Combatant* a = battleActing();
    battle.phaseActed = 1;
    if (overwatchTrigger(SIDE_PLAYER, slotOf(a))) {   // the enemy's Overwatch fires first; the shot follows if it can
        queueShot(SIDE_PLAYER, slotOf(a), battle.side[SIDE_ENEMY].field[battle.playerTarget], SHOT_OWN, mount);
        return;
    }
    int fired = corruptedMount(a, mount);
    doAttack(a, SIDE_PLAYER, battle.actingSlot, SIDE_ENEMY, battle.playerTarget, fired, fired != mount ? "TARGETING CORRUPTED! " : NULL);
    syncLog();
}

// Autoplay for tests: the enemy AI's link / formation move for the commanded mech
int battleAIMoveForPlayer(void) {
    if (battle.phase != BP_PLAYER_TURN || battleBusy() || !battleActing()) return 0;
    if (!aiConsiderIntercept(SIDE_PLAYER, battle.actingSlot) && !aiConsiderLink(SIDE_PLAYER, battle.actingSlot)
        && !aiConsiderMove(SIDE_PLAYER, battle.actingSlot)) return 0;
    battle.phaseActed = 1;
    return 1;
}

// Autoplay for tests: the enemy AI's pick for the commanded mech (sets the target)
int battleAIChooseForPlayer(void) {
    if (battle.phase != BP_PLAYER_TURN || battleBusy() || !battleActing()) return -1;
    int target;
    int mount = aiChooseAction(battleActing(), SIDE_ENEMY, 0, &target, NULL, NULL, 0);
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
        linkUpkeep();
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
