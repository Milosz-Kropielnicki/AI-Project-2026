#include "battle.h"
#include "world.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
    out->dmgMod = out->executeMod * out->overchargeMod * out->reductionMod * out->firstHitMod * out->adaptiveMod;
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
    int gap = enemy->fw.revision - rosterActive()->fw.revision;
    return gap > 0 ? gap * 3 : 0;
}
int revisionDataForWild(const Mech* enemy) { return 10 + enemy->fw.revision * 4 + levelGapBonus(enemy) + rand() % 4; }
int revisionDataForTrainer(const Mech* enemy, int tier) {
    return 15 + enemy->fw.revision * 5 + tier * 8 + levelGapBonus(enemy) + rand() % 5;
}
int revisionDataForParticipation(const Mech* enemy) { return 5 + enemy->fw.revision * 2; }

// ============ COMBATANTS ============
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
    return ctx;
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

// Best-scoring weapon for this side, -1 if none. ignoreResources skips the
// Energy / Heat / scramble checks (Dead-Man Protocol's free final shot).
static int aiChooseMount(const Combatant* a, const Combatant* d, int ignoreResources, float* bestScore) {
    int best = -1;
    float bestValue = -1;
    AttackContext ctx = liveContext(a, d);
    for (int i = 0; i < MAX_WEAPONS; i++) {
        const Weapon* w = mechWeapon(a->mech, i);
        if (!w) continue;
        if (ignoreResources ? (w->ammo > 0 && a->mech->weapons[i].ammo <= 0) : !canFire(a, i, NULL)) continue;
        float v = aiScoreAttack(a->mech, w, d->mech, &ctx, a->ai);
        if (v > bestValue) { bestValue = v; best = i; }
    }
    if (bestScore) *bestScore = bestValue;
    return best;
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
    c->ai = &aiDefault;
    firmwareClearCorruption(&m->fw);
    mechRefreshStats(m);
    m->stats.heat = 0;
    mechReloadWeapons(m);
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

// Start of this side's turn: tick corruption, refill Energy, cool Heat, apply
// queued scrambles, then run Behavioral (IF/THEN) chips. Returns a log note.
static const char* turnStart(Combatant* c) {
    MechStats* s = &c->mech->stats;
    firmwareCorruptionTick(&c->mech->fw);
    mechRefreshStats(c->mech);   // a stat chip may have come back online
    s->energy = s->maxEnergy - c->nextEnergyLoss + (int)fwEffect(c->mech, CFX_BONUS_ENERGY);
    if (!c->emergencyPowerUsed && integrityBelow(c, 0.25f) && fwEffect(c->mech, CFX_EMERGENCY_POWER) > 0) {
        s->energy += (int)fwEffect(c->mech, CFX_EMERGENCY_POWER);
        c->emergencyPowerUsed = 1;
    }
    if (!c->lastStandUsed && integrityBelow(c, 0.10f) && fwEffect(c->mech, CFX_LAST_STAND) > 0) {
        s->energy += (int)fwEffect(c->mech, CFX_LAST_STAND);
        c->lastStandUsed = 1;
    }
    if (s->energy < 0) s->energy = 0;
    s->heat = formulaHeatAfterCooling(s->heat, s->cooling);
    c->accPenalty = c->nextAccPenalty;
    c->disabledWeapon = c->nextDisabledWeapon;
    c->skipTurn = c->nextSkipTurn;
    if (c->skipTurn) c->skipImmune = 2;          // no stun-lock: two normal turns before the next loss
    else if (c->skipImmune > 0) c->skipImmune--;
    c->energyTax = c->nextEnergyTax;
    c->randomTargeting = c->nextRandomTargeting;
    c->nextEnergyLoss = c->nextAccPenalty = c->nextSkipTurn = c->nextEnergyTax = c->nextRandomTargeting = 0;
    c->nextDisabledWeapon = -1;
    c->evasiveBonus = 0;
    c->actionsThisTurn = 0;
    c->attackedThisRound = 0;

    static char note[64];
    note[0] = 0;
    int repair = (int)fwEffect(c->mech, CFX_EMERGENCY_REPAIR);
    if (repair > 0 && integrityBelow(c, 0.30f) && s->energy >= 2) {
        s->energy -= 2;
        s->integrity = clampi(s->integrity + repair, 0, s->maxIntegrity);
        snprintf(note, sizeof(note), " REPAIR PROTOCOL +%d INT.", repair);
    }
    int vent = (int)fwEffect(c->mech, CFX_COOLANT_DUMP);
    if (vent > 0 && s->heat > s->maxHeat * 0.75f && s->energy >= 1) {
        s->energy -= 1;
        s->heat = s->heat > vent ? s->heat - vent : 0;
        snprintf(note + strlen(note), sizeof(note) - strlen(note), " COOLANT DUMP -%d HEAT.", vent);
    }
    return note;
}

// ============ EVENTS ============
static void pushEvent(int fx, int fromPlayer, int damage, int hit, Color color) {
    if (battle.numEvents >= MAX_BATTLE_EVENTS) return;
    battle.events[battle.numEvents++] = (BattleEvent){ fx, fromPlayer, damage, hit, color };
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
    default: return 0.55f;
    }
}

// ============ ACTIONS ============
int battlePreviewPlayer(int mount, AttackPreview* out) {
    const Weapon* w = mechWeapon(battle.player.mech, mount);
    if (!w) return 0;
    AttackContext ctx = liveContext(&battle.player, &battle.enemy);
    attackPreview(battle.player.mech, w, battle.enemy.mech, &ctx, out);
    return 1;
}

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

// prefix, if set, is prepended to the log line (Dead-Man, corrupted targeting)
static void doAttack(Combatant* a, Combatant* d, int mount, int isPlayer, const char* prefix) {
    const Weapon* w = mechWeapon(a->mech, mount);
    AttackContext ctx = liveContext(a, d);
    AttackPreview p;
    attackPreview(a->mech, w, d->mech, &ctx, &p);

    MechStats* as = &a->mech->stats;
    MechStats* ds = &d->mech->stats;
    as->energy -= p.energyCost;
    as->heat += p.heat;
    if (w->ammo > 0) a->mech->weapons[mount].ammo--;
    a->actionsThisTurn++;
    d->attackedThisRound = 1;

    const char* who = isPlayer ? a->mech->name : TextFormat("Enemy %s", a->mech->name);
    int hit = frand() < p.hitChance;
    int total = 0;
    char extra[96] = "";
    if (hit) {
        ds->armor -= p.armorDamage;
        ds->integrity -= p.integrityDamage;
        if (ds->integrity < 0) ds->integrity = 0;
        if (p.breachBonus > 0) a->breachUsed = 1;
        total = p.armorDamage + p.integrityDamage;
        if (total > 0) {
            d->hitTaken = 1;
            d->lastMunitionTaken = w->munition;
        }
        if (p.scramble > 0 && ds->integrity > 0) {
            if (frand() < p.resist) {
                int heal = (int)fwEffect(d->mech, CFX_SYSTEM_RECOVERY);
                ds->integrity = clampi(ds->integrity + heal, 0, ds->maxIntegrity);
                snprintf(extra, sizeof(extra), " Scramble resisted%s.", heal > 0 ? " (recovered)" : "");
            }
            else snprintf(extra, sizeof(extra), " SCRAMBLED: %s!", applyScramble(d, p.scramble, w->virus));
        }
        if (total > 0)
            snprintf(battle.log, sizeof(battle.log), "%s fired %s! %d DMG (%d ARM / %d INT).%s",
                who, w->name, total, p.armorDamage, p.integrityDamage, extra);
        else
            snprintf(battle.log, sizeof(battle.log), "%s activated %s.%s", who, w->name, extra);
    }
    else {
        snprintf(battle.log, sizeof(battle.log), "%s fired %s... MISSED! (%d%% to hit)", who, w->name,
            (int)roundf(p.hitChance * 100));
        if (fwEffect(a->mech, CFX_RECURSIVE_TARGETING) > 0) a->missStacks++;
    }

    if (prefix) {
        char line[sizeof(battle.log)];
        snprintf(line, sizeof(line), "%s%s", prefix, battle.log);
        memcpy(battle.log, line, sizeof(line));
    }
    a->evasiveBonus = (int)fwEffect(a->mech, CFX_EVASIVE_MANEUVER);
    pushEvent(w->fx, isPlayer, total, hit, munitionColor(w->munition));
    battle.animTimer = fxDuration(w->fx);
    battle.outcomePending = 1;
}

static void awardData(int amount) {
    Mech* m = rosterActive();
    battle.dataEarned += amount;
    int gained = firmwareAddData(&m->fw, amount);
    if (gained > 0) {
        battle.revisionsGained += gained;
        mechRefreshStats(m);
    }
}

static void beginEnemyTurn(void) {
    const char* note = turnStart(&battle.enemy);
    battle.phase = BP_ENEMY_TURN;
    if (note[0]) snprintf(battle.log, sizeof(battle.log), "Enemy %s:%s", battle.enemyMech.name, note);
    if (battle.enemy.skipTurn) {
        snprintf(battle.log, sizeof(battle.log), "Enemy %s is scrambled and loses its turn!", battle.enemyMech.name);
        battle.enemyMech.stats.energy = 0;
        battle.animTimer = 1.2f;
    }
}

static void beginPlayerTurn(void) {
    battle.round++;
    const char* note = turnStart(&battle.player);
    battle.phase = BP_PLAYER_TURN;
    snprintf(battle.log, sizeof(battle.log), "REACTOR RECHARGED: %d EN, heat vented to %d. Round %d.%s",
        battle.player.mech->stats.energy, battle.player.mech->stats.heat, battle.round, note);
    if (battle.player.skipTurn) {
        snprintf(battle.log, sizeof(battle.log), "SYSTEMS SCRAMBLED! %s loses its turn.", battle.player.mech->name);
        beginEnemyTurn();
        battle.animTimer = 1.2f;
    }
}

// ============ START ============
static const AIProfile* enemyAI(void) {
    return battle.enemyArchetype >= 0 ? &archetypes[battle.enemyArchetype].ai : &aiDefault;
}

static void spawnArchetype(int archetype, int level) {
    battle.enemyMech = archetypeBuild(archetype, level);
    battle.enemyArchetype = archetype;
}

static void beginBattle(void) {
    battle.phase = BP_PLAYER_TURN;
    battle.dialogue = DLG_NONE;
    battle.testRange = 0;
    battle.round = 0;
    battle.animTimer = 0;
    battle.outcomePending = 0;
    battle.dataEarned = 0;
    battle.revisionsGained = 0;
    battle.hacked = 0;
    battle.loot[0] = 0;
    battle.result = RESULT_NONE;
    battle.numEvents = 0;
    battle.oldRevision = rosterActive()->fw.revision;
    initCombatant(&battle.player, rosterActive());
    initCombatant(&battle.enemy, &battle.enemyMech);
    battle.enemy.ai = enemyAI();
    beginPlayerTurn();
}

void battleStartWild(void) {
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
    battle.trainer = -1;
    int level = gameWildLevel(worldCurrentZone());
    if (rand() % 10 < 7) spawnArchetype(rand() % NUM_WILD_ARCHETYPES, level);
    else {   // a stock chassis running random chips
        battle.enemyMech = mechCreateStock(model, level);
        battle.enemyArchetype = -1;
        equipEnemyChips(&battle.enemyMech);
    }
    beginBattle();
    snprintf(battle.log, sizeof(battle.log), "HOSTILE %s detected! Reactor online (%d energy).",
        battle.enemyMech.name, battle.player.mech->stats.energy);
}

void battleStartTrainer(int trainerIdx) {
    if (trainerIdx < 0 || trainerIdx >= NUM_TRAINERS) { battleStartWild(); return; }
    Trainer* t = &trainers[trainerIdx];
    t->numDefeated = 0;
    battle.trainer = trainerIdx;
    spawnArchetype(t->teamArchetypes[0], t->teamRevisions[0]);
    beginBattle();
    snprintf(battle.log, sizeof(battle.log), "%s sent out %s! (1/%d)", t->name, battle.enemyMech.name, t->numMechs);
    battle.dialogue = DLG_INTRO;
    snprintf(battle.dialogueText, sizeof(battle.dialogueText), "\"%s\"", t->introLine);
}

void battleResetDummy(void) {
    battle.enemyMech = mechCreateStock(MODEL_DUMMY, 0);
    battle.enemyArchetype = -1;
    snprintf(battle.enemyMech.name, sizeof(battle.enemyMech.name), "TARGET DUMMY");
    initCombatant(&battle.enemy, &battle.enemyMech);
}

void battleStartTestRange(void) {
    Mech* m = rosterActive();
    battle.savedIntegrity = m->stats.integrity;
    battle.savedArmor = m->stats.armor;
    battle.trainer = -1;
    battle.dummyKills = 0;
    battleResetDummy();
    beginBattle();
    battle.testRange = 1;
    snprintf(battle.log, sizeof(battle.log), "TEST RANGE: %s vs TARGET DUMMY. The dummy never fires back.", m->name);
}

void battleEndTestRange(void) {
    Mech* m = rosterActive();
    mechRefreshStats(m);
    m->stats.integrity = battle.savedIntegrity < m->stats.maxIntegrity ? battle.savedIntegrity : m->stats.maxIntegrity;
    m->stats.armor = battle.savedArmor < m->stats.maxArmor ? battle.savedArmor : m->stats.maxArmor;
    m->stats.heat = 0;
    m->stats.energy = m->stats.maxEnergy;
    mechReloadWeapons(m);
    battle.testRange = 0;
}

// ============ OUTCOME ============
static void encounterMechDown(void);

static void enemyScrapped(void) {
    if (battle.testRange) {
        battle.dummyKills++;
        battleResetDummy();
        snprintf(battle.log, sizeof(battle.log), "TARGET DUMMY destroyed (%d). A new dummy is online.", battle.dummyKills);
        return;
    }
    if (battle.trainer >= 0) encounterMechDown();
    else {
        int data = revisionDataForWild(&battle.enemyMech);
        awardData(data);
        gameOnWildScrapped(&battle.enemyMech, battle.loot, sizeof(battle.loot));
        battle.phase = BP_VICTORY;
        if (battle.revisionsGained > 0)
            snprintf(battle.log, sizeof(battle.log), "TARGET %s SCRAPPED! Firmware revision ready!", battle.enemyMech.name);
        else
            snprintf(battle.log, sizeof(battle.log), "TARGET %s SCRAPPED! +%d DATA.", battle.enemyMech.name, data);
    }
}

// One of an encounter squad's mechs is out (scrapped or reprogrammed): send the
// next one, or pay out the encounter.
static void encounterMechDown(void) {
    Trainer* t = &trainers[battle.trainer];
    t->numDefeated++;
    awardData(revisionDataForTrainer(&battle.enemyMech, t->tier));
    if (t->numDefeated < t->numMechs) {
        spawnArchetype(t->teamArchetypes[t->numDefeated], t->teamRevisions[t->numDefeated]);
        initCombatant(&battle.enemy, &battle.enemyMech);
        battle.enemy.ai = enemyAI();
        battle.round = 0;
        beginPlayerTurn();
        snprintf(battle.log, sizeof(battle.log), "%s sent out %s! (%d/%d)",
            t->name, battle.enemyMech.name, t->numDefeated + 1, t->numMechs);
    }
    else {
        t->defeated = 1;
        gameOnEncounterDefeated(battle.trainer, battle.loot, sizeof(battle.loot));
        battle.phase = BP_VICTORY;
        snprintf(battle.log, sizeof(battle.log), "ALL MECHS DOWN! %s defeated!", t->name);
        battle.dialogue = DLG_DEFEAT;
        snprintf(battle.dialogueText, sizeof(battle.dialogueText), "\"%s\"", t->defeatLine);
    }
}

// Dead-Man Protocol: a machine reduced to 0 Integrity fires one last shot,
// its best weapon, free of Energy and Heat.
static int tryDeadMan(Combatant* c, Combatant* target, int isPlayer) {
    MechStats* s = &c->mech->stats;
    if (s->integrity > 0 || c->deadManUsed || target->mech->stats.integrity <= 0) return 0;
    if (fwEffect(c->mech, CFX_DEAD_MAN) <= 0) return 0;
    c->deadManUsed = 1;
    int mount = aiChooseMount(c, target, 1, NULL);
    if (mount < 0) return 0;
    int energy = s->energy, heat = s->heat;
    doAttack(c, target, mount, isPlayer, "DEAD-MAN PROTOCOL! ");
    s->energy = energy;
    s->heat = heat;
    return 1;
}

// Returns 1 if the battle state changed because a mech went down. Dead-Man
// shots resolve first; if both machines end at 0, the player loses.
static int checkOutcome(void) {
    battle.outcomePending = 0;
    if (tryDeadMan(&battle.enemy, &battle.player, 0)) return 1;
    if (tryDeadMan(&battle.player, &battle.enemy, 1)) return 1;
    if (battle.player.mech->stats.integrity <= 0) {
        if (!battle.testRange) {
            awardData(revisionDataForParticipation(&battle.enemyMech));
            gameOnPlayerDisabled(battle.loot, sizeof(battle.loot));
        }
        battle.phase = BP_DEFEAT;
        snprintf(battle.log, sizeof(battle.log), "%s DISABLED!", battle.player.mech->name);
        return 1;
    }
    if (battle.enemyMech.stats.integrity <= 0) { enemyScrapped(); return 1; }
    return 0;
}

// Leaving a battle: vent heat and refill energy for the overworld
static void finish(BattleResult result) {
    Mech* m = rosterActive();
    firmwareClearCorruption(&m->fw);
    mechRefreshStats(m);
    m->stats.heat = 0;
    m->stats.energy = m->stats.maxEnergy;
    battle.result = result;
    battle.phase = BP_OVER;
}

// ============ UPDATE / INPUT ============
int battleBusy(void) { return battle.dialogue != DLG_NONE || battle.animTimer > 0; }

void battleUpdate(float dt) {
    if (battle.dialogue != DLG_NONE) return;
    if (battle.animTimer > 0) {
        battle.animTimer -= dt;
        if (battle.animTimer > 0) return;
    }
    if (battle.outcomePending && checkOutcome()) return;

    if (battle.phase == BP_PLAYER_TURN) {
        if (battle.player.actionsThisTurn > 0 && !anyFireable(&battle.player)) {
            snprintf(battle.log, sizeof(battle.log), "REACTOR DEPLETED. Enemy's turn!");
            beginEnemyTurn();
        }
    }
    else if (battle.phase == BP_ENEMY_TURN) {
        float score;
        int mount = aiChooseMount(&battle.enemy, &battle.player, 0, &score);
        if (mount >= 0 && battle.enemy.actionsThisTurn > 0 && score < AI_HOLD_SCORE) mount = -1;   // hold fire
        if (mount >= 0) {
            int fired = corruptedMount(&battle.enemy, mount);
            doAttack(&battle.enemy, &battle.player, fired, 0, fired != mount ? "TARGETING CORRUPTED! " : NULL);
        }
        else beginPlayerTurn();
    }
}

int battleCanFire(int mount, const char** reason) {
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) {
        if (reason) *reason = "STANDBY";
        return 0;
    }
    return canFire(&battle.player, mount, reason);
}

void battleFire(int mount) {
    if (!battleCanFire(mount, NULL)) return;
    int fired = corruptedMount(&battle.player, mount);
    doAttack(&battle.player, &battle.enemy, fired, 1, fired != mount ? "TARGETING CORRUPTED! " : NULL);
}

int battleAIChooseForPlayer(void) {
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) return -1;
    return aiChooseMount(&battle.player, &battle.enemy, 0, NULL);
}

void battleEndTurn(void) {
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) return;
    snprintf(battle.log, sizeof(battle.log), "Ending turn. Enemy taking action...");
    beginEnemyTurn();
}

// Wild machines are rogue AI; encounter squads only if their faction is rogue
// AI, and never a boss.
static int enemyHackable(void) {
    if (battle.testRange) return 0;
    if (battle.trainer < 0) return factionHackable(FAC_WILD);
    return factionHackable(trainers[battle.trainer].faction) && !battle.enemyMech.boss;
}

int battleCanHack(void) {
    return battle.phase == BP_PLAYER_TURN && enemyHackable() && teamSize < MAX_TEAM;
}

int battleHackStability(void) {
    return battle.enemyMech.stats.stability + (int)fwEffect(&battle.enemyMech, CFX_COUNTER_INTRUSION)
        - HACK_STABILITY_PER_EFFECT * pendingScrambles(&battle.enemy);
}

float battleHackChance(void) { return hackChance(hackStrength(battle.player.mech), battleHackStability()); }

// Hacking costs the rest of the turn
void battleHack(void) {
    if (!battleCanHack() || battleBusy()) return;
    pushEvent(FX_SCAN, 1, 0, 1, (Color) { 120, 255, 220, 255 });
    battle.animTimer = fxDuration(FX_SCAN);
    if (frand() < battleHackChance()) {
        Mech caught = battle.enemyMech;
        firmwareClearCorruption(&caught.fw);
        mechRepair(&caught);
        mechReloadWeapons(&caught);
        caught.fw.data = 0;
        rosterAdd(&caught);
        gameOnHacked(&caught, battle.enemyArchetype, battle.loot, sizeof(battle.loot));   // its parts and chips join the inventory
        battle.hacked = 1;
        if (battle.trainer >= 0) {   // a rogue AI squad keeps fighting
            encounterMechDown();
            if (battle.phase != BP_VICTORY)
                snprintf(battle.log, sizeof(battle.log), "REPROGRAMMED %s! Next machine incoming.", caught.name);
            return;
        }
        awardData(revisionDataForWild(&battle.enemyMech) / 2);
        battle.phase = BP_VICTORY;
        snprintf(battle.log, sizeof(battle.log), "REPROGRAMMED %s! Added to team (%d/%d).", caught.name, teamSize, MAX_TEAM);
    }
    else {
        beginEnemyTurn();
        if (!battle.enemy.skipTurn)
            snprintf(battle.log, sizeof(battle.log), "HACK FAILED! %s rejected the intrusion.", battle.enemyMech.name);
    }
}

void battleConfirm(void) {
    if (battle.dialogue != DLG_NONE) {
        BattleDialogue was = battle.dialogue;
        battle.dialogue = DLG_NONE;
        if (was != DLG_DEFEAT) return;
    }
    else if (battle.animTimer > 0) return;

    if (battle.phase == BP_VICTORY) {
        mechReplate(rosterActive());
        finish(battle.revisionsGained > 0 ? RESULT_TO_REVISION : RESULT_TO_WORLD);
    }
    else if (battle.phase == BP_DEFEAT) {
        mechRepair(rosterActive());   // recovered and repaired after being disabled
        finish(battle.revisionsGained > 0 ? RESULT_TO_REVISION : RESULT_TO_WORLD);
    }
}
