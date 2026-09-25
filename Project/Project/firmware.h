#ifndef FIRMWARE_H
#define FIRMWARE_H

#include "stats.h"

// ============ FIRMWARE ============
// Firmware Revision -> Instruction Sockets -> Algorithmic Chips -> Algorithms
//
// A revision is stored as a single step counter: 0 = 1.0, 1 = 1.1 ... 5 = 1.5,
// 6 = 2.0 and so on. Each step has a fixed milestone (see firmwareUnlockAt).
// On top of the chips, every robot carries one Trait (rolled when it is built)
// and picks a Branch at each major revision. Chips, trait and branches all
// express themselves as ChipEffects, summed by firmwareEffect.

#define MAX_SOCKETS 8           // reached at 2.5
#define BASE_SOCKETS 2
#define BASE_CAPACITY 6
#define REVISION_STEPS 6        // x.0 .. x.5 before the next major revision
#define MAX_REVISION 29         // 5.5
#define MAX_MAJORS (MAX_REVISION / REVISION_STEPS)   // 2.0, 3.0, 4.0, 5.0
#define MAX_PROFILES 3
#define CORRUPT_TURNS 2         // a corrupted chip stays affected through the victim's next turn

// Chip-level Firmware Corruption (design doc 7.19). Energy-cost and targeting
// corruption live on the battle Combatant instead.
typedef enum { CORRUPT_DISABLED, CORRUPT_REVERSED } CorruptKind;

typedef enum {
    UNLOCK_BASE,            // 1.0  basic firmware
    UNLOCK_SOCKET,          // x.1, x.3, x.5  +1 Instruction Socket
    UNLOCK_OPTIMIZATION,    // x.2  one Optimization Point
    UNLOCK_CAPACITY,        // x.4  +2 Processing Capacity
    UNLOCK_MAJOR            // x.0  +2 Processing Capacity, choose a Firmware Branch
} RevisionUnlock;

// ============ CHIPS ============
typedef enum {
    CAT_OFFENSIVE, CAT_DEFENSIVE, CAT_MOBILITY, CAT_EW,
    CAT_BEHAVIORAL, CAT_PASSIVE, CAT_TRIGGERED, NUM_CHIP_CATEGORIES
} ChipCategory;

typedef enum {
    RARITY_STANDARD, RARITY_ADVANCED, RARITY_EXPERIMENTAL, RARITY_PROTOTYPE, RARITY_BLACK_BOX,
    NUM_RARITIES
} ChipRarity;

// What an installed chip does. Battle code asks for the summed value of an
// effect across all installed chips (firmwareChipTotal).
typedef enum {
    CFX_NONE,
    CFX_STAT,               // passive: +value to `stat`
    CFX_ENERGY_HEAT_CUT,    // Energy munitions generate value (fraction) less heat
    CFX_PEN_VS_ARMORED,     // +value Armor Penetration vs targets with > 50 Armor
    CFX_ARMOR_BREACH,       // first attack vs an armored target: +value (fraction) Armor damage
    CFX_PRECISION_STRIKE,   // first attack each turn: +value Accuracy
    CFX_FIRST_ACTION_FREE,  // first action each turn costs 0 Energy
    CFX_EMERGENCY_EVASION,  // Integrity < 25%: +value Mobility
    CFX_EVASIVE_MANEUVER,   // after attacking: +value Mobility until your next turn
    CFX_EMERGENCY_POWER,    // Integrity < 25% at round start: +value Energy
    CFX_COUNTER_INTRUSION,  // +value Stability when resisting a scramble
    CFX_TARGETING_SPOOF,    // first attack vs this robot each round: -value (fraction) hit chance
    CFX_SYSTEM_RECOVERY,    // resisting a scramble restores value Integrity
    CFX_LAST_STAND,         // first time below 10% Integrity: +value Energy
    // behavioral (IF/THEN, run automatically at the start of the robot's turn)
    CFX_EMERGENCY_REPAIR,   // IF Integrity < 30% and Energy >= 2 THEN spend 2 Energy, repair value Integrity
    CFX_COOLANT_DUMP,       // IF Heat > 75% and Energy >= 1 THEN spend 1 Energy, vent value Heat
    // prototype / black box
    CFX_OVERCHARGE,         // Energy munitions deal +value (fraction) damage and +50% Heat
    CFX_RECURSIVE_TARGETING,// every miss: +value Accuracy for the rest of the battle
    CFX_DEAD_MAN,           // on reaching 0 Integrity: one final attack, free of Energy and Heat
    // branches and traits
    CFX_EXECUTE,            // +value (fraction) damage vs targets below 50% Integrity
    CFX_PEN_BONUS,          // +value Armor Penetration on every attack
    CFX_HEAT_MULT,          // weapons generate +value (fraction) Heat
    CFX_DAMAGE_REDUCTION,   // incoming damage -value (fraction)
    CFX_BONUS_ENERGY,       // +value Energy at the start of every turn
    CFX_SCRAMBLE_BONUS,     // +value Scramble strength on scrambling weapons
    CFX_POWER_WHEN_DAMAGED, // below 50% Integrity: +value Power
    CFX_FIRST_HIT_SHIELD,   // first attack received each battle deals -value (fraction) damage
    CFX_ADAPTIVE,           // -value (fraction) damage from the munition that last damaged it
    NUM_CHIP_EFFECTS
} ChipEffect;

typedef struct {
    const char* name;
    ChipCategory category;
    ChipRarity rarity;
    int cost;               // Processing Capacity
    ChipEffect effect;
    StatId stat;            // stat the effect relates to; only CFX_STAT adds it to Stats
    float value;
    const char* desc;       // short, numeric ("+5 Accuracy.")
    const char* plain;      // what it means in play, for tooltips and shops
} ChipDef;

enum {
    CHIP_PREDICTIVE_TARGETING, CHIP_HARDENED_KERNEL, CHIP_THERMAL_OPTIMIZATION, CHIP_EFFICIENT_POWER,
    CHIP_ARMOR_ANALYSIS, CHIP_ARMOR_BREACH, CHIP_PRECISION_STRIKE, CHIP_EMERGENCY_EVASION,
    CHIP_EVASIVE_MANEUVER, CHIP_EMERGENCY_POWER, CHIP_COUNTER_INTRUSION, CHIP_TARGETING_SPOOF,
    CHIP_SYSTEM_RECOVERY, CHIP_LAST_STAND, CHIP_EMERGENCY_REPAIR, CHIP_COOLANT_DUMP, CHIP_OVERCHARGE,
    CHIP_RECURSIVE_TARGETING, CHIP_DEAD_MAN,
    NUM_CHIPS
};
extern const ChipDef chipDefs[NUM_CHIPS];     // data_chips.c

// ============ BRANCHES / TRAITS ============
// A Branch is picked at every major revision (2.0, 3.0 ...), each at most once.
// A Trait is the robot's individual kernel, like a Pokemon Ability.
typedef struct {
    const char* name;
    const char* desc;
    ChipEffect effect;  float value;
    ChipEffect effect2; float value2;   // CFX_NONE if unused (usually the drawback)
} KernelDef;

enum { BRANCH_HUNTER, BRANCH_SIEGE, BRANCH_GHOST, BRANCH_BASTION, BRANCH_OVERCLOCK, BRANCH_SIGNAL, NUM_BRANCHES };
enum { TRAIT_AGGRESSIVE, TRAIT_DEFENSIVE, TRAIT_EFFICIENT, TRAIT_ADAPTIVE, NUM_TRAITS };
extern const KernelDef branchDefs[NUM_BRANCHES];    // data_chips.c
extern const KernelDef traitDefs[NUM_TRAITS];

// Optimization Points (design doc 7.15): small permanent bumps chosen by the player
#define NUM_OPTIMIZATIONS 6
typedef struct { StatId stat; int amount; } Optimization;
extern const Optimization optimizations[NUM_OPTIMIZATIONS];

// A saved chip loadout (design doc 7.12)
typedef struct {
    char name[16];
    int saved;
    int chips[MAX_SOCKETS];
} FirmwareProfile;

typedef struct {
    int revision;                       // step counter, see top of file
    int data;                           // Revision Data toward the next step
    int optPoints;                      // unspent Optimization Points
    int optPicks[NUM_OPTIMIZATIONS];    // times each optimization was taken
    int chips[MAX_SOCKETS];             // chip index per socket, -1 = empty
    int corrupt[MAX_SOCKETS];           // turns a socket stays corrupted (battle only)
    int corruptKind[MAX_SOCKETS];       // CorruptKind
    int trait;                          // TRAIT_*
    int branches[MAX_MAJORS];           // BRANCH_* in pick order
    int numBranches;
    FirmwareProfile profiles[MAX_PROFILES];
} Firmware;

// ---- revision ----
void firmwareInit(Firmware* fw, int revision);   // sets revision, clears chips, grants its opt points (trait 0)
const char* firmwareLabel(int revision);         // "1.3"
int firmwareDataToNext(int revision);
int firmwareAddData(Firmware* fw, int amount);   // returns revision steps gained
RevisionUnlock firmwareUnlockAt(int revision);
const char* firmwareUnlockDesc(RevisionUnlock u);
void firmwareSpendOptimization(Firmware* fw, int option);
void firmwareAutoSpend(Firmware* fw);            // AI: distribute unspent points and pick pending branches

// ---- branches ----
int firmwareMajors(const Firmware* fw);          // major revisions reached
int firmwareBranchesPending(const Firmware* fw);
int firmwareHasBranch(const Firmware* fw, int branch);
void firmwarePickBranch(Firmware* fw, int branch);

// ---- derived numbers ----
int firmwareSockets(const Firmware* fw);
int firmwareCapacity(const Firmware* fw);
int firmwareCapacityUsed(const Firmware* fw);
float firmwareStatBonus(const Firmware* fw, StatId s);  // revision + optimization bonuses

// ---- chips ----
int firmwareCanInstall(const Firmware* fw, int socket, int chip);  // capacity check
void firmwareInstall(Firmware* fw, int socket, int chip);          // chip -1 = remove
int firmwareChipActive(const Firmware* fw, int socket);    // installed, within sockets, not corrupted
int firmwareChipSign(const Firmware* fw, int socket);      // +1 working, -1 reversed, 0 offline / empty
float firmwareEffect(const Firmware* fw, ChipEffect e);   // chips (reversed ones negated) + trait + branches
float firmwareChipStat(const Firmware* fw, StatId s);
int firmwareInstalledCount(const Firmware* fw);

// ---- corruption (battle only) ----
int firmwareCorruptRandom(Firmware* fw, CorruptKind kind);   // hits an active chip, returns its index or -1
void firmwareCorruptionTick(Firmware* fw);       // start of the victim's turn
void firmwareClearCorruption(Firmware* fw);

// ---- profiles ----
void firmwareSaveProfile(Firmware* fw, int slot, const char* name);   // name NULL keeps the old one
const char* chipCategoryName(ChipCategory c);
const char* chipRarityName(ChipRarity r);

// ---- player chip collection ----
extern int chipOwned[NUM_CHIPS];
void chipCollectionInit(void);

#endif
