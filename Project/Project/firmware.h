#ifndef FIRMWARE_H
#define FIRMWARE_H

#include "stats.h"

// ============ FIRMWARE ============
// Firmware Revision -> Instruction Sockets -> Algorithmic Chips -> Combat Instructions
//
// A revision is stored as a single step counter: 0 = 1.0, 1 = 1.1 ... 5 = 1.5,
// 6 = 2.0 and so on. Each step has a fixed milestone (see firmwareUnlockAt).

#define MAX_SOCKETS 8
#define BASE_SOCKETS 2
#define BASE_CAPACITY 6
#define REVISION_STEPS 6        // x.0 .. x.5 before the next major revision
#define MAX_REVISION 29         // 5.5

typedef enum {
    UNLOCK_BASE,            // 1.0
    UNLOCK_MINOR_TUNING,    // x.1  +2 Accuracy, +2 Stability
    UNLOCK_OPTIMIZATION,    // x.2  one Optimization Point
    UNLOCK_MINOR_HARDENING, // x.3  +5 Heat capacity, +5 Armor
    UNLOCK_CAPACITY,        // x.4  +2 Processing Capacity
    UNLOCK_SOCKET,          // x.5  +1 Instruction Socket
    UNLOCK_MAJOR            // x.0  +1 Socket, +2 Capacity (branches/traits later)
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
    const char* desc;
} ChipDef;

enum {
    CHIP_PREDICTIVE_TARGETING, CHIP_HARDENED_KERNEL, CHIP_THERMAL_OPTIMIZATION, CHIP_EFFICIENT_POWER,
    CHIP_ARMOR_ANALYSIS, CHIP_ARMOR_BREACH, CHIP_PRECISION_STRIKE, CHIP_EMERGENCY_EVASION,
    CHIP_EVASIVE_MANEUVER, CHIP_EMERGENCY_POWER, CHIP_COUNTER_INTRUSION, CHIP_TARGETING_SPOOF,
    CHIP_SYSTEM_RECOVERY, CHIP_LAST_STAND,
    NUM_CHIPS
};
extern const ChipDef chipDefs[NUM_CHIPS];     // data_chips.c

// Optimization Points (design doc 7.15): small permanent bumps chosen by the player
#define NUM_OPTIMIZATIONS 6
typedef struct { StatId stat; int amount; } Optimization;
extern const Optimization optimizations[NUM_OPTIMIZATIONS];

typedef struct {
    int revision;                       // step counter, see top of file
    int data;                           // Revision Data toward the next step
    int optPoints;                      // unspent Optimization Points
    int optPicks[NUM_OPTIMIZATIONS];    // times each optimization was taken
    int chips[MAX_SOCKETS];             // chip index per socket, -1 = empty
} Firmware;

// ---- revision ----
void firmwareInit(Firmware* fw, int revision);   // sets revision, clears chips, grants its opt points
const char* firmwareLabel(int revision);         // "1.3"
int firmwareDataToNext(int revision);
int firmwareAddData(Firmware* fw, int amount);   // returns revision steps gained
RevisionUnlock firmwareUnlockAt(int revision);
const char* firmwareUnlockDesc(RevisionUnlock u);
void firmwareSpendOptimization(Firmware* fw, int option);
void firmwareAutoSpend(Firmware* fw);            // AI: distribute unspent points

// ---- derived numbers ----
int firmwareSockets(const Firmware* fw);
int firmwareCapacity(const Firmware* fw);
int firmwareCapacityUsed(const Firmware* fw);
float firmwareStatBonus(const Firmware* fw, StatId s);  // revision + optimization bonuses

// ---- chips ----
int firmwareCanInstall(const Firmware* fw, int socket, int chip);  // capacity check
void firmwareInstall(Firmware* fw, int socket, int chip);          // chip -1 = remove
float firmwareChipTotal(const Firmware* fw, ChipEffect e);
float firmwareChipStat(const Firmware* fw, StatId s);
const char* chipCategoryName(ChipCategory c);
const char* chipRarityName(ChipRarity r);

// ---- player chip collection ----
extern int chipOwned[NUM_CHIPS];
void chipCollectionInit(void);

#endif
