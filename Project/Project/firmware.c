#include "firmware.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
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
// Milestones (design doc 7.4): x.1 Socket, x.2 Optimization, x.3 Socket,
// x.4 Capacity, x.5 Socket, then the next major revision.
RevisionUnlock firmwareUnlockAt(int revision) {
    if (revision <= 0) return UNLOCK_BASE;
    switch (revision % REVISION_STEPS) {
    case 0: return UNLOCK_MAJOR;
    case 2: return UNLOCK_OPTIMIZATION;
    case 4: return UNLOCK_CAPACITY;
    default: return UNLOCK_SOCKET;
    }
}

const char* firmwareUnlockDesc(RevisionUnlock u) {
    switch (u) {
    case UNLOCK_BASE:         return "Basic firmware";
    case UNLOCK_SOCKET:       return "Instruction Socket +1";
    case UNLOCK_OPTIMIZATION: return "Firmware optimization: +1 Optimization Point";
    case UNLOCK_CAPACITY:     return "Processing Capacity +2";
    default:                  return "MAJOR REVISION: Capacity +2, choose a Firmware Branch";
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
    for (int p = 0; p < MAX_PROFILES; p++) {
        snprintf(fw->profiles[p].name, sizeof(fw->profiles[p].name), "PROFILE %c", 'A' + p);
        for (int i = 0; i < MAX_SOCKETS; i++) fw->profiles[p].chips[i] = -1;
    }
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
    while (firmwareBranchesPending(fw) > 0) {
        int b = rand() % NUM_BRANCHES;
        while (firmwareHasBranch(fw, b)) b = (b + 1) % NUM_BRANCHES;
        firmwarePickBranch(fw, b);
    }
}

// ============ BRANCHES ============
int firmwareMajors(const Firmware* fw) { return countUnlocks(fw->revision, UNLOCK_MAJOR); }
int firmwareBranchesPending(const Firmware* fw) { return firmwareMajors(fw) - fw->numBranches; }

int firmwareHasBranch(const Firmware* fw, int branch) {
    for (int i = 0; i < fw->numBranches; i++) if (fw->branches[i] == branch) return 1;
    return 0;
}

void firmwarePickBranch(Firmware* fw, int branch) {
    if (branch < 0 || branch >= NUM_BRANCHES || firmwareBranchesPending(fw) <= 0 || firmwareHasBranch(fw, branch)) return;
    fw->branches[fw->numBranches++] = branch;
}

// ============ DERIVED ============
int firmwareSockets(const Firmware* fw) {
    int s = BASE_SOCKETS + countUnlocks(fw->revision, UNLOCK_SOCKET);
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

// Firmware is behavioral (doc 7.14): the only flat stat bonuses are the
// player's Optimization Points.
float firmwareStatBonus(const Firmware* fw, StatId s) {
    float bonus = 0;
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
    fw->corrupt[socket] = 0;
}

int firmwareChipActive(const Firmware* fw, int socket) {
    return socket >= 0 && socket < firmwareSockets(fw) && fw->chips[socket] >= 0 && fw->corrupt[socket] <= 0;
}

int firmwareChipSign(const Firmware* fw, int socket) {
    if (socket < 0 || socket >= firmwareSockets(fw) || fw->chips[socket] < 0) return 0;
    if (fw->corrupt[socket] <= 0) return 1;
    return fw->corruptKind[socket] == CORRUPT_REVERSED ? -1 : 0;
}

static float kernelEffect(const KernelDef* k, ChipEffect e) {
    return (k->effect == e ? k->value : 0) + (k->effect2 == e ? k->value2 : 0);
}

float firmwareEffect(const Firmware* fw, ChipEffect e) {
    float total = 0;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        int sign = firmwareChipSign(fw, i);
        if (sign != 0 && chipDefs[fw->chips[i]].effect == e) total += sign * chipDefs[fw->chips[i]].value;
    }
    total += kernelEffect(&traitDefs[fw->trait], e);
    for (int i = 0; i < fw->numBranches; i++) total += kernelEffect(&branchDefs[fw->branches[i]], e);
    return total;
}

float firmwareChipStat(const Firmware* fw, StatId s) {
    float total = 0;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        int sign = firmwareChipSign(fw, i);
        if (sign == 0) continue;
        const ChipDef* c = &chipDefs[fw->chips[i]];
        if (c->effect == CFX_STAT && c->stat == s) total += sign * c->value;
    }
    return total;
}

int firmwareInstalledCount(const Firmware* fw) {
    int n = 0;
    for (int i = 0; i < firmwareSockets(fw); i++) if (fw->chips[i] >= 0) n++;
    return n;
}

// ============ CORRUPTION ============
int firmwareCorruptRandom(Firmware* fw, CorruptKind kind) {
    int sockets[MAX_SOCKETS], n = 0;
    for (int i = 0; i < MAX_SOCKETS; i++) if (firmwareChipActive(fw, i)) sockets[n++] = i;
    if (n == 0) return -1;
    int s = sockets[rand() % n];
    fw->corrupt[s] = CORRUPT_TURNS;
    fw->corruptKind[s] = kind;
    return fw->chips[s];
}

void firmwareCorruptionTick(Firmware* fw) {
    for (int i = 0; i < MAX_SOCKETS; i++) if (fw->corrupt[i] > 0) fw->corrupt[i]--;
}

void firmwareClearCorruption(Firmware* fw) { memset(fw->corrupt, 0, sizeof(fw->corrupt)); }

// ============ PROFILES ============
void firmwareSaveProfile(Firmware* fw, int slot, const char* name) {
    if (slot < 0 || slot >= MAX_PROFILES) return;
    FirmwareProfile* p = &fw->profiles[slot];
    if (name) snprintf(p->name, sizeof(p->name), "%s", name);
    memcpy(p->chips, fw->chips, sizeof(p->chips));
    p->saved = 1;
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

// Starting chip collection; more are salvaged from reprogrammed machines
void chipCollectionInit(void) {
    memset(chipOwned, 0, sizeof(chipOwned));
    chipOwned[CHIP_PREDICTIVE_TARGETING] = 2;
    chipOwned[CHIP_HARDENED_KERNEL] = 1;
    chipOwned[CHIP_ARMOR_ANALYSIS] = 1;
    chipOwned[CHIP_EVASIVE_MANEUVER] = 1;
    chipOwned[CHIP_COUNTER_INTRUSION] = 1;
    chipOwned[CHIP_PRECISION_STRIKE] = 1;
    chipOwned[CHIP_COOLANT_DUMP] = 1;
}
