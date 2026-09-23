#include "battle.h"
#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

Battle battle;

static float frand(void) { return (float)rand() / ((float)RAND_MAX + 1.0f); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ============ FORMULAS ============
float formulaAccuracyMod(float accuracy) { return accuracy / 100.0f; }
float formulaEvasionMod(float mobility) { return 1.0f - mobility / 200.0f; }

float formulaHitChance(float weaponAcc, float accuracy, float mobility) {
    float h = weaponAcc / 100.0f * formulaAccuracyMod(accuracy) * formulaEvasionMod(mobility);
    return clampf(h, HIT_CHANCE_MIN, HIT_CHANCE_MAX);
}

float formulaScrambleResist(float stability, float strength) {
    if (stability + strength <= 0) return 1.0f;
    return stability / (stability + strength);
}

void attackPreview(const Stats* atk, const Firmware* atkFw, const WeaponDef* w,
                   const Stats* def, const Firmware* defFw, const AttackContext* ctx, AttackPreview* out) {
    memset(out, 0, sizeof(*out));

    // Step 1 - hit chance
    out->weaponAcc = (float)w->accuracy;
    out->accMod = formulaAccuracyMod(ctx->attackerAccuracy);
    out->evasionMod = formulaEvasionMod(ctx->targetMobility);
    out->hitUnclamped = out->weaponAcc / 100.0f * out->accMod * out->evasionMod;
    out->spoofMod = ctx->spoofActive ? 1.0f - firmwareChipTotal(defFw, CFX_TARGETING_SPOOF) : 1.0f;
    out->hitChance = clampf(out->hitUnclamped * out->spoofMod, HIT_CHANCE_MIN, HIT_CHANCE_MAX);

    // Steps 3-4 - raw damage and weapon modifier
    out->baseDamage = (float)w->baseDamage;
    out->power = atk->v[STAT_POWER];
    out->raw = out->baseDamage * out->power;
    out->damageMod = w->damageMod;
    out->modified = out->raw * out->damageMod;

    // Step 5 - split between Armor and Integrity; armor damage beyond current armor spills
    out->pen = w->armorPen;
    if (ctx->targetArmor > ARMORED_THRESHOLD) out->pen += firmwareChipTotal(atkFw, CFX_PEN_VS_ARMORED);
    out->pen = clampf(out->pen, 0, 1);
    out->breachMod = (ctx->breachReady && ctx->targetArmor > 0) ? 1.0f + firmwareChipTotal(atkFw, CFX_ARMOR_BREACH) : 1.0f;
    float armorShare = out->modified * (1.0f - out->pen);
    out->toArmor = armorShare * out->breachMod;
    out->toIntegrity = out->modified * out->pen;
    out->armorBefore = ctx->targetArmor;
    int armorHit = (int)roundf(out->toArmor);
    out->armorDamage = armorHit < ctx->targetArmor ? armorHit : ctx->targetArmor;
    int spill = (int)roundf(armorShare) - ctx->targetArmor;   // the breach bonus never spills
    out->spill = spill > 0 ? spill : 0;
    out->integrityDamage = (int)roundf(out->toIntegrity) + out->spill;

    // Heat and scramble
    float heat = (float)w->heat;
    if (w->munition == MUN_ENERGY) heat *= 1.0f - firmwareChipTotal(atkFw, CFX_ENERGY_HEAT_CUT);
    out->heat = (int)roundf(heat);
    out->scramble = w->scramble;
    out->resist = w->scramble > 0
        ? formulaScrambleResist(def->v[STAT_STABILITY] + firmwareChipTotal(defFw, CFX_COUNTER_INTRUSION), (float)w->scramble)
        : 1.0f;
}

// Conditions for the first attack of a fresh round, used by the debug screen
AttackContext attackContextBaseline(const Stats* atk, const Firmware* atkFw,
                                    const Stats* def, const Firmware* defFw, int targetArmor) {
    AttackContext c;
    c.attackerAccuracy = atk->v[STAT_ACCURACY] + firmwareChipTotal(atkFw, CFX_PRECISION_STRIKE);
    c.targetMobility = def->v[STAT_MOBILITY];
    c.targetArmor = targetArmor;
    c.spoofActive = firmwareChipTotal(defFw, CFX_TARGETING_SPOOF) > 0;
    c.breachReady = firmwareChipTotal(atkFw, CFX_ARMOR_BREACH) > 0;
    return c;
}

// Hacking (capture): easier on damaged, common, low-Stability machines
float hackChance(const Mech* target, const Stats* targetStats) {
    float maxI = targetStats->v[STAT_INTEGRITY];
    float damage = maxI > 0 ? 1.0f - target->integrity / maxI : 0;
    int rarity = mechModel(target)->rarity;
    if (rarity < 1) rarity = 3;
    float chance = 0.25f + damage * 0.55f - (rarity - 1) * 0.08f - (targetStats->v[STAT_STABILITY] - 60) * 0.003f;
    return clampf(chance, 0.05f, 0.95f);
}

int revisionDataForWild(const Mech* enemy) { return 10 + enemy->fw.revision * 4 + rand() % 4; }
int revisionDataForTrainer(const Mech* enemy, int tier) { return 15 + enemy->fw.revision * 5 + tier * 8 + rand() % 5; }

// ============ COMBATANTS ============
static float chipTotal(const Combatant* c, ChipEffect e) { return firmwareChipTotal(&c->mech->fw, e); }

static int integrityBelow(const Combatant* c, float fraction) {
    return c->mech->integrity < c->stats.v[STAT_INTEGRITY] * fraction;
}

float combatAccuracy(const Combatant* c) {
    float acc = c->stats.v[STAT_ACCURACY] - c->accPenalty;
    if (c->actionsThisTurn == 0) acc += chipTotal(c, CFX_PRECISION_STRIKE);
    return clampf(acc, 0, 100);
}

float combatMobility(const Combatant* c) {
    float mob = c->stats.v[STAT_MOBILITY] + c->evasiveBonus;
    if (integrityBelow(c, 0.25f)) mob += chipTotal(c, CFX_EMERGENCY_EVASION);
    return clampf(mob, 0, 100);
}

static int effectiveCost(const Combatant* c, const WeaponDef* w) {
    if (c->actionsThisTurn == 0 && chipTotal(c, CFX_FIRST_ACTION_FREE) > 0) return 0;
    return w->energyCost;
}

static int shotHeat(const Combatant* c, const WeaponDef* w) {
    float heat = (float)w->heat;
    if (w->munition == MUN_ENERGY) heat *= 1.0f - chipTotal(c, CFX_ENERGY_HEAT_CUT);
    return (int)roundf(heat);
}

static int canFire(const Combatant* c, int mount, const char** reason) {
    const char* r = NULL;
    const WeaponDef* w = mechWeapon(c->mech, mount);
    if (!w) r = "EMPTY MOUNT";
    else if (mount == c->disabledWeapon) r = "WEAPON SCRAMBLED";
    else if (w->ammo > 0 && c->mech->weapons[mount].ammo <= 0) r = "NO AMMO";
    else if (effectiveCost(c, w) > c->energy) r = "INSUFFICIENT ENERGY";
    else if (c->heat + shotHeat(c, w) > c->stats.v[STAT_HEAT]) r = "THERMAL LIMIT";
    if (reason) *reason = r;
    return r == NULL;
}

static int anyFireable(const Combatant* c) {
    for (int i = 0; i < MAX_WEAPONS; i++) if (canFire(c, i, NULL)) return 1;
    return 0;
}

static void initCombatant(Combatant* c, Mech* m) {
    memset(c, 0, sizeof(*c));
    c->mech = m;
    mechStats(m, &c->stats);
    c->disabledWeapon = -1;
    c->nextDisabledWeapon = -1;
    mechReloadWeapons(m);
}

// Start of this side's turn: refresh Energy, dissipate Heat, apply queued scrambles
static void turnStart(Combatant* c) {
    c->energy = (int)c->stats.v[STAT_ENERGY] - c->nextEnergyLoss;
    if (!c->emergencyPowerUsed && integrityBelow(c, 0.25f) && chipTotal(c, CFX_EMERGENCY_POWER) > 0) {
        c->energy += (int)chipTotal(c, CFX_EMERGENCY_POWER);
        c->emergencyPowerUsed = 1;
    }
    if (!c->lastStandUsed && integrityBelow(c, 0.10f) && chipTotal(c, CFX_LAST_STAND) > 0) {
        c->energy += (int)chipTotal(c, CFX_LAST_STAND);
        c->lastStandUsed = 1;
    }
    if (c->energy < 0) c->energy = 0;
    c->heat -= (int)c->stats.v[STAT_COOLING];
    if (c->heat < 0) c->heat = 0;
    c->accPenalty = c->nextAccPenalty;
    c->disabledWeapon = c->nextDisabledWeapon;
    c->skipTurn = c->nextSkipTurn;
    c->nextEnergyLoss = c->nextAccPenalty = c->nextSkipTurn = 0;
    c->nextDisabledWeapon = -1;
    c->evasiveBonus = 0;
    c->actionsThisTurn = 0;
    c->attackedThisRound = 0;
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
// Scramble outcome depends on strength (design doc 4.6); it hits the victim's next turn
static const char* applyScramble(Combatant* d, int strength) {
    if (strength >= 100) { d->nextSkipTurn = 1; return "TURN LOST"; }
    if (strength >= 70) {
        int mounts[MAX_WEAPONS], n = 0;
        for (int i = 0; i < MAX_WEAPONS; i++) if (d->mech->weapons[i].def >= 0) mounts[n++] = i;
        if (n > 0) { d->nextDisabledWeapon = mounts[rand() % n]; return "WEAPON DISABLED"; }
    }
    if (strength >= 40) { d->nextAccPenalty = 15; return "ACCURACY -15"; }
    d->nextEnergyLoss += 1;
    return "ENERGY -1";
}

static AttackContext liveContext(const Combatant* a, const Combatant* d) {
    AttackContext ctx;
    ctx.attackerAccuracy = combatAccuracy(a);
    ctx.targetMobility = combatMobility(d);
    ctx.targetArmor = d->mech->armor;
    ctx.spoofActive = !d->attackedThisRound && chipTotal(d, CFX_TARGETING_SPOOF) > 0;
    ctx.breachReady = !a->breachUsed && chipTotal(a, CFX_ARMOR_BREACH) > 0;
    return ctx;
}

int battlePreviewPlayer(int mount, AttackPreview* out) {
    const WeaponDef* w = mechWeapon(battle.player.mech, mount);
    if (!w) return 0;
    AttackContext ctx = liveContext(&battle.player, &battle.enemy);
    attackPreview(&battle.player.stats, &battle.player.mech->fw, w, &battle.enemy.stats, &battle.enemy.mech->fw, &ctx, out);
    return 1;
}

static void doAttack(Combatant* a, Combatant* d, int mount, int isPlayer) {
    const WeaponDef* w = mechWeapon(a->mech, mount);
    AttackContext ctx = liveContext(a, d);
    AttackPreview p;
    attackPreview(&a->stats, &a->mech->fw, w, &d->stats, &d->mech->fw, &ctx, &p);

    a->energy -= effectiveCost(a, w);
    if (w->ammo > 0) a->mech->weapons[mount].ammo--;
    a->heat += p.heat;
    a->actionsThisTurn++;
    d->attackedThisRound = 1;

    const char* who = isPlayer ? a->mech->name : TextFormat("Enemy %s", a->mech->name);
    int hit = frand() < p.hitChance;
    int total = 0;
    char extra[96] = "";
    if (hit) {
        d->mech->armor -= p.armorDamage;
        d->mech->integrity -= p.integrityDamage;
        if (d->mech->integrity < 0) d->mech->integrity = 0;
        if (p.breachMod > 1.0f) a->breachUsed = 1;
        total = p.armorDamage + p.integrityDamage;
        if (p.scramble > 0 && d->mech->integrity > 0) {
            if (frand() < p.resist) {
                float heal = chipTotal(d, CFX_SYSTEM_RECOVERY);
                int maxI = (int)d->stats.v[STAT_INTEGRITY];
                d->mech->integrity += (int)heal;
                if (d->mech->integrity > maxI) d->mech->integrity = maxI;
                snprintf(extra, sizeof(extra), " Scramble resisted%s.", heal > 0 ? " (recovered)" : "");
            }
            else snprintf(extra, sizeof(extra), " SCRAMBLED: %s!", applyScramble(d, p.scramble));
        }
        if (total > 0)
            snprintf(battle.log, sizeof(battle.log), "%s fired %s! %d DMG (%d ARM / %d INT).%s",
                who, w->name, total, p.armorDamage, p.integrityDamage, extra);
        else
            snprintf(battle.log, sizeof(battle.log), "%s activated %s.%s", who, w->name, extra);
    }
    else snprintf(battle.log, sizeof(battle.log), "%s fired %s... MISSED!", who, w->name);

    a->evasiveBonus = (int)chipTotal(a, CFX_EVASIVE_MANEUVER);
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
        mechStats(m, &battle.player.stats);
    }
}

static void beginEnemyTurn(void) {
    turnStart(&battle.enemy);
    battle.phase = BP_ENEMY_TURN;
    if (battle.enemy.skipTurn) {
        snprintf(battle.log, sizeof(battle.log), "Enemy %s is scrambled and loses its turn!", battle.enemyMech.name);
        battle.enemy.energy = 0;
        battle.animTimer = 1.2f;
    }
}

static void beginPlayerTurn(void) {
    battle.round++;
    turnStart(&battle.player);
    battle.phase = BP_PLAYER_TURN;
    snprintf(battle.log, sizeof(battle.log), "REACTOR RECHARGED: %d EN. Round %d.", battle.player.energy, battle.round);
    if (battle.player.skipTurn) {
        snprintf(battle.log, sizeof(battle.log), "SYSTEMS SCRAMBLED! %s loses its turn.", battle.player.mech->name);
        beginEnemyTurn();
        battle.animTimer = 1.2f;
    }
}

static int enemyChooseWeapon(void) {
    int choices[MAX_WEAPONS], n = 0;
    for (int i = 0; i < MAX_WEAPONS; i++) if (canFire(&battle.enemy, i, NULL)) choices[n++] = i;
    if (n == 0) return -1;
    int pick = choices[rand() % n];
    // Lean toward heavier weapons while energy allows
    for (int i = 0; i < n; i++)
        if (mechWeapon(battle.enemy.mech, choices[i])->energyCost >= 2 && rand() % 2 == 0) { pick = choices[i]; break; }
    return pick;
}

// ============ START ============
static void beginBattle(void) {
    battle.phase = BP_PLAYER_TURN;
    battle.dialogue = DLG_NONE;
    battle.round = 0;
    battle.animTimer = 0;
    battle.outcomePending = 0;
    battle.dataEarned = 0;
    battle.revisionsGained = 0;
    battle.hacked = 0;
    battle.result = RESULT_NONE;
    battle.numEvents = 0;
    battle.oldRevision = rosterActive()->fw.revision;
    initCombatant(&battle.player, rosterActive());
    initCombatant(&battle.enemy, &battle.enemyMech);
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
    battle.enemyMech = mechCreate(model, rand() % 4);
    beginBattle();
    snprintf(battle.log, sizeof(battle.log), "HOSTILE %s detected! Reactor online (%d energy).",
        battle.enemyMech.name, battle.player.energy);
}

void battleStartTrainer(int trainerIdx) {
    if (trainerIdx < 0 || trainerIdx >= NUM_TRAINERS) { battleStartWild(); return; }
    Trainer* t = &trainers[trainerIdx];
    t->numDefeated = 0;
    battle.trainer = trainerIdx;
    battle.enemyMech = mechCreate(t->teamModels[0], t->teamRevisions[0]);
    beginBattle();
    snprintf(battle.log, sizeof(battle.log), "%s sent out %s! (1/%d)", t->name, battle.enemyMech.name, t->numMechs);
    battle.dialogue = DLG_INTRO;
    snprintf(battle.dialogueText, sizeof(battle.dialogueText), "\"%s\"", t->introLine);
}

// ============ OUTCOME ============
static void enemyScrapped(void) {
    if (battle.trainer >= 0) {
        Trainer* t = &trainers[battle.trainer];
        t->numDefeated++;
        awardData(revisionDataForTrainer(&battle.enemyMech, t->tier));
        if (t->numDefeated < t->numMechs) {
            battle.enemyMech = mechCreate(t->teamModels[t->numDefeated], t->teamRevisions[t->numDefeated]);
            initCombatant(&battle.enemy, &battle.enemyMech);
            battle.round = 0;
            beginPlayerTurn();
            snprintf(battle.log, sizeof(battle.log), "%s sent out %s! (%d/%d)",
                t->name, battle.enemyMech.name, t->numDefeated + 1, t->numMechs);
        }
        else {
            t->defeated = 1;
            battle.phase = BP_VICTORY;
            snprintf(battle.log, sizeof(battle.log), "ALL MECHS DOWN! %s defeated!", t->name);
            battle.dialogue = DLG_DEFEAT;
            snprintf(battle.dialogueText, sizeof(battle.dialogueText), "\"%s\"", t->defeatLine);
        }
    }
    else {
        int data = revisionDataForWild(&battle.enemyMech);
        awardData(data);
        battle.phase = BP_VICTORY;
        if (battle.revisionsGained > 0)
            snprintf(battle.log, sizeof(battle.log), "TARGET %s SCRAPPED! Firmware revision ready!", battle.enemyMech.name);
        else
            snprintf(battle.log, sizeof(battle.log), "TARGET %s SCRAPPED! +%d DATA.", battle.enemyMech.name, data);
    }
}

// Returns 1 if the battle state changed because a mech went down
static int checkOutcome(void) {
    battle.outcomePending = 0;
    if (battle.enemyMech.integrity <= 0) { enemyScrapped(); return 1; }
    if (battle.player.mech->integrity <= 0) {
        battle.phase = BP_DEFEAT;
        snprintf(battle.log, sizeof(battle.log), "%s DISABLED!", battle.player.mech->name);
        return 1;
    }
    return 0;
}

static void finish(BattleResult result) {
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
        int mount = enemyChooseWeapon();
        if (mount >= 0) doAttack(&battle.enemy, &battle.player, mount, 0);
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
    doAttack(&battle.player, &battle.enemy, mount, 1);
}

void battleEndTurn(void) {
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) return;
    snprintf(battle.log, sizeof(battle.log), "Ending turn. Enemy taking action...");
    beginEnemyTurn();
}

int battleCanHack(void) {
    return battle.phase == BP_PLAYER_TURN && battle.trainer < 0 && teamSize < MAX_TEAM;
}

// Hacking costs the rest of the turn
void battleHack(void) {
    if (!battleCanHack() || battleBusy()) return;
    pushEvent(FX_SCAN, 1, 0, 1, (Color) { 120, 255, 220, 255 });
    battle.animTimer = fxDuration(FX_SCAN);
    if (frand() < hackChance(&battle.enemyMech, &battle.enemy.stats)) {
        Mech caught = battle.enemyMech;
        mechRepair(&caught);
        mechReloadWeapons(&caught);
        caught.fw.data = 0;
        rosterAdd(&caught);
        battle.hacked = 1;
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
        finish(RESULT_TO_WORLD);
    }
}
