#include "firmware.h"
#include "raylib.h"
#include <string.h>

// Optimization Points (design doc 7.15)
const Optimization optimizations[NUM_OPTIMIZATIONS] = {
    { STAT_INTEGRITY, 5 },
    { STAT_ARMOR, 3 },
    { STAT_ACCURACY, 3 },
    { STAT_STABILITY, 3 },
    { STAT_MOBILITY, 5 },
    { STAT_HEAT, 5 },
};

int chipOwned[NUM_CHIPS];

// ============ REVISIONS ============
RevisionUnlock firmwareUnlockAt(int revision) {
    if (revision <= 0) return UNLOCK_BASE;
    switch (revision % REVISION_STEPS) {
    case 0: return UNLOCK_MAJOR;
    case 1: return UNLOCK_MINOR_TUNING;
    case 2: return UNLOCK_OPTIMIZATION;
    case 3: return UNLOCK_MINOR_HARDENING;
    case 4: return UNLOCK_CAPACITY;
    default: return UNLOCK_SOCKET;
    }
}

const char* firmwareUnlockDesc(RevisionUnlock u) {
    switch (u) {
    case UNLOCK_BASE:            return "Basic firmware";
    case UNLOCK_MINOR_TUNING:    return "Minor revision: +2 Accuracy, +2 Stability";
    case UNLOCK_OPTIMIZATION:    return "Firmware optimization: +1 Optimization Point";
    case UNLOCK_MINOR_HARDENING: return "Minor revision: +5 Heat capacity, +5 Armor";
    case UNLOCK_CAPACITY:        return "Algorithmic Capacity +2";
    case UNLOCK_SOCKET:          return "Instruction Socket +1";
    default:                     return "MAJOR REVISION: Instruction Socket +1, Capacity +2";
    }
}

// Number of steps in 1..revision that granted a given unlock
static int countUnlocks(int revision, RevisionUnlock u) {
    int n = 0;
    for (int r = 1; r <= revision; r++)
        if (firmwareUnlockAt(r) == u) n++;
    return n;
}

const char* firmwareLabel(int revision) {
    return TextFormat("%d.%d", 1 + revision / REVISION_STEPS, revision % REVISION_STEPS);
}

int firmwareDataToNext(int revision) { return 20 + revision * 8; }

void firmwareInit(Firmware* fw, int revision) {
    memset(fw, 0, sizeof(*fw));
    if (revision < 0) revision = 0;
    if (revision > MAX_REVISION) revision = MAX_REVISION;
    fw->revision = revision;
    fw->optPoints = countUnlocks(revision, UNLOCK_OPTIMIZATION);
    for (int i = 0; i < MAX_SOCKETS; i++) fw->chips[i] = -1;
}

int firmwareAddData(Firmware* fw, int amount) {
    int gained = 0;
    fw->data += amount;
    while (fw->revision < MAX_REVISION && fw->data >= firmwareDataToNext(fw->revision)) {
        fw->data -= firmwareDataToNext(fw->revision);
        fw->revision++;
        if (firmwareUnlockAt(fw->revision) == UNLOCK_OPTIMIZATION) fw->optPoints++;
        gained++;
    }
    if (fw->revision >= MAX_REVISION) fw->data = 0;
    return gained;
}

void firmwareSpendOptimization(Firmware* fw, int option) {
    if (fw->optPoints <= 0 || option < 0 || option >= NUM_OPTIMIZATIONS) return;
    fw->optPoints--;
    fw->optPicks[option]++;
}

void firmwareAutoSpend(Firmware* fw) {
    for (int i = 0; fw->optPoints > 0; i++)
        firmwareSpendOptimization(fw, i % NUM_OPTIMIZATIONS);
}

// ============ DERIVED ============
int firmwareSockets(const Firmware* fw) {
    int s = BASE_SOCKETS + countUnlocks(fw->revision, UNLOCK_SOCKET) + countUnlocks(fw->revision, UNLOCK_MAJOR);
    return s > MAX_SOCKETS ? MAX_SOCKETS : s;
}

int firmwareCapacity(const Firmware* fw) {
    return BASE_CAPACITY + 2 * (countUnlocks(fw->revision, UNLOCK_CAPACITY) + countUnlocks(fw->revision, UNLOCK_MAJOR));
}

int firmwareCapacityUsed(const Firmware* fw) {
    int used = 0;
    for (int i = 0; i < MAX_SOCKETS; i++)
        if (fw->chips[i] >= 0) used += chipDefs[fw->chips[i]].cost;
    return used;
}

float firmwareStatBonus(const Firmware* fw, StatId s) {
    float bonus = 0;
    int tuning = countUnlocks(fw->revision, UNLOCK_MINOR_TUNING);
    int hardening = countUnlocks(fw->revision, UNLOCK_MINOR_HARDENING);
    if (s == STAT_ACCURACY || s == STAT_STABILITY) bonus += 2.0f * tuning;
    if (s == STAT_HEAT || s == STAT_ARMOR) bonus += 5.0f * hardening;
    for (int i = 0; i < NUM_OPTIMIZATIONS; i++)
        if (optimizations[i].stat == s) bonus += (float)(optimizations[i].amount * fw->optPicks[i]);
    return bonus;
}

// ============ CHIPS ============
int firmwareCanInstall(const Firmware* fw, int socket, int chip) {
    if (socket < 0 || socket >= firmwareSockets(fw)) return 0;
    if (chip < 0) return 1;
    int used = firmwareCapacityUsed(fw);
    if (fw->chips[socket] >= 0) used -= chipDefs[fw->chips[socket]].cost;
    return used + chipDefs[chip].cost <= firmwareCapacity(fw);
}

void firmwareInstall(Firmware* fw, int socket, int chip) {
    if (socket < 0 || socket >= MAX_SOCKETS) return;
    fw->chips[socket] = chip < 0 ? -1 : chip;
}

float firmwareChipTotal(const Firmware* fw, ChipEffect e) {
    float total = 0;
    int sockets = firmwareSockets(fw);
    for (int i = 0; i < sockets; i++)
        if (fw->chips[i] >= 0 && chipDefs[fw->chips[i]].effect == e) total += chipDefs[fw->chips[i]].value;
    return total;
}

float firmwareChipStat(const Firmware* fw, StatId s) {
    float total = 0;
    int sockets = firmwareSockets(fw);
    for (int i = 0; i < sockets; i++) {
        int c = fw->chips[i];
        if (c >= 0 && chipDefs[c].effect == CFX_STAT && chipDefs[c].stat == s) total += chipDefs[c].value;
    }
    return total;
}

const char* chipCategoryName(ChipCategory c) {
    static const char* names[NUM_CHIP_CATEGORIES] = {
        "OFFENSIVE", "DEFENSIVE", "MOBILITY", "ELECTRONIC WARFARE", "BEHAVIORAL", "PASSIVE", "TRIGGERED"
    };
    return names[c];
}

const char* chipRarityName(ChipRarity r) {
    static const char* names[NUM_RARITIES] = { "STANDARD", "ADVANCED", "EXPERIMENTAL", "PROTOTYPE", "BLACK BOX" };
    return names[r];
}

// Starting chip collection (temporary until chips drop from battles / shops)
void chipCollectionInit(void) {
    memset(chipOwned, 0, sizeof(chipOwned));
    chipOwned[CHIP_PREDICTIVE_TARGETING] = 2;
    chipOwned[CHIP_HARDENED_KERNEL] = 1;
    chipOwned[CHIP_ARMOR_ANALYSIS] = 1;
    chipOwned[CHIP_EVASIVE_MANEUVER] = 1;
    chipOwned[CHIP_COUNTER_INTRUSION] = 1;
    chipOwned[CHIP_PRECISION_STRIKE] = 1;
}
