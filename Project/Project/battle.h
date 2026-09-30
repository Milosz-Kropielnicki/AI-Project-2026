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
    float formatMod;        // team-battle damage pass (TEAM_DAMAGE_SCALE)
    float splashMod;        // area / cone falloff for a secondary target
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

// ============ ATTACK EXPLANATION ============
// The reasons behind one attack's numbers in plain language. The same lines
// feed the weapon tooltip (before firing) and the battle log (after).
#define EXPLAIN_LINES 12
#define EXPLAIN_LEN 120
typedef struct {
    int n;
    char line[EXPLAIN_LINES][EXPLAIN_LEN];
    int warn[EXPLAIN_LINES];            // 1 = draw as a warning
} Explanation;

// ============ BATTLE LOG ============
#define LOG_HISTORY 48
typedef struct {
    char text[256];
    int side;                           // 1 = player, 0 = enemy, -1 = system
    int munition;                       // -1 = not an attack
    int round;
    Explanation why;                    // empty for system lines
} LogEntry;
int battleLogCount(void);
const LogEntry* battleLogEntry(int back);   // 0 = newest

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
    float formatMod;        // team-battle damage pass, 1 outside battle
    float splashMod;        // 1 for the primary target, x0.75 per extra area / cone target
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
    BP_DEFEAT,      // player's last mech disabled (or yielded), waiting for confirm
    BP_DEPLOY,      // no player mech on the field: deploy a reserve (free) or yield
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
    int switchLock;         // own turns left during which it can't be switched out
    int switchLocked;       // this turn: just switched in, can't switch out
    int done;               // finished acting this phase (or arrived this round)
    int fielded;            // has been on the field this battle (shares Revision Data)
    int archetype;          // enemy archetype it was built from, -1 = none
    int out;                // scrapped or reprogrammed: no longer part of the fight
} Combatant;

// ============ SIDES ============
// Each side owns battle copies of its mechs. Slots hold the whole squad
// (field + reserves); field[] says which slots are on the field. The player's
// copies are written back to team[] when the battle ends.
#define MAX_FIELD 3
#define SIDE_ENEMY 0
#define SIDE_PLAYER 1
typedef struct {
    Mech mech[MAX_TEAM];
    Combatant slot[MAX_TEAM];
    int rosterIndex[MAX_TEAM];  // team[] index the slot writes back to, -1 = none
    int field[MAX_FIELD];       // slot on each field position, -1 = empty
    int count, numField;
    int startRevision[MAX_TEAM];    // firmware revision when the battle began
} Side;

// A visual cue for ui_battle.c; battle logic never touches effects directly
typedef struct {
    int fx, fromPlayer, damage, hit;
    Color color;
    int munition;                       // drives impact particles and sound
    int armorDamage, integrityDamage;   // shown as separate numbers
    int lethal;
    int fromSlot, toSlot;               // field positions of the attacker and the target
} BattleEvent;
#define MAX_BATTLE_EVENTS 8

typedef struct {
    BattlePhase phase;
    BattleDialogue dialogue;
    char dialogueText[256];
    char log[256];
    Side side[2];           // SIDE_ENEMY, SIDE_PLAYER
    int actingSlot;         // player's field position being commanded
    int playerTarget;       // enemy field position the player is aiming at
    int enemyActing;        // enemy field position acting in the enemy phase
    int phaseActed;         // the player did something this phase (auto-ends once all are spent)
    int trainer;            // -1 = wild
    int testRange;          // player vs a passive, self-rebuilding dummy
    int dummyKills;
    int round;
    float animTimer;        // > 0 while an attack animation plays
    int outcomePending;     // check for scrapped mechs once the animation ends
    int dataEarned, revisionsGained, hacked;
    // team mechs that gained revisions this battle, for the revision screen
    int revTeam[MAX_TEAM], revFrom[MAX_TEAM], numRevisions;
    char loot[160];         // credits, salvage and job updates from this battle
    BattleResult result;
    BattleEvent events[MAX_BATTLE_EVENTS];
    int numEvents;
} Battle;

extern Battle battle;

Combatant* battleField(int side, int pos);  // NULL if that field position is empty
Combatant* battleActing(void);              // player mech being commanded, NULL if none can be
Combatant* battleTarget(void);              // enemy the player is aiming at, NULL if none
// Never NULL (UI / tests): the acting mech or target, else any field mech, else slot 0
Combatant* battleFieldPlayer(void);
Combatant* battleFieldEnemy(void);
int battleFieldCount(int side);             // mechs standing on the field

// ============ TEAM PHASES (3v3) ============
// Shared round: the player phase, then the enemy phase. In a phase each field
// mech acts once, in any order, with its own Energy. Weapons hit the selected
// target; AREA weapons also hit every other enemy on the field and CONE weapons
// the neighbouring positions, each extra target taking SPLASH_FALLOFF less
// (compounding). Every target is rolled and resolved separately.
#define SPLASH_FALLOFF 0.75f
#ifndef TEAM_DAMAGE_SCALE
#define TEAM_DAMAGE_SCALE 0.80f     // damage pass: focus fire from three mechs, so every hit does 20% less
#endif
#define TEAM_DATA_BONUS 1.5f        // a kill's Revision Data, shared by every mech that took the field
void battleSelectActor(int pos);            // command this field mech
void battleNextActor(void);                 // TAB
void battleSetTarget(int pos);
void battleCycleTarget(int dir);            // Q / E
int battleCanDeploy(void);                  // an empty field position and a standing reserve
int battlePreviewTargets(int mount, int* pos, AttackPreview* out, int max);   // every target the shot reaches
Mech* battleRosterMech(int teamIdx);        // battle copy of team[teamIdx], NULL if not in this battle

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
int battleAIChooseForPlayer(void);
int battleExplainPlayer(int mount, Explanation* out);   // why the selected weapon would do what it does; 0 if empty
// Weapons that would be blocked by the Thermal Limit next turn if this mount fires now
int battleHeatBlocksNextTurn(int mount, int* blocked, int max);          // the enemy AI's pick for the player's side (tests / autoplay)
// ============ SWITCHING ============
// A switch is a field mech's action: it needs 1 Energy, the outgoing mech's
// remaining Energy is lost and the incoming mech takes its position but doesn't
// act this round; it also can't be switched out on its next turn. Integrity,
// Armor, Heat and queued scrambles stay with each mech. A disabled mech leaves
// its position empty; a reserve can deploy there for free on the next phase.
// The battle is lost when no mech is left standing.
#define SWITCH_ENERGY_COST 1
#define AI_SWITCH_BELOW 0.25f       // enemy pulls a machine out below 25% Integrity...
#define HEALTHY_RESERVE 0.50f       // ...if a reserve with 50%+ Integrity can come in
int battleCanSwitch(const char** reason);   // player can switch right now
int battleSwitchList(int* out, int max);    // player slots that can come in (standing reserves)
void battleSwitchTo(int slot);
void battleDeploy(int slot);                // free: reserve into an empty field position (it acts next round)
void battleYield(void);                     // give up the battle
int battleSlotStanding(int side, int slot); // in the fight: not scrapped, disabled or captured
int battleSlotOnField(int side, int slot);
int battleSideStanding(int side);           // standing mechs left on a side

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
