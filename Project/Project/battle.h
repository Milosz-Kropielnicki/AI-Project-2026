#ifndef BATTLE_H
#define BATTLE_H

#include "raylib.h"
#include "mech.h"

// ============ FORMULAS (design doc section 4) ============
// Pure functions shared by the battle, the damage preview and the debug
// screen, so every number shown on screen is exactly what combat uses.
#define HIT_CHANCE_MIN 0.05f
#define HIT_CHANCE_MAX 0.95f
#define ARMORED_THRESHOLD 50        // "targets with > 50 Armor"

// Hit Chance = Weapon Accuracy x (Accuracy / 100) x (1 - Mobility / 200)
float formulaHitUnclamped(int weaponAcc, int accuracy, int mobility);
float formulaHitChance(int weaponAcc, int accuracy, int mobility);     // clamped 5-95%
// Raw Damage = Weapon Base Damage x Power
float formulaRawDamage(int baseDamage, float power);

// Integrity Damage = Raw x Penetration, Armor Damage = Raw x (1 - Penetration),
// armor damage beyond the current Armor spills into Integrity.
typedef struct {
    float toIntegrity;      // raw x pen
    float toArmor;          // raw x (1 - pen)
    int armorDamage;        // absorbed by armor
    int spill;              // armor damage beyond current armor
    int integrityDamage;    // round(toIntegrity) + spill
} DamageSplit;
DamageSplit formulaDamageSplit(float raw, int penPercent, int currentArmor);

int formulaHeatAfterCooling(int heat, int cooling);                    // max(0, heat - cooling)
// Resistance = Stability / (Stability + Scramble Strength)
float formulaScrambleResist(int stability, int strength);

// Every intermediate number of one attack (doc 4.7), for combat and previews
typedef struct {
    // hit chance
    int weaponAcc, accuracy, mobility;
    float accMod, evasionMod, hitUnclamped, spoofMod, hitChance;
    // damage
    int baseDamage;
    float power;            // attacker Power + Aggressive Kernel bonus
    float dmgMod;           // product of the firmware damage modifiers below
    float executeMod, overchargeMod, reductionMod, firstHitMod, adaptiveMod;
    float raw;              // base x power x dmgMod
    int pen;                // weapon pen + Armor Analysis bonus, percent
    DamageSplit split;
    int armorBefore;
    int breachBonus;        // extra armor damage from Armor Breach Routine (never spills)
    int armorDamage;        // split.armorDamage + breachBonus
    int integrityDamage;
    // resources
    int energyCost, energyBefore;
    int heat, heatBefore, maxHeat;
    int scramble;
    float resist;           // target's chance to resist the scramble
} AttackPreview;

// Per-attack conditions that come from battle state rather than stats
typedef struct {
    int attackerAccuracy;   // effective (scramble penalties / precision strike applied)
    int targetMobility;     // effective (evasive maneuver / emergency evasion applied)
    int spoofActive;        // target's Targeting Spoof applies to this attack
    int breachReady;        // attacker's Armor Breach Routine still unused
    int firstAction;        // Efficient Power Distribution makes it free
    int firstHitOnTarget;   // target has not been hit this battle (Defensive Kernel)
    int targetLastMunition; // munition that last damaged the target, -1 = none (Adaptive Kernel)
    int energyTax;          // extra Energy per attack from Firmware Corruption
    int targetScrambled;    // scramble / corruption effects already pending on the target (AI only)
} AttackContext;

void attackPreview(const Mech* attacker, const Weapon* w, const Mech* target,
                   const AttackContext* ctx, AttackPreview* out);
AttackContext attackContextBaseline(const Mech* attacker, const Mech* target);   // fresh round, first action

// Enemy AI: expected value of one attack, weighed by the archetype's profile.
// Covers Energy cost, Heat headroom, Armor vs penetration, target Mobility (via
// hit chance) and Stability (via scramble resistance).
float aiScoreAttack(const Mech* attacker, const Weapon* w, const Mech* target,
                    const AttackContext* ctx, const AIProfile* ai);
#define AI_HOLD_SCORE 2.0f      // after its first action the AI stops rather than fire below this
// Hacking (capture) is a scramble attack on the target's Stability:
// chance = Strength / (Strength + Stability), clamped 5-95%. Strength comes from
// the hacker's best scrambling weapon; every scramble / corruption effect
// pending on the target lowers its Stability by HACK_STABILITY_PER_EFFECT.
#define HACK_BASE_STRENGTH 30
#define HACK_STABILITY_PER_EFFECT 15
int hackStrength(const Mech* hacker);
float hackChance(int strength, int stability);
int revisionDataForWild(const Mech* enemy);
int revisionDataForTrainer(const Mech* enemy, int tier);
int revisionDataForParticipation(const Mech* enemy);   // losing still teaches the firmware something

// ============ BATTLE STATE ============
typedef enum {
    BP_PLAYER_TURN,
    BP_ENEMY_TURN,
    BP_VICTORY,     // enemy scrapped or hacked, waiting for confirm
    BP_DEFEAT,      // player's mech disabled, waiting for confirm
    BP_OVER         // main loop should leave the battle (see battle.result)
} BattlePhase;

typedef enum { DLG_NONE = 0, DLG_INTRO, DLG_DEFEAT } BattleDialogue;
typedef enum { RESULT_NONE, RESULT_TO_WORLD, RESULT_TO_REVISION } BattleResult;

// Battle-only modifiers; the pools themselves live in mech->stats
typedef struct {
    Mech* mech;
    const AIProfile* ai;    // weights used when the AI picks for this side
    int actionsThisTurn;
    int attackedThisRound;  // for Targeting Spoof
    int breachUsed;         // Armor Breach Routine spent
    int lastStandUsed, emergencyPowerUsed;
    int evasiveBonus;       // Mobility until this side's next turn
    int missStacks;         // Recursive Targeting
    int hitTaken;           // hit at least once this battle
    int lastMunitionTaken;  // -1 = none
    // scramble effects active this turn
    int accPenalty, disabledWeapon, skipTurn;
    // scramble / corruption effects queued for this side's next turn
    int nextEnergyLoss, nextAccPenalty, nextDisabledWeapon, nextSkipTurn;
    int energyTax, nextEnergyTax;               // corruption: +Energy per attack
    int randomTargeting, nextRandomTargeting;   // corruption: attacks may fire a random weapon
    int deadManUsed;
    int skipImmune;         // normal turns left before this side can lose a turn again
} Combatant;

// A visual cue for ui_battle.c; battle logic never touches effects directly
typedef struct { int fx; int fromPlayer; int damage; int hit; Color color; } BattleEvent;
#define MAX_BATTLE_EVENTS 8

typedef struct {
    BattlePhase phase;
    BattleDialogue dialogue;
    char dialogueText[256];
    char log[256];
    Combatant player, enemy;
    Mech enemyMech;
    int enemyArchetype;     // -1 = stock chassis with random chips
    int trainer;            // -1 = wild
    int testRange;          // player vs a passive, self-rebuilding dummy
    int dummyKills;
    int savedIntegrity, savedArmor;   // player's pools before the test range
    int round;
    float animTimer;        // > 0 while an attack animation plays
    int outcomePending;     // check for scrapped mechs once the animation ends
    int dataEarned, revisionsGained, oldRevision, hacked;
    char loot[160];         // credits, salvage and job updates from this battle
    BattleResult result;
    BattleEvent events[MAX_BATTLE_EVENTS];
    int numEvents;
} Battle;

extern Battle battle;

void battleStartWild(void);
void battleStartTrainer(int trainerIdx);
void battleStartTestRange(void);
void battleEndTestRange(void);              // restore the player's mech as it was
void battleResetDummy(void);
void battleUpdate(float dt);
int battleBusy(void);                       // animating or showing dialogue
int battleCanFire(int mount, const char** reason);
void battleFire(int mount);
void battleEndTurn(void);
int battleAIChooseForPlayer(void);          // the enemy AI's pick for the player's side (tests / autoplay)
int battleCanHack(void);
int battleHackStability(void);              // enemy's effective Stability against a hack
float battleHackChance(void);
void battleHack(void);
void battleConfirm(void);                   // advance dialogue / victory / defeat
int battlePopEvent(BattleEvent* out);
int battlePreviewPlayer(int mount, AttackPreview* out);   // live numbers for a player weapon, 0 if empty
int combatMobility(const Combatant* c);     // effective values used for attacks
int combatAccuracy(const Combatant* c);

#endif
